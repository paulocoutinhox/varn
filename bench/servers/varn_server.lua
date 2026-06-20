-- varn baseline server for the comparison benchmark: /plaintext and /json, no middleware.
-- scale across cores by running with VARN_WORKERS=N.
local http = require("http")

local port = tonumber(os.getenv("PORT") or "3000")

http.createServer(function(req, res)
    if req.path == "/plaintext" then
        res:setHeader("Content-Type", "text/plain")
        res:finish("Hello, World!")
    else
        res:json({ hello = "world" })
    end
end):listen({ host = "127.0.0.1", port = port, servePublic = false })
