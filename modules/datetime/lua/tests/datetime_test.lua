local datetime = require("datetime")

-- parse and iso round-trip, in utc.
assert(datetime.parse("2026-06-21T12:30:00Z"):iso() == "2026-06-21T12:30:00Z", "iso round-trip")
assert(datetime.parse("2026-06-21"):iso() == "2026-06-21T00:00:00Z", "bare date is midnight utc")
assert(datetime.parse("2026-06-21 12:30:00"):iso() == "2026-06-21T12:30:00Z", "space separator")

-- a trailing offset is normalized back to utc.
assert(datetime.parse("2026-06-21T12:30:00+03:00"):iso() == "2026-06-21T09:30:00Z", "positive offset to utc")
assert(datetime.parse("2026-06-21T12:30:00-0500"):iso() == "2026-06-21T17:30:00Z", "negative offset to utc")

-- sub-second precision survives parse, iso, and millis.
local frac = datetime.parse("2026-06-21T12:30:00.123Z")
assert(frac:iso() == "2026-06-21T12:30:00.123Z", "fractional iso")
assert(frac:millis() % 1000 == 123, "fractional millis")

-- fromFields builds the same instant as the equivalent iso string.
assert(datetime.fromFields({ year = 2026, month = 6, day = 21, hour = 12, minute = 30 })
    == datetime.parse("2026-06-21T12:30:00Z"), "fromFields equals parse")

-- field access.
local f = datetime.parse("2026-06-21T12:30:45Z"):fields()
assert(f.year == 2026 and f.month == 6 and f.day == 21, "date fields")
assert(f.hour == 12 and f.minute == 30 and f.second == 45, "time fields")
assert(f.weekday == 7 and f.yearday == 172, "2026-06-21 is a sunday, day 172")

local d = datetime.parse("2026-06-21T12:30:45Z")
assert(d:year() == 2026 and d:month() == 6 and d:day() == 21, "scalar date accessors")
assert(d:hour() == 12 and d:minute() == 30 and d:second() == 45, "scalar time accessors")
assert(d:weekdayName() == "Sunday" and d:monthName() == "June", "named accessors")

-- leap years and month lengths.
assert(datetime.parse("2024-02-10"):isLeapYear() and datetime.parse("2024-02-10"):daysInMonth() == 29, "leap february")
assert(not datetime.parse("2026-02-10"):isLeapYear() and datetime.parse("2026-02-10"):daysInMonth() == 28, "common february")
assert(datetime.parse("2026-06-10"):daysInMonth() == 30, "june has 30 days")

-- calendar-aware arithmetic clamps to the end of a shorter month.
assert(datetime.parse("2026-01-31T00:00:00Z"):add({ months = 1 }):iso() == "2026-02-28T00:00:00Z", "jan 31 + 1 month clamps")
assert(datetime.parse("2024-02-29T00:00:00Z"):add({ years = 1 }):iso() == "2025-02-28T00:00:00Z", "leap day + 1 year clamps")

-- duration arithmetic crosses day boundaries.
assert(datetime.parse("2026-06-21T12:00:00Z"):add({ hours = 25 }):iso() == "2026-06-22T13:00:00Z", "add 25 hours")
local base = datetime.parse("2026-06-21T12:00:00Z")
assert(base:add({ days = 3, hours = 5 }):subtract({ days = 3, hours = 5 }) == base, "add then subtract is identity")

-- diff in plain and calendar units.
local later = datetime.parse("2026-03-15")
local earlier = datetime.parse("2026-01-10")
assert(later:diffIn(earlier, "days") == 64, "64 days apart")
assert(later:diffIn(earlier, "months") == 2, "2 whole months apart")
assert(earlier:diffIn(later, "months") == -2, "diff is signed")
assert(datetime.parse("2026-06-21"):diffIn(datetime.parse("2020-06-21"), "years") == 6, "6 years apart")
assert(datetime.parse("2026-06-21T01:00:00Z"):diff(datetime.parse("2026-06-21T00:00:00Z")) == 3600, "diff in seconds")

-- start and end of a unit.
local m = datetime.parse("2026-06-21T12:30:45.500Z")
assert(m:startOf("day"):iso() == "2026-06-21T00:00:00Z", "start of day")
assert(m:endOf("day"):iso() == "2026-06-21T23:59:59.999Z", "end of day")
assert(m:startOf("month"):iso() == "2026-06-01T00:00:00Z", "start of month")
assert(m:endOf("month"):iso() == "2026-06-30T23:59:59.999Z", "end of month")
assert(m:startOf("year"):iso() == "2026-01-01T00:00:00Z", "start of year")
assert(m:startOf("week"):iso() == "2026-06-15T00:00:00Z", "iso week starts monday")

-- fixed-offset rendering keeps the same instant but shifts the wall clock.
local noon = datetime.parse("2026-06-21T12:00:00Z")
assert(noon:iso(180) == "2026-06-21T15:00:00+03:00", "render at +03:00")
assert(noon:iso(-300) == "2026-06-21T07:00:00-05:00", "render at -05:00")
local of = noon:fields(180)
assert(of.hour == 15 and of.offset == 180, "fields at offset")

-- comparisons and min/max.
local a = datetime.parse("2026-01-01")
local b = datetime.parse("2026-02-01")
assert(a < b and a <= b and b > a and a ~= b, "ordering operators")
assert(datetime.min(b, a, datetime.parse("2026-03-01")) == a, "min")
assert(datetime.max(a, b, datetime.parse("2025-01-01")) == b, "max")

-- tostring and a sane now().
assert(tostring(datetime.parse("2026-06-21T12:30:00Z")) == "2026-06-21T12:30:00Z", "tostring is iso")
assert(datetime.now():millis() > 1577836800000, "now is after 2020")

-- a string with no separators or junk is rejected.
assert(not pcall(datetime.parse, "not a date"), "rejects garbage")
assert(not pcall(datetime.parse, "2026-13-01"), "rejects invalid month")

print("datetime ok")
