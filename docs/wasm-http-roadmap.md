# WebAssembly HTTP client

## Current behavior

**`VARN_HTTP_CLIENT_DRIVER=EMSCRIPTEN_FETCH`** (Emscripten-only) implements **`http.client.request`** with the browser **`fetch()`** API from **`EM_JS`** in **`src/varn/http/drivers/emscripten_fetch/EmscriptenFetchHttpClient.cpp`**. Headers are serialized as JSON from C++ and parsed in JS into a **`Headers`**-compatible plain object (empty maps become **`{}`**, never **`null`**, so **`RequestInit`** stays valid). The Lua-facing contract is unchanged: a **`Promise`** that resolves to the **`VARN/1 <status> <len>\n` + body** wire string or rejects with a string.

The WASM build links **`-s ASYNCIFY`** for **`async.sleep`**, cooperative stop, and related paths; the HTTP client does **not** use **`-s FETCH=1`** / **`emscripten_fetch`**.

In-flight **`fetch`** calls are tracked so **`varnRunChunk`** can keep pumping the worker until they finish (see **`include/varn/wasm/WasmAsyncHost.h`**).

## Possible follow-ups (not committed)

- **`credentials` / `redirect`** policies aligned with embedding needs.
- **`AbortController`** surfaced from Lua (beyond fixed **`timeoutSeconds`**).
- **Streaming** bodies (`ReadableStream` → chunked bridge) instead of buffering the full response in WASM memory.

## Security

Never **`eval`** URL or header strings from untrusted input. Document **SSRF** when server-side Lua calls user-controlled URLs (see [security.md](security.md)).

## References

- [build.md — Emscripten](build.md#emscripten-wasm)
- [lua-api.md — `http.client`](lua-api.md)
- [async.md — Emscripten](async.md#emscripten-varn_wasm)
