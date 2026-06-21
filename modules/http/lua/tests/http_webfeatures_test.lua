-- day-to-day web features: gzip compression, sse streaming, cache/etag helpers and accept negotiation, all through one in-process server.
local async = require("async")
local http = require("http")

local port = 39823
local base = "http://127.0.0.1:" .. port

-- splits the VARN/1 wire into status and raw body, the only framing the client returns.
local function parseWire(wire)
    local nl = assert(wire:find("\n", 1, true), "missing header terminator")
    local status, len = wire:sub(1, nl - 1):match("^VARN/1 (%d+) (%d+)$")
    assert(status, "bad header line: " .. wire:sub(1, nl - 1))
    return tonumber(status), wire:sub(nl + 1, nl + tonumber(len))
end

local function request(method, path, headers)
    return http.client.requestRaw({
        url = base .. path,
        method = method,
        headers = headers or {},
        timeoutSeconds = 10,
    }):await()
end

local function get(path, headers)
    return request("GET", path, headers)
end

local app = http.createApp()

-- a large json body crosses the compression threshold; the client sees raw bytes since it does not decode.
local big = {}
for i = 1, 400 do
    big[i] = { id = i, label = "item-number-" .. i }
end

app:get("/big", function(ctx) ctx:json({ items = big }) end)

-- a tiny body stays below the threshold and must never be compressed.
app:get("/tiny", function(ctx) ctx:json({ ok = true }) end)

-- an already-encoded body is left untouched even when large and gzip is accepted.
app:get("/preencoded", function(ctx)
    local payload = string.rep("text-payload-line\n", 200)
    ctx:header("Content-Encoding", "identity"):type("text/plain"):send(payload)
end)

-- sse streams a couple of events then closes so the buffering client receives a finite body.
app:get("/events", function(ctx)
    local stream = ctx:sse()
    stream:send("hello")
    stream:send("tick", "first")
    stream:send("multi\nline")
    stream:comment("heartbeat")
    stream:close()
end)

-- cache and etag helpers stamp dynamic responses.
app:get("/cached", function(ctx)
    ctx:cache({ maxAge = 120, private = true }):json({ ok = true })
end)

app:get("/tagged", function(ctx)
    ctx:etag("v1"):json({ ok = true })
end)

-- content negotiation picks html or json from the Accept header.
app:get("/negotiate", function(ctx)
    local best = ctx:accepts("html", "json")
    if best == "json" then
        ctx:json({ kind = "json" })
    else
        ctx:html("<p>html</p>")
    end
end)

app:listen({ host = "127.0.0.1", port = port })

async.run(function()
    -- a large json body with gzip accepted comes back gzip-framed and smaller than the plain body.
    local plainStatus, plainBody = parseWire(get("/big"))
    assert(plainStatus == 200, "plain big request failed")
    assert(plainBody:byte(1) ~= 0x1f, "plain body unexpectedly gzip-framed")

    local _, gzBody = parseWire(get("/big", { ["Accept-Encoding"] = "gzip" }))
    assert(gzBody:byte(1) == 0x1f and gzBody:byte(2) == 0x8b, "gzip magic bytes missing")
    assert(#gzBody < #plainBody, "gzip body not smaller than plain body")

    -- a tiny body stays uncompressed even when gzip is accepted.
    local _, tinyBody = parseWire(get("/tiny", { ["Accept-Encoding"] = "gzip" }))
    assert(tinyBody:byte(1) ~= 0x1f, "tiny body should not be gzipped")

    -- an already-encoded body is never recompressed.
    local _, preBody = parseWire(get("/preencoded", { ["Accept-Encoding"] = "gzip" }))
    assert(preBody:byte(1) ~= 0x1f, "pre-encoded body recompressed")

    -- the sse stream carries event, data and comment lines with correct framing.
    local sseStatus, sseBody = parseWire(get("/events"))
    assert(sseStatus == 200, "sse request failed")
    assert(sseBody:find("data: hello\n\n", 1, true), "sse default-event message missing")
    assert(sseBody:find("event: tick\ndata: first\n\n", 1, true), "sse named event missing")
    assert(sseBody:find("data: multi\ndata: line\n\n", 1, true), "sse multi-line data not split")
    assert(sseBody:find(": heartbeat\n\n", 1, true), "sse comment heartbeat missing")

    -- the cache helper composes the directive string.
    local cacheStatus, cacheBody = parseWire(get("/cached"))
    assert(cacheStatus == 200 and cacheBody:find("ok", 1, true), "cache route failed")

    -- the etag helper answers a fresh request and short-circuits a matching If-None-Match to 304.
    assert(parseWire(get("/tagged")) == 200, "etag fresh request failed")
    assert(parseWire(get("/tagged", { ["If-None-Match"] = '"v1"' })) == 304, "etag did not honor If-None-Match")

    -- content negotiation returns json or html based on Accept.
    local _, negJson = parseWire(get("/negotiate", { ["Accept"] = "application/json" }))
    assert(negJson:find("json", 1, true), "negotiation did not pick json")
    local _, negHtml = parseWire(get("/negotiate", { ["Accept"] = "text/html" }))
    assert(negHtml:find("html", 1, true), "negotiation did not pick html")

    print("http web features ok")
end)
