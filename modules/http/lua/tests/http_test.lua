-- http: an in-process server answers a route and a 404, exercised through the client.
local async = require("async")
local http = require("http")

local port = 3991

local function parseWire(wire)
    local nl = assert(wire:find("\n", 1, true), "missing header terminator")
    local status, len = wire:sub(1, nl - 1):match("^VARN/1 (%d+) (%d+)$")
    assert(status, "bad header line: " .. wire:sub(1, nl - 1))
    return tonumber(status), wire:sub(nl + 1, nl + tonumber(len))
end

http.createServer(function(req, res)
    if req.path == "/text" then
        res:finish("hello-http")
        return
    end
    res:status(404)
    res:finish("not found")
end):listen({ host = "127.0.0.1", port = port })

local function get(path)
    return http.client.request({
        url = "http://127.0.0.1:" .. port .. path,
        method = "GET",
        headers = {},
        timeoutSeconds = 10,
    }):await()
end

async.spawn(function()
    local ok, err = pcall(function()
        local wire, requestErr = get("/text")
        assert(not requestErr, requestErr)
        local status, body = parseWire(wire)
        assert(status == 200, "expected 200, got " .. status)
        assert(body == "hello-http", "unexpected body: " .. body)

        local missingWire, missingErr = get("/missing")
        assert(not missingErr, missingErr)
        assert(parseWire(missingWire) == 404, "expected 404")
    end)

    if not ok then
        print("http FAIL: " .. tostring(err))
        os.exit(1)
    end
    print("http ok")
    os.exit(0)
end)
