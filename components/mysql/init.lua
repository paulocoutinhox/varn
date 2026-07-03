-- async mysql client speaking the text protocol (COM_QUERY) over the native socket and authenticating with caching_sha2_password (full auth via the server rsa public key) or mysql_native_password, where every socket operation yields on the event loop so one process keeps many queries in flight instead of blocking on each, running inside an async coroutine (async.spawn/async.run) and pairing with the pool component to share connections
local socket = require("socket")
local crypto = require("crypto")

local mysql = {}

local CLIENT_LONG_PASSWORD = 0x00000001
local CLIENT_CONNECT_WITH_DB = 0x00000008
local CLIENT_PROTOCOL_41 = 0x00000200
local CLIENT_SSL = 0x00000800
local CLIENT_TRANSACTIONS = 0x00002000
local CLIENT_SECURE_CONNECTION = 0x00008000
local CLIENT_PLUGIN_AUTH = 0x00080000

local CAPABILITIES = CLIENT_LONG_PASSWORD | CLIENT_CONNECT_WITH_DB | CLIENT_PROTOCOL_41
    | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH

local READ_CHUNK = 65536

local COM_QUIT = 0x01
local COM_QUERY = 0x03

-- buffered byte reader over the socket where socket:receive yields up to n bytes so packet framing needs exact-count reads
local Reader = {}
Reader.__index = Reader

function Reader.new(sock)
    return setmetatable({ sock = sock, buffer = "", cursor = 1 }, Reader)
end

function Reader:pull()
    local chunk, err = self.sock:receive(READ_CHUNK):await()
    if err then
        error("[Mysql] Receive failed: " .. tostring(err))
    end
    if chunk == "" then
        error("[Mysql] Connection closed by server.")
    end

    if self.cursor > 1 then
        self.buffer = self.buffer:sub(self.cursor)
        self.cursor = 1
    end
    self.buffer = self.buffer .. chunk
end

