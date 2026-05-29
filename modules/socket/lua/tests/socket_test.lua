-- socket: an in-process tcp echo server round-trips a payload back to a client.
local async = require("async")
local socket = require("socket")

local host = "127.0.0.1"
local port = 9911

-- server: accept one client, echo a prefixed message, then shut down.
async.spawn(function()
    local listener, lerr = socket.tcp.listen(host, port, 16):await()
    assert(not lerr, lerr)

    local client, aerr = listener:accept():await()
    assert(not aerr, aerr)

    local chunk, rerr = client:receive(4096):await()
    assert(not rerr, rerr)
    client:send("echo:" .. chunk):await()

    client:close():await()
    listener:close():await()
end)

-- client: connect once the listener is bound, send, and verify the echo.
async.spawn(function()
    local ok, err = pcall(function()
        async.sleep(50):await()

        local conn, cerr = socket.tcp.connect(host, port):await()
        assert(not cerr, cerr)

        conn:send("ping"):await()
        local reply, rerr = conn:receive(4096):await()
        assert(not rerr, rerr)
        assert(reply == "echo:ping", "unexpected reply: " .. tostring(reply))

        conn:close():await()
    end)

    if not ok then
        print("socket FAIL: " .. tostring(err))
        os.exit(1)
    end
    print("socket ok")
    os.exit(0)
end)
