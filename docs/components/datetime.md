# 🕒 datetime

Parse, format, and do arithmetic on instants. A datetime is stored as a unix timestamp (seconds,
UTC), so arithmetic and diffs are plain integer math. ISO-8601 parsing and formatting add what
`os.date` alone does not: reading a string with an optional `Z` or `±hh:mm` offset and normalizing
it to UTC.

Add the components directory to `package.path` first (see [components](../components.md)):

```lua
package.path = package.path .. ";./components/?.lua;./components/?/init.lua"
local datetime = require("datetime")
```

## Constructing

- `datetime.now()` → a datetime for the current instant.
- `datetime.fromUnix(seconds)` → a datetime at that unix timestamp.
- `datetime.parse(text)` → a datetime parsed from an ISO-8601 string. Raises on anything it cannot
  parse.

`parse` accepts:

| Input | Read as |
|-------|---------|
| `2026-06-21` | midnight UTC |
| `2026-06-21T08:30:00` | UTC (no offset means UTC) |
| `2026-06-21 08:30:00` | space separator, UTC |
| `2026-06-21T08:30:00Z` | UTC |
| `2026-06-21T10:30:00+02:00` | shifted to UTC (`+0200` without the colon also works) |

## Methods

- `obj:unix()` → the underlying unix timestamp in seconds.
- `obj:format(fmt?)` → the instant rendered with an `os.date` format string, in UTC. Defaults to
  ISO-8601.
- `obj:iso()` → the instant as an ISO-8601 UTC string with a trailing `Z`.
- `obj:add({days=, hours=, minutes=, seconds=})` → a new datetime shifted by the given amounts (any
  subset, may be negative). The receiver is unchanged.
- `obj:diff(other)` → whole seconds from `other` to `self` (`self - other`), positive when `self` is
  later.

Two datetimes compare with `==`, `<`, `<=`, and print as their ISO form.

```lua
local launch = datetime.parse("2026-06-21T08:30:00Z")
local later  = launch:add({ days = 1, hours = 2, minutes = 30 })

print(later:iso())          -- 2026-06-22T11:00:00Z
print(later:diff(launch))   -- 95400
print(later > launch)       -- true

-- an offset is normalized, so these are the same instant:
print(datetime.parse("2026-06-21T10:30:00+02:00") == launch)  -- true
```

## Example

### `basic.lua`

Parses several ISO forms (a UTC datetime, a bare date, an offset datetime), formats them, adds a
delta, measures a difference, and round-trips through a unix timestamp.
