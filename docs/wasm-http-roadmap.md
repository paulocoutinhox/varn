# WebAssembly HTTP client

## Current behavior

In the browser, `http.client.request` uses the `EMSCRIPTEN_FETCH` driver, which calls the browser `fetch()` API. The code is in `modules/http/src/drivers/emscripten_fetch/EmscriptenFetchHttpClient.cpp`.

Headers are sent from C++ to JS as JSON and parsed there into a plain object (an empty map becomes `{}`, never `null`, so `RequestInit` stays valid). The Lua contract is the same as desktop: you get a `Promise` that resolves to the `VARN/1 <status> <len>\n` plus body wire string, or rejects with an error string.

The WASM build uses `-s ASYNCIFY` for `async.sleep`, cooperative stop, and similar paths. The HTTP client does not use `-s FETCH=1`.

In-flight `fetch` calls are tracked so the host can keep pumping the worker until they finish. See `modules/core/include/varn/wasm/WasmAsyncHost.h`.

## Possible follow-ups (not committed)

- `credentials` and `redirect` policies that match embedding needs.
- An `AbortController` exposed to Lua, beyond the fixed `timeoutSeconds`.
- Streaming bodies (`ReadableStream` to a chunked bridge) instead of buffering the whole response in WASM memory.

## Security

Never `eval` URL or header strings from untrusted input. Server-side Lua that calls user-controlled URLs risks SSRF. See [security.md](security.md).

## References

- [build.md — WebAssembly](build.md#webassembly)
- [lua-api.md — http.client](lua-api.md)
- [async.md — Emscripten](async.md)