function Reader:bytes(count)
    while (#self.buffer - self.cursor + 1) < count do
        self:pull()
    end

    local data = self.buffer:sub(self.cursor, self.cursor + count - 1)
    self.cursor = self.cursor + count
    return data
end

-- a length-encoded integer whose marker byte selects an inline value or a 2/3/8-byte little-endian tail
local function lenencInt(s, pos)
    local first = string.byte(s, pos)
    if first < 0xFB then
        return first, pos + 1
    end
    if first == 0xFB then
        return nil, pos + 1
    end
    if first == 0xFC then
        return string.unpack("<I2", s, pos + 1)
    end
    if first == 0xFD then
        return string.unpack("<I3", s, pos + 1)
    end

    return string.unpack("<I8", s, pos + 1)
end

local function lenencStr(s, pos)
    local length, next = lenencInt(s, pos)
    if length == nil then
        return nil, next
    end

    return s:sub(next, next + length - 1), next + length
end

local function errorMessage(payload, label)
    local code = string.unpack("<I2", payload, 2)
    local messageStart = 4
    if string.byte(payload, 4) == 35 then
        messageStart = 10
    end

    return error("[Mysql] " .. label .. " " .. code .. ": " .. payload:sub(messageStart))
end

local function nativePassword(password, scramble)
    if password == "" then
        return ""
    end

    local stage1 = crypto.digest("SHA1", password, "raw")
    local stage2 = crypto.digest("SHA1", stage1, "raw")
    local token = crypto.digest("SHA1", scramble .. stage2, "raw")

    local out = {}
    for i = 1, 20 do
        out[i] = string.char(string.byte(stage1, i) ~ string.byte(token, i))
    end

    return table.concat(out)
end

-- caching_sha2 scramble is sha256(password) xored with sha256(sha256(sha256(password)) .. nonce)
local function cachingSha2Password(password, scramble)
    if password == "" then
        return ""
    end

    local d1 = crypto.digest("SHA256", password, "raw")
    local d2 = crypto.digest("SHA256", d1, "raw")
    local d3 = crypto.digest("SHA256", d2 .. scramble, "raw")

    local out = {}
    for i = 1, 32 do
        out[i] = string.char(string.byte(d1, i) ~ string.byte(d3, i))
    end

    return table.concat(out)
end

-- xors the nul-terminated password with the repeating nonce for the caching_sha2 full-auth path
local function xorPassword(password, scramble)
    local pw = password .. "\0"
    local out = {}
    for i = 1, #pw do
        local s = string.byte(scramble, ((i - 1) % #scramble) + 1)
        out[i] = string.char(string.byte(pw, i) ~ s)
    end

    return table.concat(out)
end

-- builds the initial auth response for whichever plugin the server offers
local function authResponseFor(plugin, password, scramble)
    if plugin == "caching_sha2_password" then
        return cachingSha2Password(password, scramble)
    end

    return nativePassword(password, scramble)
end

local Client = {}
Client.__index = Client

function Client:readPacket()
    local header = self.reader:bytes(4)
    local length = string.unpack("<I3", header)
    self.lastSeq = string.byte(header, 4)
    return self.reader:bytes(length)
end

function Client:sendPacket(payload, seq)
    local frame = string.pack("<I3", #payload) .. string.char(seq) .. payload
    local _, err = self.sock:send(frame):await()
    if err then
        error("[Mysql] Send failed: " .. tostring(err))
    end
end

function Client:handshake(options)
    local payload = self:readPacket()

    local pos = 2
    local versionEnd = payload:find("\0", pos, true)
    pos = versionEnd + 1
    pos = pos + 4

    local part1 = payload:sub(pos, pos + 7)
    pos = pos + 8 + 1 + 2 + 1 + 2 + 2

    local authLen = string.byte(payload, pos)
    pos = pos + 1 + 10

    local part2Len = math.max(13, authLen - 8)
    local part2 = payload:sub(pos, pos + part2Len - 1)
    local scramble = part1 .. part2:sub(1, 12)

    -- the auth plugin name follows the auth data and drives which scramble the server expects
    pos = pos + part2Len
    local pluginEnd = payload:find("\0", pos, true)
    local serverPlugin = pluginEnd and payload:sub(pos, pluginEnd - 1) or "mysql_native_password"
    local plugin = serverPlugin == "caching_sha2_password" and "caching_sha2_password" or "mysql_native_password"

    local user = options.user or "root"
    local database = options.database or ""
    local authResponse = authResponseFor(plugin, options.password or "", scramble)

    -- advertise ssl in the handshake capabilities when the caller asked for tls
    local capabilities = options.tls and (CAPABILITIES | CLIENT_SSL) or CAPABILITIES
    local preamble = string.pack("<I4", capabilities)
        .. string.pack("<I4", 16777216)
        .. string.char(33)
        .. string.rep("\0", 23)

    local response = preamble
        .. user .. "\0"
        .. string.char(#authResponse) .. authResponse
        .. database .. "\0"
        .. plugin .. "\0"

    if options.tls then
        -- send the ssl request preamble, upgrade the raw socket to tls, then send the real handshake response over it
        self:sendPacket(preamble, 1)
        local _, tlsError = self.sock:startTls(self.host, { insecure = options.insecure }):await()
        if tlsError then
            error("[Mysql] TLS negotiation failed: " .. tostring(tlsError))
        end

        self.tls = true
        self:sendPacket(response, 2)
    else
        self:sendPacket(response, 1)
    end

    self:finishAuth(options, plugin, scramble)
end

function Client:finishAuth(options, plugin, scramble)
    while true do
        local result = self:readPacket()
        local marker = string.byte(result, 1)

        if marker == 0x00 then
            return
        end

        if marker == 0xFF then
            errorMessage(result, "Auth failed")
        end

        if marker == 0xFE then
            -- the server asks to switch plugins and re-scramble with the fresh nonce it sends here
            local nameEnd = result:find("\0", 2, true)
            plugin = result:sub(2, nameEnd - 1)
            scramble = result:sub(nameEnd + 1):sub(1, 20)
            self:sendPacket(authResponseFor(plugin, options.password or "", scramble), self.lastSeq + 1)
        elseif marker == 0x01 then
            -- caching_sha2 auth-more-data selects fast (0x03) or full (0x04) authentication
            local status = string.byte(result, 2)
            if status == 0x04 then
                if self.tls then
                    -- over tls the password is safe to send in the clear, nul-terminated
                    self:sendPacket((options.password or "") .. "\0", self.lastSeq + 1)
                else
                    -- full auth over a plaintext socket requests the server rsa public key, then sends the password xored with the nonce and rsa-encrypted
                    self:sendPacket(string.char(0x02), self.lastSeq + 1)
                    local keyPacket = self:readPacket()
                    local pem = keyPacket:sub(2)
                    self:sendPacket(crypto.rsaEncryptPublic(pem, xorPassword(options.password or "", scramble)), self.lastSeq + 1)
                end
            elseif status ~= 0x03 then
                error("[Mysql] Unexpected caching_sha2 auth status.")
            end
        else
            error("[Mysql] Unexpected auth result byte.")
        end
    end
end

-- query(sql) -> an array of row tables keyed by column name where all values come back as strings matching the text protocol so convert numerics at the call site when needed
function Client:query(sql)
    self:sendPacket(string.char(COM_QUERY) .. sql, 0)

    local first = self:readPacket()
    local marker = string.byte(first, 1)
    if marker == 0x00 then
        return {}
    end
    if marker == 0xFF then
        errorMessage(first, "Query failed")
    end

    local columnCount = lenencInt(first, 1)

    local columns = {}
    for i = 1, columnCount do
        local definition = self:readPacket()
        local pos = 1
        for _ = 1, 4 do
            pos = select(2, lenencStr(definition, pos))
        end
        columns[i] = (lenencStr(definition, pos))
    end

    -- the eof packet that closes the column definitions (no CLIENT_DEPRECATE_EOF negotiated)
    self:readPacket()

    local rows = {}
    while true do
        local packet = self:readPacket()
        local head = string.byte(packet, 1)
        if head == 0xFE and #packet < 9 then
            break
        end
        if head == 0xFF then
            errorMessage(packet, "Result failed")
        end

        local row = {}
        local pos = 1
        for i = 1, columnCount do
            local value
            value, pos = lenencStr(packet, pos)
            row[columns[i]] = value
        end
        rows[#rows + 1] = row
    end

    return rows
end

function Client:close()
    pcall(function()
        self:sendPacket(string.char(COM_QUIT), 0)
    end)
    pcall(function()
        self.sock:close():await()
    end)
end

-- connect({ host, port, user, password, database }) -> a ready client, authenticated and database-selected
function mysql.connect(options)
    options = options or {}

    local sock, err = socket.tcp.connect(options.host or "127.0.0.1", options.port or 3306):await()
    if err then
        error("[Mysql] Connect failed: " .. tostring(err))
    end

    local self = setmetatable({ sock = sock, reader = Reader.new(sock), host = options.host or "127.0.0.1", tls = false }, Client)
    self:handshake(options)
    return self
end

return mysql
