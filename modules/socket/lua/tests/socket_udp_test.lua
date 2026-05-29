-- socket udp: an in-process echo server round-trips a datagram back to a client.
local async = require("async")
local socket = require("socket")

local host = "127.0.0.1"
local serverPort = 9921
local clientPort = 9922

-- server: bind, receive one datagram, echo it back to the sender, then close.
async.spawn(function()
    local server, berr = socket.udp.bind(host, serverPort):await()
    assert(not berr, berr)

    local packet, rerr = server:recvFrom(4096):await()
    assert(not rerr, rerr)

    server:sendTo(packet.host, packet.port, "echo:" .. packet.data):await()

    server:close():await()
end)

-- client: bind on its own port, send to the server, then verify the echo.
async.run(function()
    async.sleep(50):await()

    local client, berr = socket.udp.bind(host, clientPort):await()
    assert(not berr, berr)

    client:sendTo(host, serverPort, "ping"):await()

    local reply, rerr = client:recvFrom(4096):await()
    assert(not rerr, rerr)
    assert(reply.data == "echo:ping", "unexpected reply: " .. tostring(reply.data))
    assert(reply.host == host, "unexpected echo origin host: " .. tostring(reply.host))
    assert(reply.port == serverPort, "unexpected echo origin port: " .. tostring(reply.port))

    client:close():await()

    print("socket udp ok")
end)
