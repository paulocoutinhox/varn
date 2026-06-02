# 🔒 Safety notes

Known trust boundaries and sharp edges for people running or embedding Varn. This is not a full audit.

## 🔐 crypto

`crypto.digest` and `crypto.hmac` are convenience primitives, not side-channel-hardened building blocks, and scripts are not a good place for constant-time secret comparison. For high-assurance work, handle it at the edge, such as TLS termination or an HSM. Do not pass untrusted strings as algorithm names without validating them yourself.

## 🗜️ zip

`zip.extract` rejects `..`, absolute entries, and paths that escape the destination (zip slip). Still review archives from untrusted sources. Total archive size and entry count are not capped today, so host disk quotas still apply.

## 🖥️ platform

`platform.os`, `platform.arch`, and the filename helpers expose non-secret host facts. They are not a substitute for secure packaging or attestation.

## 🌐 http and http.client

Server-side code that calls `http.client.request` with user-controlled URLs can reach internal networks (SSRF). Restrict URLs in your application or with network egress rules. In the browser, `http.client.request` follows the browser's CORS, cookies, and redirects.

## 🧩 ffi

`ffi` can call any native function available to the process. Treat scripts that use `ffi` as equivalent to native code for sandboxing purposes.

## 🛡️ Reporting

Use the repository issue templates for vulnerabilities that should be handled privately. Follow any security policy published on the project's GitHub org.
