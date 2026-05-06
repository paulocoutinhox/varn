# Lua API reference

Built-in modules are preloaded by the **desktop** host (`varn` binary) and by the **WebAssembly** host (`varn_wasm`, loaded from **`apps/wasm`**). Names follow a small Node-like set: **`http`**, **`socket`**, **`ffi`**, **`async`**, **`fs`**, **`log`**, **`crypto`**, **`platform`**, **`zip`**.

Runnable examples are grouped by module under **`apps/lua/examples/`**; see **[apps/lua/examples/README.md](../apps/lua/examples/README.md)** (run with **`cwd`** at the repository root unless a script notes otherwise).

Which methods exist on **`http`** responses (for example `res:json`) depends on **driver ids** selected at CMake configure time; see [build.md](build.md). The **`http.client`** surface depends on **`VARN_HTTP_CLIENT_DRIVER`**. **`socket`** depends on **`VARN_SOCKET_DRIVER`**. **`ffi`** depends on **`VARN_FFI_DRIVER`**. Vendor libraries are listed in **[official-libraries.md](official-libraries.md)**.

### WebAssembly (`varn_wasm`)

Emscripten **`CMakeLists.txt`** forces **`DUMMY`** only where the browser cannot match the desktop stack: **`VARN_HTTP_SERVER_DRIVER`** (no in-page **`http.createServer`**), **`VARN_SOCKET_DRIVER`**, **`VARN_CRYPTO_DRIVER`**, and **`VARN_FFI_DRIVER`**. It sets **`VARN_HTTP_CLIENT_DRIVER=EMSCRIPTEN_FETCH`** (non-blocking browser **`fetch()`** via **`EM_JS`**, same **`Promise`** / **`VARN/1`** wire as desktop). **`log`**, JSON, and XML use the same **vendor** paths as a typical native configure (**`SPDLOG`**, **`NLOHMANN`**, **`PUGIXML`**); **`fs`** is **`STD`** (MEMFS / normal I/O in the worker). Because there is no HTTP server on WASM, **`res:json` / `res:xml`** do not appear in practice (no **`http.createServer`** handler path). **`async.sleep`** uses **`emscripten_sleep`** inside a **queued** task-pool job; **`Promise`** and **`EventLoop`** draining are described in [async.md](async.md#emscripten-varn_wasm). Cooperative stop and **`print`** capture: **`src/varn/wasm/VarnWasm.cpp`**.

## `http`

### When the stack is fully enabled

If your build selected a **full HTTP transport** and matching serializers, `http.createServer` runs a real server.

#### `http.createServer(handler) -> builder`

- `handler` is a Lua function invoked as a **coroutine** with `(req, res)` for each request.
- **Request (`req`)** — table fields: `host`, `method`, `path`, `target`, `queryString`, `body`, `remoteAddress`, `headers`, `cookies`, `query` (string map for query parameters).
- **Response (`res`)** — userdata with methods:
  - **`res:status(code)`** — set HTTP status.
  - **`res:setHeader(name, value)`** — set a response header.
  - **`res:finish(body?)`** — send optional body and end the response.
  - **`res:json(table)`** — present with **`VARN_HTTP_SERVER_DRIVER=POCO`** and **`VARN_JSON_DRIVER=NLOHMANN`**; serializes a **flat** table and sets a JSON `Content-Type`.
  - **`res:xml(table)`** — present with **`VARN_HTTP_SERVER_DRIVER=POCO`** and **`VARN_XML_DRIVER=PUGIXML`**; serializes a **flat** table to XML.

If the handler returns without ending the response, the server may send **204 No Content** when the coroutine completes successfully.

#### `builder:listen(port)` / `builder:listen(options)`

- Integer port, or a table: `host`, `port`, `publicDir`, `servePublic`, `tls`, `certFile`, `keyFile`.
- If `publicDir` is omitted, the native host default is **`apps/lua/public`** (relative to the process **current working directory**), matching the sample layout under **`apps/lua/`**.
- Environment overrides: `VARN_PORT`, `VARN_TLS_CERT`, `VARN_TLS_KEY`.

### `http.client` (outbound HTTP)

Always registered on the **`http`** table. Behavior is selected by **`VARN_HTTP_CLIENT_DRIVER`** (see [build.md](build.md)).

#### `http.client.request(options) -> Promise`

- `options` is a table: **`url`** (string, required), **`method`** (string, default `"GET"`), **`headers`** (optional string→string map), **`body`** (optional string), **`timeoutSeconds`** (optional integer, default `60`).
- On success the promise resolves to a single **wire string**: `VARN/1 <status> <bodyByteLength>\n` immediately followed by the raw response body (binary-safe framing). Parse it in Lua or see **`apps/lua/examples/http/client_request.lua`** (or the root forwarder **`apps/lua/examples/http_client_request.lua`**).
- On failure the promise rejects with a string message.

With **`VARN_HTTP_CLIENT_DRIVER=POCO`**, the request runs on the **task pool** and settles via the **`EventLoop`**. With **`EMSCRIPTEN_FETCH`** (WASM), **`fetch()`** runs in the browser worker; completion still goes through the same **`Promise`** / **`EventLoop::post`** path after the chunk host drains queued work (see [async.md](async.md#emscripten-varn_wasm)).

### Stub / disabled server transport

The **`http`** module is always registered. If **`VARN_HTTP_SERVER_DRIVER`** is not the full POCO server, `http.createServer` **errors at call time** with a message that includes the active server driver id. **`http.client`** is independent; see **`VARN_HTTP_CLIENT_DRIVER`** in [build.md](build.md).

## `socket`

Registered when the native host loads. **`VARN_SOCKET_DRIVER`** selects the implementation (see [build.md](build.md)).

### `socket.tcp.connect(host, port) -> Promise`

Resolves to a **TCP socket userdata** with methods below. Rejects if the connect fails.

### `socket.tcp.listen(host, port, backlog?) -> Promise`

`backlog` defaults to `64`. Resolves to a **TCP listener userdata** with methods below.

### Socket methods

- **`sock:send(data) -> Promise`** — `data` is a string; resolves to `"ok"` on success.
- **`sock:receive(maxBytes?) -> Promise`** — `maxBytes` defaults to `65536`; resolves to a string (possibly empty when the peer closed cleanly).
- **`sock:close() -> Promise`** — shuts down the connection; resolves to `"ok"` on success.

### Listener methods

- **`listener:accept() -> Promise`** — resolves to a new **socket userdata** for an accepted client.
- **`listener:close() -> Promise`** — closes the listening socket.

On **native** builds with **`VARN_SOCKET_DRIVER=POCO`**, blocking socket work runs on the runtime **task pool**; use **`async.spawn`** in scripts so you can call **`:await()`** outside an **`http`** handler. Example: **`apps/lua/examples/socket/echo_server.lua`** (or **`apps/lua/examples/tcp_echo_server.lua`**). On **Emscripten**, **`VARN_SOCKET_DRIVER`** is **`DUMMY`**; **`connect` / `listen`** reject at perform time.

## `ffi`

Lua-facing API follows **[lua-ffi](https://github.com/zhaojh329/lua-ffi)** (LuaJIT-style `ffi.cdef`, `ffi.new`, `ffi.C`, `ffi.load`, …). CMake selects **`VARN_FFI_DRIVER`**; see [build.md](build.md) and [official-libraries.md](official-libraries.md).

When **`LIBFFI`** is active, the module is the vendored C implementation linked into the host. When **`DUMMY`** is active, `require("ffi")` succeeds but every exported function errors.

Examples (desktop, **`LIBFFI`**): **`apps/lua/examples/ffi/puts.lua`** (libc), **`apps/lua/examples/ffi/sqlite3.lua`** (SQLite3 types: INTEGER, REAL, TEXT, BLOB, NULL).

## `async`

### `async.sleep(ms) -> Promise`

Resolves after `ms` milliseconds. On **native** hosts the delay is driven by the **task pool** worker path. On **Emscripten** (`varn_wasm`), **`emscripten_sleep`** is used instead (see [async.md](async.md#emscripten-varn_wasm)).

### `async.spawn(fn)`

Starts `fn` in a new coroutine. Used for advanced control flow; most server code relies on the **`http`** handler coroutine instead.

## `fs`

Behavior depends on **`VARN_FS_DRIVER`** (see [build.md](build.md)); **`STD`** uses the host filesystem (in the browser, Emscripten’s virtual filesystem / **MEMFS** when configured), **`DUMMY`** rejects reads/writes and makes `exists` always false.

### `fs.readFile(path) -> Promise`

Resolves to file contents as a string (binary-safe). Rejects on I/O error.

### `fs.writeFile(path, content) -> Promise`

Writes binary-safe string `content`. Resolves to `"ok"` on success.

### `fs.exists(path) -> boolean`

Synchronous existence check.

## `log`

Behavior depends on **`VARN_LOG_DRIVER`** (see [build.md](build.md)): default **`SPDLOG`** routes records through **spdlog**’s default logger; **`STDOUT`** writes leveled lines to **stdout**; **`DUMMY`** discards them.

### `log.debug(...)`, `log.info(...)`, `log.warn(...)`, `log.error(...)`

Each accepts **zero or more** arguments, like **`print`**: every value is converted with `tostring` (same rules as Lua’s `print`), joined by **tab**, then one line is emitted with the level tag.

## `crypto`

Behavior depends on the **crypto** driver id for your build (see [build.md](build.md)); with **`DUMMY`**, primitives throw when called.

### `crypto.digest(algorithm, data, format?) -> string`

When **`OPENSSL`** is active, computes a message digest using **OpenSSL 3** `EVP_MD_fetch` when available (algorithm names such as **`SHA256`**, **`SHA512`**, **`SHA3-256`** — see OpenSSL docs). Leading/trailing whitespace on **`algorithm`** is stripped; **casing** is not normalized by Varn, but common digests are usually accepted in several forms (e.g. **`SHA256`** and **`sha256`** both work with typical OpenSSL builds). **`format`** is optional: **`"hex"`** (default) for lowercase hex, or **`"raw"`** for raw digest bytes. For SHA-256 hex specifically, use **`crypto.digest("SHA256", str, "hex")`** (or **`"sha256"`** if your OpenSSL accepts it).

Known-value checks for **SHA-256** (empty string and **`abc`**) live in **`apps/lua/examples/crypto/digest_vectors.lua`** (prints **`SKIP`** and exits **0** when **`crypto.digest`** is unavailable).

### `crypto.hmac(digestAlgorithm, key, data, format?) -> string`

When **`OPENSSL`** is active, **HMAC** with the inner digest chosen by name (same style and naming rules as **`crypto.digest`**, e.g. **`SHA256`**, **`SHA512`**). **`format`** is optional: **`"hex"`** (default) or **`"raw"`**. RFC 4231 coverage: **`apps/lua/examples/crypto/hmac_vectors.lua`**.

### `crypto.randomBytes(n) -> string`

Returns `n` random bytes as a string when the driver provides a CSPRNG.

## `platform`

Host and naming helpers (not a CMake driver matrix).

### `platform.os() -> string`

Stable OS id, for example **`linux`**, **`macos`**, **`windows`**, **`ios`**, **`ios-simulator`**, **`android`**, **`wasm`**, etc.

### `platform.arch() -> string`

CPU id when detected, for example **`arm64`**, **`x86_64`**, **`wasm32`**.

### `platform.hostVersion() -> string`

Semver string of the **Varn host binary** (from **`project(... VERSION)`** in **`CMakeLists.txt`**, generated into **`VarnVersion.h`** at configure time).

### `platform.libPrefix()`, `platform.shlibSuffix() -> string`

Shared-library conventions (`lib` / `` and `.so` / `.dylib` / `.dll`).

### `platform.libraryFilename(name) -> string`

Returns a **filename** heuristic such as **`libz.so`** / **`libz.dylib`** / **`z.dll`** for logical name **`z`**.

### `platform.getLibraryPathByName(name, subdir?) -> string`

Builds **`subdir`/`filename`** (default **`./`**). This is a **development heuristic**, not a substitute for **`dlopen`** / **`ffi.load`** resolution on real systems.

## `zip`

When **`VARN_ENABLE_ZIP=ON`** and **libzip** is linked (**native** and **`varn_wasm`**), **`zip.extract`**, **`zip.create`**, and **`zip.list`** return **`Promise`** objects (work runs on the **task pool**).

- **`zip.extract(archivePath, destDir)`** — extracts all entries under **`destDir`**, rejecting **zip slip** paths (`..`, absolute entries, paths escaping the destination).
- **`zip.create(archivePath, entries)`** — **`entries`** is a non-empty array of **`{ file = "...", entry = "..." }`** (each **`entry`** must be a safe relative path).
- **`zip.list(archivePath)`** — resolves to an **array** of entry names (strings). Unsafe entry names cause rejection (same rules as **`extract`**).

When zip is disabled (**`VARN_ENABLE_ZIP=OFF`**), these functions error at call time.

## `Promise` userdata

Methods:

- **`promise:await()`** — if pending, yields the current coroutine until settled; then returns the value, or `nil, err` on rejection.
- **`promise:isDone()`** — boolean, true if resolved or rejected. On **native** hosts this reads cross-thread-visible state without blocking; another thread may still settle the promise immediately after, so use it only as a hint, not for synchronization. On **`varn_wasm`**, the runtime is single-threaded, but **task-pool** jobs are still **queued** and run after **`async.sleep(...)`** returns the promise—so **`isDone()`** can be **`false`** immediately after **`async.sleep`** until **`await`**, matching the usual mental model (see [async.md](async.md#emscripten-varn_wasm)).

## `arg` global

The host sets `arg` to a Lua array of command-line arguments (script path and parameters), similar to standard Lua. The WASM worker constructs **`Runtime`** with an **empty** argv list, so **`arg`** is typically an empty table there unless the embedding changes.
