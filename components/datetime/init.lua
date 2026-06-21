-- datetime: parse, format, and do arithmetic on instants. an instant is stored as a unix timestamp (seconds, UTC) so arithmetic and diffs are plain integer math; ISO-8601 parsing and formatting add what os.date alone does not, namely reading an ISO string with an optional Z or +hh:mm offset and normalizing it to UTC.
local datetime = {}

local Datetime = {}
Datetime.__index = Datetime

-- wraps a unix timestamp in a datetime object.
local function fromTimestamp(unix)
    return setmetatable({ timestamp = math.floor(unix) }, Datetime)
end

-- os.time interprets a table as local time, so building an instant from UTC fields needs the local/UTC skew subtracted back out. the skew is the difference between the same wall-clock fields read as local and as UTC.
local function utcToUnix(fields)
    local localGuess = os.time(fields)
    local utcTable = os.date("!*t", localGuess)
    local backToLocal = os.time(utcTable)
    local skew = os.difftime(localGuess, backToLocal)
    return localGuess + skew
end

-- now() -> a datetime for the current instant.
function datetime.now()
    return fromTimestamp(os.time())
end

-- fromUnix(seconds) -> a datetime at that unix timestamp.
function datetime.fromUnix(seconds)
    return fromTimestamp(seconds)
end

-- parses an ISO-8601 string into a datetime, accepting a bare date ("2026-06-21"), a date-time with a "T" or space separator, optional fractional seconds, and an optional trailing "Z" or "+hh:mm" / "-hh:mm" offset. a string with no offset is read as UTC. raises on anything it cannot parse.
function datetime.parse(text)
    if type(text) ~= "string" then
        error("[Datetime] parse expects a string, got " .. type(text), 2)
    end

    local year, month, day = text:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)")
    if not year then
        error("[Datetime] Not an ISO-8601 date: " .. text, 2)
    end

    local hour, minute, second = 0, 0, 0
    local rest = text:sub(11)
    if rest ~= "" then
        local h, m, s = rest:match("^[T ](%d%d):(%d%d):(%d%d)")
        if not h then
            -- a date with a time component must have hh:mm:ss.
            error("[Datetime] Not an ISO-8601 datetime: " .. text, 2)
        end
        hour, minute, second = h, m, s
    end

    local fields = {
        year = tonumber(year),
        month = tonumber(month),
        day = tonumber(day),
        hour = tonumber(hour),
        min = tonumber(minute),
        sec = tonumber(second),
        isdst = false,
    }

    local unix = utcToUnix(fields)

    -- apply a trailing offset by shifting to UTC; "Z" means no shift.
    local sign, offHour, offMin = text:match("([+-])(%d%d):?(%d%d)%s*$")
    if sign then
        local offsetSeconds = (tonumber(offHour) * 3600) + (tonumber(offMin) * 60)
        if sign == "+" then
            unix = unix - offsetSeconds
        else
            unix = unix + offsetSeconds
        end
    end

    return fromTimestamp(unix)
end

-- unix() -> the underlying unix timestamp in seconds.
function Datetime:unix()
    return self.timestamp
end

-- format(fmt) -> the instant rendered with an os.date format string, in UTC. defaults to ISO-8601.
function Datetime:format(fmt)
    return os.date("!" .. (fmt or "%Y-%m-%dT%H:%M:%SZ"), self.timestamp)
end

-- iso() -> the instant as an ISO-8601 UTC string with a trailing Z.
function Datetime:iso()
    return self:format("%Y-%m-%dT%H:%M:%SZ")
end

-- add(delta) -> a new datetime shifted by the given days/hours/minutes/seconds (any subset, may be negative); the receiver is unchanged.
function Datetime:add(delta)
    delta = delta or {}
    local seconds = (delta.days or 0) * 86400
        + (delta.hours or 0) * 3600
        + (delta.minutes or 0) * 60
        + (delta.seconds or 0)
    return fromTimestamp(self.timestamp + seconds)
end

-- diff(other) -> whole seconds from other to self (self - other), positive when self is later.
function Datetime:diff(other)
    return self.timestamp - other.timestamp
end

-- __tostring renders the ISO form so a datetime prints readably.
function Datetime.__tostring(self)
    return self:iso()
end

-- two instants are equal when they point at the same second.
function Datetime.__eq(a, b)
    return a.timestamp == b.timestamp
end

function Datetime.__lt(a, b)
    return a.timestamp < b.timestamp
end

function Datetime.__le(a, b)
    return a.timestamp <= b.timestamp
end

return datetime
