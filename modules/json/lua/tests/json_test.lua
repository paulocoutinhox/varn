-- json: res:json serializes a lua table; the client confirms the fields on the wire.
-- json has no standalone lua module, so it is validated through the http response path.
-- the request targets a non-static path so the static file handler does not intercept it.
local async = require("async")
local http = require("http")

local port = 3992

local function body(wire)
    local nl = assert(wire:find("\n", 1, true), "missing header terminator")
    local status, len = wire:sub(1, nl - 1):match("^VARN/1 (%d+) (%d+)$")
    assert(tonumber(status) == 200, "expected 200")
    return wire:sub(nl + 1, nl + tonumber(len))
end

http.createServer(function(_, res)
    res:json({ ok = true, name = "varn", count = 42 })
end):listen({ host = "127.0.0.1", port = port })

async.spawn(function()
    local ok, err = pcall(function()
        local wire, requestErr = http.client.request({
            url = "http://127.0.0.1:" .. port .. "/api/data",
            method = "GET",
            headers = {},
            timeoutSeconds = 10,
        }):await()
        assert(not requestErr, requestErr)

        local payload = body(wire)
        assert(payload:find('"name"', 1, true) and payload:find("varn", 1, true), "name field")
        assert(payload:find('"count"', 1, true) and payload:find("42", 1, true), "count field")
        assert(payload:find("true", 1, true), "ok field")
    end)

    if not ok then
        print("json FAIL: " .. tostring(err))
        os.exit(1)
    end
    print("json ok")
    os.exit(0)
end)
