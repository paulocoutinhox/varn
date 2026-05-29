# Security notes

This is not a full audit. It records known trust boundaries and sharp edges for people embedding or running Varn. For build options see [build.md](build.md). For Lua APIs see [lua-api.md](lua-api.md).

## crypto

`crypto.digest` and `crypto.hmac` are convenience primitives, not side-channel-hardened building blocks. Lua scripts are not written for constant-time secret comparison. For high-assurance work, use established tools at the edge, such as TLS termination or an HSM.

With `VARN_CRYPTO_DRIVER=OPENSSL`, `crypto.digest` passes algorithm names straight to OpenSSL. Invalid names fail at runtime. Do not pass untrusted strings as algorithm names without validating them yourself.

## zip

`zip.extract` rejects `..`, absolute entries, and paths that escape the destination after canonicalization (zip slip). Still review archives from untrusted sources.

Large entries are read in chunks, but total archive size and entry count are not capped at the Lua layer today. Host disk quotas still apply.

## platform

`platform.os`, `platform.arch`, and the filename helpers expose non-secret host facts, useful for `ffi.load` paths. They are not a substitute for secure packaging or attestation.

## http and http.client

Server-side Lua that calls `http.client.request` with user-controlled URLs can reach internal networks from the host process (SSRF). Restrict URLs in your application or with network egress rules.

In the browser, `http.client.request` uses the worker's `fetch()`, so CORS, cookies, and redirects follow the browser. See [wasm-http-roadmap.md](wasm-http-roadmap.md) and [build.md](build.md#webassembly).

## ffi

`ffi` can call any native symbol available to the process. Treat Lua that uses `ffi` as equivalent to native code for sandboxing purposes.

## Reporting

Use the repository issue templates for vulnerabilities that should be handled privately. Follow any security policy published on the project's GitHub org.
