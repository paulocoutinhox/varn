# Lua API reference

Every built-in module is available with `require`. The modules are: `http`, `socket`,
`async`, `fs`, `crypto`, `zip`, `ffi`, `platform`, and `log`.

Runnable examples live next to each module under `modules/<module>/lua/examples/` (run them
from the repository root). Some modules depend on the backend chosen at build time; if a
module's backend is `DUMMY`, the module still loads but its functions return an error when
used. See [build.md](build.md).

## http

### Server

```lua
local http = require("http")

http.createServer(function(req, res)
    res:json({ hello = req.query.name or "world" })
end):listen(3000)
```

`http.createServer(handler)` returns a builder. The handler runs as a coroutine for each
request with `(req, res)`.

`req` fields: `host`, `method`, `path`, `target`, `queryString`, `body`, `remoteAddress`,
`headers`, `cookies`, and `query` (the parsed query string as a table).

`res` methods:

- `res:status(code)` — set the HTTP status.
- `res:setHeader(name, value)` — set a header.
- `res:finish(body?)` — send an optional body and end the response.
- `res:json(table)` — send a table as JSON.
- `res:xml(table)` — send a table as XML.

If the handler returns without ending the response, the server sends `204 No Content`.

`builder:listen(port)` or `builder:listen(options)` starts the server. Options: `host`,
`port`, `publicDir`, `servePublic`, `tls`, `certFile`, `keyFile`. The environment variables
`VARN_PORT`, `VARN_TLS_CERT`, and `VARN_TLS_KEY` override the matching options. When
`servePublic` is on and `publicDir` is omitted, it defaults to `apps/lua/public`.

### Client

```lua
local wire = http.client.request({ url = "https://example.com" }):await()
```

`http.client.request(options)` returns a promise. Options: `url` (required), `method`
(default `"GET"`), `headers` (table), `body` (string), `timeoutSeconds` (default `60`).

On success it resolves to a wire string: `VARN/1 <status> <length>\n` followed by the raw
body. See `modules/http/lua/examples/client_request.lua` for a parser. On failure it rejects
with a message.

## socket

```lua
local socket = require("socket")
local async = require("async")

async.spawn(function()
    local conn = socket.tcp.connect("127.0.0.1", 9000):await()
    conn:send("hello"):await()
    local reply = conn:receive():await()
    conn:close():await()
end)
```

- `socket.tcp.connect(host, port)` → promise resolving to a socket.
- `socket.tcp.listen(host, port, backlog?)` → promise resolving to a listener (`backlog`
  defaults to `64`).
- Socket: `sock:send(data)`, `sock:receive(maxBytes?)` (default `65536`), and `sock:close()`
  all return promises.
- Listener: `listener:accept()` (resolves to a socket) and `listener:close()`.

```lua
async.spawn(function()
    local sock = socket.udp.bind("127.0.0.1", 9000):await()
    sock:sendTo("127.0.0.1", 9001, "ping"):await()
    local packet = sock:recvFrom():await()
    print(packet.data, packet.host, packet.port)
    sock:close():await()
end)
```

- `socket.udp.bind(host, port)` → promise resolving to a UDP socket bound to the address.
- UDP socket: `sock:sendTo(host, port, data)`, `sock:recvFrom(maxBytes?)` (default `65536`),
  and `sock:close()` all return promises.
- `recvFrom` resolves to a table `{ data, host, port }` carrying the payload and the
  sender's address.

## async

- `async.sleep(ms)` → a promise that resolves after `ms` milliseconds.
- `async.spawn(fn)` → run `fn` in a new coroutine. Use it to call `:await()` outside an HTTP
  handler.

See [async.md](async.md).

## fs

- `fs.readFile(path)` → promise resolving to the file contents (binary-safe).
- `fs.writeFile(path, content)` → promise resolving to `"ok"`.
- `fs.exists(path)` → boolean, checked synchronously.

## crypto

- `crypto.digest(algorithm, data, format?)` → a digest of `data`. `algorithm` is a name like
  `"SHA256"` or `"SHA512"`. `format` is `"hex"` (default) or `"raw"`.
- `crypto.hmac(algorithm, key, data, format?)` → an HMAC, with the same `format` options.
- `crypto.randomBytes(n)` → `n` random bytes as a string.

## zip

All three return promises (the work runs on the task pool).

- `zip.create(archivePath, entries)` — `entries` is an array of `{ file = "...", entry = "..." }`.
- `zip.extract(archivePath, destDir)` — extracts under `destDir` and rejects unsafe paths.
- `zip.list(archivePath)` → resolves to an array of entry names.

## ffi

A LuaJIT-style FFI (`ffi.cdef`, `ffi.new`, `ffi.C`, `ffi.load`, …). Declare C functions, then
call them:

```lua
local ffi = require("ffi")
ffi.cdef [[ unsigned long strlen(const char *s); ]]
print(ffi.C.strlen("hello")) -- 5
```

More in `modules/ffi/lua/examples/`.

## platform

- `platform.os()` → e.g. `"linux"`, `"macos"`, `"windows"`, `"ios"`, `"android"`, `"wasm"`.
- `platform.arch()` → e.g. `"arm64"`, `"x86_64"`, `"wasm32"`.
- `platform.hostVersion()` → the Varn version string.
- `platform.cpuCount()` → the number of CPUs.
- `platform.pointerSize()` → `4` or `8`.
- `platform.endianness()` → `"little"` or `"big"`.
- `platform.libPrefix()` and `platform.shlibSuffix()` → the shared-library naming pieces.
- `platform.libraryFilename(name)` → e.g. `libz.so` for `"z"`.
- `platform.getLibraryPathByName(name, subdir?)` → builds `subdir/filename` (a dev helper).

## log

`log.debug(...)`, `log.info(...)`, `log.warn(...)`, and `log.error(...)`. Each takes any
number of values, like `print`: they are converted with `tostring`, joined by tabs, and
written as one line with the level tag.

## Promises

Async functions return a promise:

- `promise:await()` — pauses the current coroutine until the promise settles, then returns
  the value (or `nil, err` on failure).
- `promise:isDone()` → boolean. Treat it as a hint, not as synchronization.

## The `arg` global

The host sets `arg` to the command-line arguments (the script path and its parameters), like
standard Lua. In the browser build it is usually empty.

## WebAssembly notes

In the browser build (`varn_wasm`) there is no HTTP server or raw socket, `crypto` and `ffi`
are stubs, and `http.client` uses the browser `fetch()`. JSON, XML, logging, `fs`, and
`async` behave the same. See [build.md](build.md#webassembly).
