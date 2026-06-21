-- varn server for the comparison benchmark. /plaintext and /json are framework-free baselines;
-- /db reads a random row through the async mysql client and /cache hits redis, both over pooled,
-- non-blocking connections so one process keeps many queries in flight. scale with VARN_WORKERS=N.
package.path = "components/?.lua;components/?/init.lua;" .. package.path

local http = require("http")
local mysql = require("mysql")
local redis = require("redis")
local pool = require("pool")

local port = tonumber(os.getenv("PORT") or "3000")
local poolSize = tonumber(os.getenv("POOL_SIZE") or "16")

local mysqlPool = pool.new({
    size = poolSize,
    connect = function()
        return mysql.connect({
            host = os.getenv("MYSQL_HOST") or "127.0.0.1",
            port = tonumber(os.getenv("MYSQL_PORT") or "3306"),
            user = os.getenv("MYSQL_USER") or "bench",
            password = os.getenv("MYSQL_PASSWORD") or "benchpass",
            database = os.getenv("MYSQL_DB") or "bench",
        })
    end,
})

local redisPool = pool.new({
    size = poolSize,
    connect = function()
        return redis.connect({
            host = os.getenv("REDIS_HOST") or "127.0.0.1",
            port = tonumber(os.getenv("REDIS_PORT") or "6379"),
        })
    end,
})

http.createServer(function(req, res)
    local path = req.path

    if path == "/plaintext" then
        res:setHeader("Content-Type", "text/plain")
        res:finish("Hello, World!")
        return
    end

    if path == "/json" then
        res:json({ hello = "world" })
        return
    end

    if path == "/db" then
        local id = math.random(1, 10000)
        local rows = mysqlPool:with(function(db)
            return db:query("SELECT randomNumber FROM world WHERE id = " .. id)
        end)
        res:json({ id = id, randomNumber = tonumber(rows[1].randomNumber) })
        return
    end

    if path == "/cache" then
        local hits = redisPool:with(function(client)
            return client:incr("bench:hits")
        end)
        res:json({ count = hits })
        return
    end

    res:status(404)
    res:finish("not found")
end):listen({ host = "127.0.0.1", port = port, servePublic = false })
