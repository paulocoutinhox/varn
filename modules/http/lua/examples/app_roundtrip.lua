-- drives an in-process app with the client to show routing, body parsing and a json response, then exits.
-- the server runs on its own threads, so the script issues its requests inside async.run and finishes cleanly.
local async = require("async")
local http = require("http")

local port = 8091
local base = "http://127.0.0.1:" .. port

-- the client wire is "VARN/1 <status> <length>\n" followed by the raw body.
local function parseWire(wire)
    local nl = assert(wire:find("\n", 1, true), "missing header terminator")
    local status, len = wire:sub(1, nl - 1):match("^VARN/1 (%d+) (%d+)$")
    return tonumber(status), wire:sub(nl + 1, nl + tonumber(len))
end

local app = http.createApp()

-- a path parameter constrained to digits feeds a json response.
app:get("/users/:id", function(ctx)
    ctx:json({ id = ctx.params.id })
end):where("id", "int")

-- json body parsing detects the content type and hands back a parsed table.
app:post("/echo", function(ctx)
    ctx:json({ received = ctx:body() })
end)

app:listen({ host = "127.0.0.1", port = port })

async.run(function()
    local userStatus, userBody = parseWire(http.client.request({
        url = base .. "/users/7",
        method = "GET",
        headers = {},
        timeoutSeconds = 10,
    }):await())
    print("GET /users/7 -> " .. userStatus .. " " .. userBody)

    -- the client needs an explicit Content-Length so the server reads the posted body.
    local payload = '{"name":"varn"}'
    local echoStatus, echoBody = parseWire(http.client.request({
        url = base .. "/echo",
        method = "POST",
        headers = { ["Content-Type"] = "application/json", ["Content-Length"] = tostring(#payload) },
        body = payload,
        timeoutSeconds = 10,
    }):await())
    print("POST /echo -> " .. echoStatus .. " " .. echoBody)

    os.exit(0)
end)
