-- assert-based test for datetime: ISO parsing (date, datetime, Z, +/- offsets), formatting, unix round-trip, arithmetic, diffs, comparisons, and parse errors.
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local datetime = require("datetime")

-- a known UTC instant: 2026-06-21T08:30:00Z.
local base = datetime.parse("2026-06-21T08:30:00Z")
assert(base:iso() == "2026-06-21T08:30:00Z", "iso round-trips a Z datetime")

-- a datetime with no offset is read as UTC.
local naive = datetime.parse("2026-06-21T08:30:00")
assert(naive == base, "no-offset datetime is UTC")

-- a bare date is midnight UTC.
local day = datetime.parse("2026-06-21")
assert(day:iso() == "2026-06-21T00:00:00Z", "bare date is midnight UTC")

-- a space separator is accepted.
local spaced = datetime.parse("2026-06-21 08:30:00")
assert(spaced == base, "space separator parses")

-- a positive offset shifts back to an earlier UTC instant.
local plus = datetime.parse("2026-06-21T10:30:00+02:00")
assert(plus == base, "+02:00 normalizes to UTC")

-- a negative offset shifts forward.
local minus = datetime.parse("2026-06-21T05:30:00-03:00")
assert(minus == base, "-03:00 normalizes to UTC")

-- offset without a colon is accepted.
local compact = datetime.parse("2026-06-21T10:30:00+0200")
assert(compact == base, "+0200 offset parses")

-- unix round-trip.
local rt = datetime.fromUnix(base:unix())
assert(rt == base, "fromUnix(unix) round-trips")

-- custom format in UTC.
assert(base:format("%Y-%m-%d") == "2026-06-21", "custom format")
assert(base:format("%H:%M") == "08:30", "time format")

-- arithmetic produces a new instant and leaves the original alone.
local later = base:add({ days = 1, hours = 2, minutes = 30, seconds = 15 })
local expected = (1 * 86400) + (2 * 3600) + (30 * 60) + 15
assert(later:diff(base) == expected, "add then diff matches the delta")
assert(base:iso() == "2026-06-21T08:30:00Z", "add does not mutate the receiver")

-- negative components subtract.
local earlier = base:add({ hours = -1 })
assert(base:diff(earlier) == 3600, "negative add goes backward")

-- comparisons.
assert(earlier < base, "lt")
assert(base <= base, "le reflexive")
assert(later > base, "gt via lt")

-- diff sign: self - other.
assert(base:diff(later) == -expected, "diff is signed")

-- parse errors.
assert(not pcall(datetime.parse, "not a date"), "garbage raises")
assert(not pcall(datetime.parse, "2026-06-21T08:30"), "missing seconds raises")
assert(not pcall(datetime.parse, 12345), "non-string raises")

print("datetime integration ok")
