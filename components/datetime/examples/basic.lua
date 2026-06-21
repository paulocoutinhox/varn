-- parses ISO-8601 strings, formats them, does arithmetic, and measures a difference.
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local datetime = require("datetime")

local now = datetime.now()
print("now (iso):", now:iso())
print("now (unix):", now:unix())

-- parse a UTC datetime and a bare date.
local launch = datetime.parse("2026-06-21T08:30:00Z")
print("launch:", launch:iso())
print("launch (custom):", launch:format("%Y-%m-%d %H:%M"))

local day = datetime.parse("2026-06-21")
print("date only:", day:iso())

-- an offset is normalized to UTC: +02:00 is two hours ahead, so the UTC instant is two hours earlier.
local berlin = datetime.parse("2026-06-21T10:30:00+02:00")
print("offset to utc:", berlin:iso())
print("same instant as launch:", berlin == launch)

-- arithmetic returns a new instant; the original is unchanged.
local later = launch:add({ days = 1, hours = 2, minutes = 30 })
print("launch + 1d2h30m:", later:iso())
print("seconds between:", later:diff(launch))

-- round-trip through a unix timestamp.
local roundTrip = datetime.fromUnix(launch:unix())
print("round-trip equal:", roundTrip == launch)

print("datetime basic ok")
