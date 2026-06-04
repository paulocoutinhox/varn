# 🔒 Safety notes

Known trust boundaries and sharp edges for people running or embedding Varn. This is not a full audit.

## 🔐 crypto

`crypto.digest` and `crypto.hmac` are convenience primitives, not side-channel-hardened building blocks. When you verify a MAC or token, compare it with `crypto.equals` (constant-time) rather than `==`, which leaks bytes through timing. For high-assurance work, handle it at the edge, such as TLS termination or an HSM. Do not pass untrusted strings as algorithm names without validating them yourself.

## 📁 fs

Like Node, `fs` is unrestricted by design: `fs.readFile`/`fs.writeFile` operate on whatever path the script passes, with no sandbox, root confinement, or size cap — Varn runs your own code on your own machine. The consequence is that a path built from *untrusted request data* can read or overwrite anywhere the process can (traversal, symlink following); validate or confine such paths in your own application (the static file handler below is already confined). Reading a never-ending special file (e.g. `/dev/zero`) will grow until memory runs out, exactly as in Node.

## 🗜️ zip

`zip.extract` rejects `..`, absolute entries, and paths that escape the destination (zip slip). Still review archives from untrusted sources. Total archive size and entry count are not capped today, so host disk quotas still apply.

## 🖥️ platform

`platform.os`, `platform.arch`, and the filename helpers expose non-secret host facts. They are not a substitute for secure packaging or attestation. The library-name/subdir helpers build paths from their arguments without sanitization and are typically fed into `ffi.load`, so the same "treat as native code" trust applies — do not derive the name or directory from untrusted input.

## 🌐 http and http.client

Server-side code that calls `http.client.request` with user-controlled URLs can reach internal networks (SSRF). Restrict URLs in your application or with network egress rules. In the browser, `http.client.request` follows the browser's CORS, cookies, and redirects.

`ctx:file(path)` serves whatever path you give it with no root confinement — never build that path from untrusted request data without validating it (same traversal risk as `fs`). The built-in static file handler (`servePublic`) is confined and is the safe way to serve user-reachable paths.

The rate-limit middleware keys buckets by client IP. With `trustProxy = true` it trusts the left-most `X-Forwarded-For` entry, which a client can spoof — only enable it behind a proxy that overwrites that header, otherwise the limit is bypassable and the bucket table can grow per spoofed address.

## 🧩 ffi

`ffi` can call any native function available to the process. Treat scripts that use `ffi` as equivalent to native code for sandboxing purposes.

## 🛡️ Reporting

Use the repository issue templates for vulnerabilities that should be handled privately. Follow any security policy published on the project's GitHub org.
