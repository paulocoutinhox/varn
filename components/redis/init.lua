-- redis client built on top of the native socket module, speaking RESP2 over a single connection.
-- every call yields on the event loop, so it must run inside an async coroutine (async.spawn/async.run).
local async = require("async")
local socket = require("socket")

local READ_CHUNK = 4096

-- a server error reply is represented as a value instead of raised mid-parse, so an error nested in an
-- array does not desync the stream. command() raises only when the top-level reply is an error.
local function makeServerError(message)
    return setmetatable({ message = message }, { __index = { isRedisError = true } })
end

local function isServerError(value)
    return type(value) == "table" and getmetatable(value) ~= nil and value.isRedisError == true
end

-- buffered reader over a socket: socket:receive returns up to n bytes, so RESP framing needs its own
-- buffer to serve exact byte counts and crlf-terminated lines.
local Reader = {}
Reader.__index = Reader

function Reader.new(sock)
    return setmetatable({ sock = sock, buffer = "", cursor = 1 }, Reader)
end

function Reader:pull()
    local chunk, err = self.sock:receive(READ_CHUNK):await()
    if err then
        error("[Redis] receive failed: " .. tostring(err))
    end
    if chunk == "" then
        error("[Redis] connection closed by server")
    end

    -- drop the already-consumed prefix before appending so the buffer stays bounded.
    if self.cursor > 1 then
        self.buffer = self.buffer:sub(self.cursor)
        self.cursor = 1
    end
    self.buffer = self.buffer .. chunk
end

function Reader:line()
    while true do
        local stop = self.buffer:find("\r\n", self.cursor, true)
        if stop then
            local line = self.buffer:sub(self.cursor, stop - 1)
            self.cursor = stop + 2
            return line
        end
        self:pull()
    end
end

function Reader:bytes(count)
    while (#self.buffer - self.cursor + 1) < count do
        self:pull()
    end

    local data = self.buffer:sub(self.cursor, self.cursor + count - 1)
    self.cursor = self.cursor + count
    return data
end

function Reader:reply()
    local line = self:line()
    local kind = line:sub(1, 1)
    local rest = line:sub(2)

    if kind == "+" then
        return rest
    end
    if kind == "-" then
        return makeServerError(rest)
    end
    if kind == ":" then
        return tonumber(rest)
    end

    if kind == "$" then
        local length = tonumber(rest)
        if length == -1 then
            return nil
        end
        local data = self:bytes(length)
        self:line()
        return data
    end

    if kind == "*" then
        local count = tonumber(rest)
        if count == -1 then
            return nil
        end
        local items = {}
        for i = 1, count do
            items[i] = self:reply()
        end
        return items
    end

    error("[Redis] unexpected reply type: " .. tostring(line))
end

-- real methods live in a separate table so __index can fall through to dynamic command dispatch.
local methods = {}

local Client = {}
Client.__index = function(_, key)
    local method = methods[key]
    if method then
        return method
    end

    -- any other access becomes a redis command named after the key (redis names are case-insensitive).
    return function(self, ...)
        return self:command(key, ...)
    end
end

local function encodeArgument(value)
    local kind = type(value)
    if kind == "string" then
        return value
    end
    if kind == "number" then
        return tostring(value)
    end
    error("[Redis] command arguments must be string or number, got " .. kind)
end

-- encodes one command as a RESP array of bulk strings.
local function encodeCommand(...)
    local args = table.pack(...)
    if args.n == 0 then
        error("[Redis] command requires at least the command name")
    end

    local parts = { "*" .. args.n .. "\r\n" }
    for i = 1, args.n do
        local arg = encodeArgument(args[i])
        parts[#parts + 1] = "$" .. #arg .. "\r\n" .. arg .. "\r\n"
    end
    return table.concat(parts)
end

function methods:command(...)
    local _, err = self.sock:send(encodeCommand(...)):await()
    if err then
        error("[Redis] send failed: " .. tostring(err))
    end

    local reply = self.reader:reply()
    if isServerError(reply) then
        error("[Redis] " .. reply.message)
    end
    return reply
end

-- a pipeline records commands without sending, then flushes them in one write and reads every reply
-- in order. it uses the same dynamic dispatch, so p:set(...), p:get(...) queue their commands.
local pipelineMethods = {}

local Pipeline = {}
Pipeline.__index = function(_, key)
    local method = pipelineMethods[key]
    if method then
        return method
    end

    return function(self, ...)
        return self:command(key, ...)
    end
end

function pipelineMethods:command(...)
    self.frames[#self.frames + 1] = encodeCommand(...)
    self.count = self.count + 1
end

function methods:pipeline(builder)
    local batch = setmetatable({ frames = {}, count = 0 }, Pipeline)
    builder(batch)

    if batch.count == 0 then
        return {}
    end

    local _, err = self.sock:send(table.concat(batch.frames)):await()
    if err then
        error("[Redis] pipeline send failed: " .. tostring(err))
    end

    -- read every reply before raising so a single server error cannot desync the stream.
    local replies = {}
    local firstError
    for i = 1, batch.count do
        local reply = self.reader:reply()
        if isServerError(reply) then
            replies[i] = nil
            firstError = firstError or reply.message
        else
            replies[i] = reply
        end
    end

    if firstError then
        error("[Redis] " .. firstError)
    end
    return replies
end

function methods:close()
    if not self.sock then
        return
    end
    self.sock:close():await()
    self.sock = nil
end

local redis = {}

function redis.connect(options)
    options = options or {}

    local host = options.host or "127.0.0.1"
    local port = options.port or 6379

    local sock, err = socket.tcp.connect(host, port):await()
    if err then
        error("[Redis] connect failed: " .. tostring(err))
    end

    local client = setmetatable({ sock = sock, reader = Reader.new(sock) }, Client)

    -- authenticate before anything else so a wrong credential fails fast.
    if options.username then
        client:command("AUTH", options.username, options.password or "")
    elseif options.password then
        client:command("AUTH", options.password)
    end

    if options.db then
        client:command("SELECT", options.db)
    end

    return client
end

return redis
