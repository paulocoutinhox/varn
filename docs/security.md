# Security notes (high level)

This document is **not** a full audit. It records known **trust boundaries** and **sharp edges** for embedders and operators. For build-time knobs, see [build.md](build.md); for Lua surfaces, see [lua-api.md](lua-api.md).

## `crypto`

- **Timing**: Lua scripts are not written for constant-time comparison of secrets. Use established libraries at the edge (TLS termination, HSMs) for high-assurance crypto; treat **`crypto.digest`** (and **`crypto.hmac`**) as convenience primitives, not as side-channel–hardened building blocks.
- **Algorithms**: **`crypto.digest`** forwards algorithm names to **OpenSSL** when **`VARN_CRYPTO_DRIVER=OPENSSL`**. Invalid or legacy names fail at runtime; do not pass untrusted strings as algorithms without validation in your own code.

## `zip`

- **Zip slip**: **`zip.extract`** rejects **`..`**, absolute entries, and paths that escape the destination directory after canonicalization. Do not disable review for archives from untrusted sources.
- **Resource limits**: Large entries are read in chunks on disk, but **total archive size** and **entry count** are not hard-capped at the Lua layer today; host disk quotas still apply.

## `platform`

- **Information disclosure**: **`platform.os`**, **`platform.arch`**, and filename heuristics expose **non-secret** host facts useful for **`ffi.load`** paths. They are not a substitute for secure packaging or attestation.

## `http` / `http.client`

- **SSRF**: Server-side Lua that calls **`http.client.request`** with user-controlled URLs can reach internal networks from the host process. Restrict URLs in application policy or network egress rules.
- **WASM**: Outbound **`http.client.request`** uses the worker’s **`fetch()`** (see [wasm-http-roadmap.md](wasm-http-roadmap.md)); **CORS**, **cookies**, and **redirect** behavior follow the browser (see [build.md — Emscripten](build.md#emscripten-wasm)).

## `ffi`

- **Native code**: **`ffi`** can call arbitrary native symbols available to the process. Treat Lua with **`ffi`** as **equivalent to native code** from a sandboxing perspective.

## Reporting

Use the repository issue templates for vulnerabilities you believe should be handled privately; follow any **security policy** published on the project’s GitHub org if present.
