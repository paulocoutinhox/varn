# 🔌 socket

Async TCP and UDP sockets. Every operation returns a promise.

## TCP

- `socket.tcp.connect(host, port)` → promise resolving to a socket.
- `socket.tcp.listen(host, port, backlog?)` → promise resolving to a listener (`backlog` defaults to `64`).
- Socket: `sock:send(data)`, `sock:receive(maxBytes?)` (default `65536`), and `sock:close()` all return promises.
- Listener: `listener:accept()` (resolves to a socket) and `listener:close()`.

## UDP

- `socket.udp.bind(host, port)` → promise resolving to a UDP socket bound to the address.
- UDP socket: `sock:sendTo(host, port, data)`, `sock:recvFrom(maxBytes?)` (default `65536`), and `sock:close()` all return promises.
- `recvFrom` resolves to a table `{ data, host, port }` carrying the payload and the sender's address.

## Closing and limits

- `close()` is safe to call while a `receive`/`recvFrom`/`accept` is still pending. It signals the socket and returns promptly instead of blocking; the pending operation is released on its next internal poll (within ~200 ms).
- Once a socket is closed, a pending `receive`/`recvFrom` resolves to an empty/last result and a pending `accept` rejects with a "listener was closed" error.
- `receive` and `recvFrom` cap the read buffer (16 MB for TCP, 64 KB for UDP) regardless of the requested `maxBytes`, so an oversized request cannot drive a huge allocation.
- `host`/`port` are validated before use: the port must be in `1..65535` (the full integer is checked, not a truncated value).

## Examples

### `echo_server.lua`

```lua
-- echo service with an overridable listen port through the environment

local async = require("async")
local socket = require("socket")

local host = os.getenv("VARN_SOCKET_HOST") or "127.0.0.1"
local port = tonumber(os.getenv("VARN_SOCKET_PORT") or "9000")

local function handleClient(sock)
    while true do
        local chunk, err = sock:receive(4096):await()
        if err then
            print("client receive error:", err)
            break
        end
        if #chunk == 0 then
            break
        end
        local _, sendErr = sock:send("You sent: " .. chunk):await()
        if sendErr then
            print("client send error:", sendErr)
            break
        end
    end
    sock:close():await()
end

async.spawn(function()
    local listener, lerr = socket.tcp.listen(host, port, 128):await()
    if lerr then
        error(lerr)
    end
    print(string.format("tcp echo listening on %s:%d", host, port))
    while true do
        local client, aerr = listener:accept():await()
        if aerr then
            print("accept error:", aerr)
            break
        end
        async.spawn(function()
            handleClient(client)
        end)
    end
    listener:close():await()
end)
```

### `udp_echo.lua`

```lua
-- udp echo service with an overridable bind address through the environment

local async = require("async")
local socket = require("socket")

local host = os.getenv("VARN_SOCKET_HOST") or "127.0.0.1"
local port = tonumber(os.getenv("VARN_SOCKET_PORT") or "9000")

async.spawn(function()
    local sock, berr = socket.udp.bind(host, port):await()
    if berr then
        error(berr)
    end
    print(string.format("udp echo listening on %s:%d", host, port))
    while true do
        local packet, rerr = sock:recvFrom(4096):await()
        if rerr then
            print("recv error:", rerr)
            break
        end
        local _, serr = sock:sendTo(packet.host, packet.port, "You sent: " .. packet.data):await()
        if serr then
            print("send error:", serr)
            break
        end
    end
    sock:close():await()
end)
```
