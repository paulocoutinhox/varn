local async = require("async")

local function parseVarnWire(wire)
    local nl = wire:find("\n", 1, true)
    if not nl then
        error("http.client: missing header line terminator")
    end
    local head = wire:sub(1, nl - 1)
    local statusStr, lenStr = head:match("^VARN/1 (%d+) (%d+)$")
    if not statusStr then
        error("http.client: bad header line: " .. head)
    end
    local status = tonumber(statusStr)
    local bodyLen = tonumber(lenStr)
    local body = wire:sub(nl + 1, nl + bodyLen)
    if #body ~= bodyLen then
        error("http.client: body length mismatch")
    end
    return status, body
end

async.spawn(function()
    local http = require("http")
    local url = os.getenv("VARN_HTTP_URL") or "https://httpbin.org/get"
    local wire, err = http.client.requestRaw({
        url = url,
        method = "GET",
        headers = {},
        timeoutSeconds = 30
    }):await()
    if err then
        error(err)
    end
    local status, body = parseVarnWire(wire)
    print("status", status)
    print("body", body)
end)
