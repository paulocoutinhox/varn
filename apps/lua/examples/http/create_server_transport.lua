-- introspects whether listen entrypoints are active or stubbed
local http = require("http")

local ok, serverOrErr = pcall(function()
    return http.createServer(function(_, res)
        res:finish("ok")
    end)
end)

if ok then
    print("http.createServer available (full HTTP server transport).")
    os.exit(0)
end

local msg = tostring(serverOrErr)
assert(msg:find("not available") or msg:find("transport"), "unexpected error: " .. msg)
print("expected: http server stub —", msg)
os.exit(0)
