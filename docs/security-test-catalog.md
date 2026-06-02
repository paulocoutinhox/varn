# Varn Security Vulnerability Catalog

A general reference list of security bug and exploit classes, organized by module/domain.
It enumerates **what can go wrong** — the vulnerability families historically reported and
exploited against this kind of code — and how each is exploited. No remediation state is
tracked here; this is the universe of failures to know about and test for.

### Entry schema

```
ID       MODULE-AREA-NN
Name     the vulnerability / attack
Class    CWE / OWASP (WSTG, ASVS, API Top 10) reference
Exploit  how it is triggered / what the failure point is
```

### Frameworks covered

OWASP WSTG v4.2 (Info Gathering, Config/Deploy, Identity, Authentication, Authorization,
Session, Input Validation, Error Handling, Cryptography, Business Logic, Client-side, API),
OWASP ASVS, OWASP API Security Top 10 (2023), CWE Top 25 + memory-safety classes.

---

## Cross-cutting (any native/Lua module)

### Memory safety (C/C++)

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| XC-MEM-01 | Stack buffer overflow | CWE-121 | oversized input written into a fixed stack buffer corrupts the stack / return address |
| XC-MEM-02 | Heap buffer overflow | CWE-122 | length/offset drives a write past a heap allocation, corrupting adjacent chunks |
| XC-MEM-03 | Out-of-bounds read | CWE-125 | crafted offset/length reads past a buffer, leaking memory |
| XC-MEM-04 | Use-after-free | CWE-416 | object used after free/GC; reclaimed memory is attacker-controlled |
| XC-MEM-05 | Double free | CWE-415 | freeing the same pointer twice corrupts allocator metadata |
| XC-MEM-06 | Null pointer dereference | CWE-476 | an unchecked null return is dereferenced → crash/DoS |
| XC-MEM-07 | Integer overflow/underflow | CWE-190/191 | size/count arithmetic wraps, producing a tiny/huge allocation or bad index |
| XC-MEM-08 | Signed/size_t cast | CWE-194/681 | length > INT_MAX becomes negative/truncated, defeating bounds checks |
| XC-MEM-09 | Type confusion (userdata) | CWE-843 | passing the wrong userdata type to a binding reinterprets memory |
| XC-MEM-10 | Uninitialized memory use | CWE-457 | reading a field before assignment leaks stale data / undefined behavior |
| XC-MEM-11 | Format string | CWE-134 | user data flows into a printf-style format, enabling read/write |
| XC-MEM-12 | OOB write / arbitrary write | CWE-787 | controlled index/offset writes outside a buffer → RCE primitive |

### Lua binding & runtime

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| XC-LUA-01 | Stack imbalance | CWE-664 | an error path leaves the Lua stack unbalanced → corruption/leak over time |
| XC-LUA-02 | Registry ref leak / double-unref | CWE-401 | `luaL_ref` not released (leak) or released twice (reuse of a slot) |
| XC-LUA-03 | Missing argument validation | CWE-20 | wrong/missing/nil args reach C code that assumes types → crash/UB |
| XC-LUA-04 | C++ exception across the C boundary | CWE-248 | a `throw` crosses a `lua_CFunction` into C-compiled Lua → abort/UB |
| XC-LUA-05 | NUL-truncated strings | CWE-626 | using `lua_tostring` (no length) truncates binary data at the first `\0` |
| XC-LUA-06 | Unisolated callback error | CWE-755 | a handler/hook error propagates and tears down the server |

### Concurrency

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| XC-CON-01 | Cross-thread Lua access | CWE-362 | touching `lua_State` from a worker thread races the main interpreter |
| XC-CON-02 | Data race on shared state | CWE-362 | concurrent unguarded access to shared maps/structures corrupts them |
| XC-CON-03 | TOCTOU | CWE-367 | state changes between a check and the dependent use |
| XC-CON-04 | Deadlock / shutdown hang | CWE-833 | a worker blocks on the main loop while it is being joined |

### Resource exhaustion & error handling

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| XC-RES-01 | Unbounded memory input | CWE-400 | multi-GB strings / huge counts / deep nesting exhaust memory |
| XC-RES-02 | Algorithmic complexity | CWE-407 | worst-case input drives super-linear CPU (sorting, parsing) |
| XC-RES-03 | FD/thread/handle leak | CWE-404/772 | churned objects/connections never released exhaust limits |
| XC-ERR-01 | Internal detail leak | CWE-209 | stack traces / internal messages returned to the client |
| XC-ERR-02 | Secret in logs | CWE-532 | tokens/keys/session ids written to logs |
| XC-ERR-03 | Fail open | CWE-636 | an error during a security check allows instead of denying |

---

## http — server & app framework

### Authentication — JWT

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-JWT-01 | `alg:none` bypass | CWE-347 | strip the signature and set `alg:none`; server accepts unsigned token |
| HTTP-JWT-02 | Algorithm confusion (RS↔HS) | CWE-347 | change RS256→HS256 and sign with the public key as the HMAC secret |
| HTTP-JWT-03 | Signature not verified / tampering | CWE-345 | modify claims (role/sub) when the signature is unchecked or forgeable |
| HTTP-JWT-04 | Weak/guessable secret | CWE-326 | brute-force a short HMAC secret offline, then forge tokens |
| HTTP-JWT-05 | Missing `exp` / no expiry | CWE-613 | a token without expiry is valid forever |
| HTTP-JWT-06 | No clock-skew handling | — | rigid `exp`/`nbf` checks reject valid tokens or, if absent, allow stale ones |
| HTTP-JWT-07 | Audience not validated | CWE-287 | a token minted for service A is accepted by service B |
| HTTP-JWT-08 | Issuer not validated | CWE-287 | a token from an untrusted issuer is accepted |
| HTTP-JWT-09 | `crit` ignored | RFC 8725 | a critical header extension the server doesn't understand is ignored |
| HTTP-JWT-10 | `jwk` header injection | CWE-347 | embed an attacker public key in the token header; server trusts it |
| HTTP-JWT-11 | `jku`/`x5u` URL injection | CWE-918/347 | point the key-set URL at an attacker host to supply the verifying key |
| HTTP-JWT-12 | `kid` injection (traversal/SQLi/RCE) | CWE-22/89 | `kid` used in a file path / SQL / command to load the key |
| HTTP-JWT-13 | `x5c` certificate injection | CWE-347 | self-signed cert in `x5c` accepted as a trust anchor |
| HTTP-JWT-14 | `cty`/nested-JWT abuse | CWE-20 | `cty` triggers nested parsing / deserialization |
| HTTP-JWT-15 | Non-object payload accepted | CWE-20 | a JSON-array payload is indexed as claims |
| HTTP-JWT-16 | Token replay / no revocation | CWE-294 | a captured token is reused after logout/compromise |
| HTTP-JWT-17 | Sign mutates caller input | CWE-664 | signing injects `exp`/`iat` into the caller's object, surprising later use |

### Authentication — sessions, keys, roles

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-SESS-01 | Predictable session ID | CWE-330 | guessable/sequential ids let an attacker ride another session |
| HTTP-SESS-02 | Session fixation | CWE-384 | id is not rotated on login; an attacker-set id survives authentication |
| HTTP-SESS-03 | Session store exhaustion | CWE-400 | flooding anonymous sessions exhausts the server store |
| HTTP-SESS-04 | Missing cookie flags | CWE-1004/614 | no `HttpOnly`/`Secure`/`SameSite` enables theft via XSS/MITM/CSRF |
| HTTP-SESS-05 | No idle timeout | CWE-613 | an abandoned session stays valid indefinitely |
| HTTP-SESS-06 | No absolute timeout | CWE-613 | a session kept active never expires, widening the theft window |
| HTTP-SESS-07 | Missing `__Host-`/`__Secure-` prefix | — | without the prefix, a sibling subdomain can overwrite the cookie |
| HTTP-AUTH-01 | Timing-unsafe key compare | CWE-208 | byte-by-byte API-key/token compare leaks the value via timing |
| HTTP-AUTH-02 | Malformed credential parsing | CWE-20 | sloppy `Authorization` parsing accepts malformed/empty credentials |

### Authorization

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-AZ-01 | BOLA / IDOR | CWE-639 (API1) | swap an object id to access another user's resource |
| HTTP-AZ-02 | BFLA (function-level) | CWE-285 (API5) | call an admin/privileged endpoint as a normal user |
| HTTP-AZ-03 | Mass assignment / BOPLA | CWE-915 (API3) | post extra fields (`role`, `isAdmin`) that get bound to the model |
| HTTP-AZ-04 | Forced browsing | CWE-425 | reach undisclosed endpoints that lack authz |
| HTTP-AZ-05 | Path-normalization authz bypass | CWE-22 | `//admin`, `/./admin`, `%2e` evade prefix-based access rules |
| HTTP-AZ-06 | Privilege escalation via role claim | CWE-269 | trust a client-supplied role/scope without server validation |

### Input validation & injection

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-INJ-01 | Header CRLF injection | CWE-113 | `\r\n` in a header name/value injects new headers |
| HTTP-INJ-02 | Response splitting | CWE-113 | CRLF in a header smuggles a second HTTP response |
| HTTP-INJ-03 | Request smuggling CL.TE | CWE-444 | front-end uses Content-Length, back-end uses Transfer-Encoding |
| HTTP-INJ-04 | Request smuggling TE.CL | CWE-444 | front-end uses Transfer-Encoding, back-end uses Content-Length |
| HTTP-INJ-05 | Request smuggling TE.TE | CWE-444 | malformed `Transfer-Encoding` makes one server ignore it |
| HTTP-INJ-06 | HTTP/2→1 downgrade desync | CWE-444 | h2 request fields smuggle an h1 request at the edge |
| HTTP-INJ-07 | Host header injection | CWE-644 | spoofed `Host` poisons cache / password-reset / absolute URLs |
| HTTP-INJ-08 | Open redirect | CWE-601 | user-controlled `Location` redirects victims to an attacker site |
| HTTP-INJ-09 | Reflected XSS | CWE-79 | unescaped input reflected in error pages / directory listings |
| HTTP-INJ-10 | Cookie name/value injection | CWE-113 | `;`/CRLF in a cookie forges attributes or a second cookie |
| HTTP-INJ-11 | Cookie attribute injection | CWE-113 | unsanitized `path`/`domain` injects `Secure`/`Domain` attributes |
| HTTP-INJ-12 | SQL injection | CWE-89 | unparameterized query built from request input |
| HTTP-INJ-13 | OS command injection | CWE-78 | request input passed to a shell/command |
| HTTP-INJ-14 | Server-side template injection | CWE-1336 | input evaluated by a template engine |
| HTTP-INJ-15 | Log injection | CWE-117 | CRLF in a logged value forges log lines |
| HTTP-INJ-16 | HTTP parameter pollution | CWE-235 | duplicate params (`a=1&a=2`) parsed inconsistently across layers |

### CSRF / CORS

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-CSRF-01 | Missing CSRF protection | CWE-352 | a state-changing request is accepted with only ambient cookies |
| HTTP-CSRF-02 | Forgeable token | CWE-352 | the anti-CSRF token is predictable or unsigned |
| HTTP-CSRF-03 | Naive double-submit / cookie tossing | CWE-352 | a sibling subdomain injects a cookie that the server echoes back |
| HTTP-CSRF-04 | Login CSRF | CWE-352 | force a victim to authenticate as the attacker |
| HTTP-CSRF-05 | State change on safe method | CWE-650 | a GET/HEAD performs a mutation, dodging CSRF defenses |
| HTTP-CORS-01 | Wildcard + credentials | CWE-942 | `ACAO:*` with credentials exposes authenticated responses |
| HTTP-CORS-02 | Reflected arbitrary origin | CWE-942 | the server echoes any `Origin`, enabling cross-site reads |
| HTTP-CORS-03 | Null origin allowed | CWE-942 | `Origin: null` (sandboxed iframe) is trusted |
| HTTP-CORS-04 | Substring/prefix origin match | CWE-942 | `evil-trusted.com`/`trusted.com.evil` passes a loose match |
| HTTP-CORS-05 | Missing `Vary: Origin` | CWE-942 | a cached response leaks an allowed origin to others |

### Routing & path

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-RT-01 | Control chars in path | CWE-74 | raw control bytes desync matching and leak into params/logs |
| HTTP-RT-02 | Route-constraint ReDoS | CWE-1333 | a backtracking `where()` regex + crafted segment hangs the worker |
| HTTP-RT-03 | HEAD handling mismatch | RFC 9110 | HEAD not derived from GET, leaking inconsistent headers/bodies |
| HTTP-RT-04 | Method/Allow inconsistency | RFC 9110 | wrong/missing `Allow` on 405 / unhandled OPTIONS |
| HTTP-RT-05 | URL-build injection | CWE-116 | unescaped params in generated URLs inject new segments |
| HTTP-RT-06 | Route shadowing | CWE-696 | overlapping route precedence exposes an unintended handler |

### Static files

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-ST-01 | Path traversal | CWE-22 | `../`, `%2e%2e`, `....//`, overlong UTF-8, NUL byte escape the web root |
| HTTP-ST-02 | Symlink escape | CWE-59 | a symlink inside the root resolves to a file outside it |
| HTTP-ST-03 | Dotfile/secret exposure | CWE-538 | serving `.env`, `.git/`, backup files leaks secrets/source |
| HTTP-ST-04 | MIME sniffing | CWE-430 | missing `nosniff` lets the browser execute uploaded content |
| HTTP-ST-05 | Large-file memory blowup | CWE-400 | reading a whole large file into memory exhausts RAM |
| HTTP-ST-06 | Blocking disk I/O on event loop | CWE-400 | a large/slow read on the main loop stalls all requests |
| HTTP-ST-07 | Range abuse | CWE-400/190 | overlapping/negative/oversized ranges over-allocate or overflow |
| HTTP-ST-08 | Directory listing XSS | CWE-79 | unescaped filenames in an auto-index execute as HTML |

### Body parsing & DoS

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-BODY-01 | Unbounded body | CWE-400 | a huge request body exhausts memory |
| HTTP-BODY-02 | Form/field flood | CWE-400 | a body full of separators drives unbounded parse work |
| HTTP-BODY-03 | Multipart bomb | CWE-400 | thousands of parts / huge part headers exhaust resources |
| HTTP-BODY-04 | Content-Type confusion | CWE-436 | mismatched content type bypasses parser-based validation |
| HTTP-DOS-01 | Slowloris (slow headers) | CWE-400 | drip-feeding headers holds connections open |
| HTTP-DOS-02 | Slow body | CWE-400 | drip-feeding the body ties up workers |
| HTTP-DOS-03 | Missing/weak rate limiting | CWE-770 (API4) | unthrottled requests exhaust CPU/IO/backends |
| HTTP-DOS-04 | Spoofed client IP | CWE-348 | trusting `X-Forwarded-For` lets one client evade per-IP limits |
| HTTP-DOS-05 | Large/abundant headers | CWE-400 | oversized or numerous headers exhaust parser memory |
| HTTP-DOS-06 | Hash-flood on param maps | CWE-407 | colliding keys degrade a hash-map to O(n) |
| HTTP-DOS-07 | Connection exhaustion | CWE-400 | many idle keep-alive connections starve the pool |

### Security headers & misconfig

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-CFG-01 | Missing security headers | CWE-693 | no `X-Content-Type-Options`/`X-Frame-Options`/`Referrer-Policy` |
| HTTP-CFG-02 | Missing/weak CSP | CWE-1021 | no Content-Security-Policy enables XSS/clickjacking escalation |
| HTTP-CFG-03 | Missing HSTS | CWE-319 | no `Strict-Transport-Security` allows SSL-strip downgrade |
| HTTP-CFG-04 | Missing COOP/COEP/CORP | CWE-1021 | cross-origin isolation headers absent (Spectre/leak vectors) |
| HTTP-CFG-05 | Sensitive responses cached | CWE-525 | auth responses lack `no-store` and get cached |
| HTTP-CFG-06 | Verbose server banner | CWE-200 | `Server`/version headers aid fingerprinting |

### TLS

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-TLS-01 | Legacy protocol enabled | CWE-326 | SSLv3/TLS 1.0/1.1 negotiable |
| HTTP-TLS-02 | Weak ciphers | CWE-327 | RC4/3DES/EXPORT/NULL/MD5 suites offered |
| HTTP-TLS-03 | POODLE | CVE-2014-3566 | padding oracle on downgraded SSLv3 CBC |
| HTTP-TLS-04 | BEAST | CVE-2011-3389 | CBC IV weakness in TLS 1.0 |
| HTTP-TLS-05 | CRIME/BREACH | CVE-2012-4929 | compression side-channel recovers secrets |
| HTTP-TLS-06 | Lucky13 | CVE-2013-0169 | CBC-padding timing side-channel |
| HTTP-TLS-07 | Heartbleed / library CVEs | CVE-2014-0160 | OpenSSL memory disclosure / unpatched CVEs |
| HTTP-TLS-08 | Insecure renegotiation | CVE-2009-3555 | client-initiated reneg enables request injection |
| HTTP-TLS-09 | Downgrade | CWE-757 | MITM forces a weaker protocol/cipher |

### WebSocket

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| HTTP-WS-01 | Cross-site WebSocket hijacking | CWE-1385 | no Origin check on upgrade; attacker page opens an authed socket |
| HTTP-WS-02 | Unbounded message memory | CWE-400 | a fragmented stream assembles into unbounded memory |
| HTTP-WS-03 | Plaintext ws:// with secrets | CWE-319 | tokens sent over unencrypted WebSocket |
| HTTP-WS-04 | Missing per-connection limits | CWE-770 | frame/connection flooding exhausts resources |
| HTTP-WS-05 | Message injection | CWE-74 | unvalidated frame data drives downstream injection |

---

## http — client

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| CLI-01 | SSRF via URL | CWE-918 (API7) | a user-supplied URL targets internal services (`127.0.0.1`, `169.254.169.254`, `[::1]`) |
| CLI-02 | SSRF filter bypass | CWE-918 | decimal/octal/hex/IPv6 IP encodings, `@`-tricks, redirect chains defeat denylists |
| CLI-03 | DNS rebinding | CWE-918 | a host resolves safe at check time then to an internal IP at connect time |
| CLI-04 | Dangerous URL scheme | CWE-918 | `file://`/`gopher://`/`ftp://` reach local files or other protocols |
| CLI-05 | No TLS verification | CWE-295 | accepting any certificate enables MITM |
| CLI-06 | Unbounded response | CWE-400 | a hostile server streams an unbounded body into memory |
| CLI-07 | Auto-followed redirect to internal | CWE-601/918 | a 30x redirect chains the client to an internal target |
| CLI-08 | Request header injection | CWE-113 | CRLF in client-supplied headers splits the outbound request |
| CLI-09 | Response decompression bomb | CWE-409 | a gzip-bomb response expands to exhaust memory |

---

## json

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| JSON-01 | Array/object type confusion | CWE-704 | sequence tables wrongly serialized as objects (or vice versa) break consumers |
| JSON-02 | NaN/Inf serialization crash | CWE-248 | non-finite numbers throw during encoding, crashing the request |
| JSON-03 | Invalid UTF-8 crash | CWE-248 | raw bytes in a string make the encoder throw across the C boundary |
| JSON-04 | Deep nesting stack overflow | CWE-674 | deeply nested arrays/objects overflow the recursive parser |
| JSON-05 | Large number precision | CWE-681 | 64-bit values lose precision or wrap when mapped to the number type |
| JSON-06 | Duplicate keys | CWE-20 | repeated keys parsed inconsistently across components |
| JSON-07 | Total-size DoS | CWE-400 | a huge JSON document exhausts memory/CPU |

---

## xml

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| XML-01 | XXE — file read | CWE-611 | `<!DOCTYPE … SYSTEM "file:///etc/passwd">` reads local files |
| XML-02 | XXE — SSRF | CWE-611 | an external entity URL makes the parser fetch internal resources |
| XML-03 | Billion laughs / entity expansion | CWE-776 | nested entities expand exponentially, exhausting memory |
| XML-04 | External/parameter DTD | CWE-611 | remote DTD or parameter entities exfiltrate data |
| XML-05 | XInclude / XSLT abuse | CWE-611 | `xi:include` or stylesheet processing reads files / executes |
| XML-06 | Output injection | CWE-91 | unescaped values/tag names break the serialized document |
| XML-07 | Serialize depth | CWE-674 | deeply nested input overflows the recursive serializer |

---

## crypto

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| CRY-01 | Weak hash/algorithm | CWE-327/328 | MD5/SHA1 used for signatures/integrity |
| CRY-02 | Predictable randomness | CWE-330 | non-CSPRNG used for tokens/keys/IVs |
| CRY-03 | Timing-unsafe comparison | CWE-208 | non-constant-time MAC/secret compare leaks via timing |
| CRY-04 | Nonce/IV reuse | CWE-323 | reusing a nonce in an AEAD/stream cipher breaks confidentiality |
| CRY-05 | Insufficient key length | CWE-326 | short keys are brute-forceable |
| CRY-06 | Unbounded input | CWE-400 | hashing/HMAC of multi-GB input exhausts memory |
| CRY-07 | Context/key leak on error | CWE-401/320 | crypto contexts or key material leaked on error paths |
| CRY-08 | Padding oracle | CWE-696 | error/timing differences in padding checks decrypt data |

---

## fs

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| FS-01 | Path traversal read | CWE-22 | `../` in a path reads files outside the intended directory |
| FS-02 | Arbitrary file write | CWE-73 | a controlled write path overwrites sensitive files |
| FS-03 | Symlink following | CWE-59 | writing/reading through a symlink escapes the target directory |
| FS-04 | TOCTOU | CWE-367 | the file is swapped between `exists` and `read`/`write` |
| FS-05 | Large-file read OOM | CWE-400 | reading a huge file into memory exhausts RAM |
| FS-06 | NUL truncation | CWE-626 | a NUL byte truncates the path or content |
| FS-07 | Special-file hang | CWE-400 | reading `/dev/zero`/a fifo blocks or never ends |
| FS-08 | Disk exhaustion | CWE-400 | writing unbounded content fills the disk |

---

## ffi

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| FFI-01 | Arbitrary code execution | CWE-829 | calling arbitrary native symbols by design — the core risk surface |
| FFI-02 | Null/invalid pointer | CWE-476 | passing a bad pointer to a C call corrupts/crashes |
| FFI-03 | Type/signature confusion | CWE-843 | argument types mismatch the C signature, corrupting the stack |
| FFI-04 | Buffer over-read/write | CWE-119 | an undersized buffer for an out-param overflows |
| FFI-05 | Integer width truncation | CWE-190 | a 64-bit value truncated into a 32-bit C argument |
| FFI-06 | Untrusted library load | CWE-426 | loading an attacker-controlled/relative library path |
| FFI-07 | Declaration parser flaw | CWE-20 | a malformed C declaration crashes/misparses the FFI parser |
| FFI-08 | Callback lifetime | CWE-416 | a Lua callback used after its state is gone |

---

## socket

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| SOCK-01 | Receive buffer mishandling | CWE-119 | a huge/negative read size over-allocates or under-reads |
| SOCK-02 | SSRF via connect | CWE-918 | connecting to internal hosts/ports from user input |
| SOCK-03 | FD exhaustion | CWE-404 | sockets never closed exhaust descriptors |
| SOCK-04 | Blocking call on event loop | CWE-400 | a blocking `receive`/`accept` stalls the main loop |
| SOCK-05 | NUL truncation | CWE-626 | binary payloads truncated at the first NUL |
| SOCK-06 | UDP spoofing/amplification | CWE-406 | spoofed source / reflection abuse |

---

## zip

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| ZIP-01 | Zip slip (relative) | CWE-22 | an entry named `../../x` writes outside the extraction root |
| ZIP-02 | Zip slip (absolute) | CWE-22 | an absolute entry path writes to a fixed location |
| ZIP-03 | Zip bomb (decompression) | CWE-409 | a tiny archive expands to gigabytes, exhausting disk/memory |
| ZIP-04 | Entry-count flood | CWE-400 | millions of entries exhaust inodes/CPU |
| ZIP-05 | Symlink entry escape | CWE-59 | a symlink entry redirects writes outside the root |
| ZIP-06 | Nested/quine bomb | CWE-409 | recursive/self-referential archives amplify on extraction |
| ZIP-07 | Malicious entry name/encoding | CWE-22 | overlong/mojibake names defeat naive path checks |

---

## async

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| ASYNC-01 | Promise ref leak | CWE-401 | coroutine/callback registry refs never released |
| ASYNC-02 | Double settle | CWE-362 | resolving and rejecting the same promise corrupts state |
| ASYNC-03 | Cross-thread settle race | CWE-362 | settling from a worker while the main loop runs races Lua |
| ASYNC-04 | Busy-spin / starvation | CWE-400 | tight async loops starve the event loop |
| ASYNC-05 | Unhandled rejection | CWE-755 | a rejection with no handler is lost or crashes |

---

## core / runtime

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| CORE-01 | Lua sandbox escape | CWE-265 | exposed `os.execute`/`io.popen`/`package.loadlib`/`ffi` give command execution |
| CORE-02 | Bytecode loading | CWE-94 | `load`/`loadstring` of crafted bytecode subverts the VM |
| CORE-03 | No script resource limits | CWE-400 | an untrusted script loops/allocs without an instruction/memory cap |
| CORE-04 | Task pool exhaustion | CWE-400 | many blocking tasks starve/deadlock the worker pool |
| CORE-05 | Teardown use-after-free | CWE-416 | the Lua state is destroyed while a server still holds refs |
| CORE-06 | Event-loop starvation | CWE-400 | long synchronous work on the main loop blocks everything |
| CORE-07 | Env/arg injection | CWE-454 | malicious `VARN_*` env or argv alters configuration |
| CORE-08 | Unhandled top-level error | CWE-755 | an uncaught error crashes the process instead of exiting cleanly |

---

## log

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| LOG-01 | Log injection (CRLF) | CWE-117 | newlines in a logged value forge or split log entries |
| LOG-02 | Format string | CWE-134 | `%s`/`{}` in a message interpreted as a format specifier |
| LOG-03 | Secret disclosure | CWE-532 | tokens/keys/session ids written to logs |
| LOG-04 | Log-volume DoS | CWE-400 | forcing excessive logging exhausts disk/IO |

---

## platform

| ID | Name | Class | Exploit |
|----|------|-------|---------|
| PLAT-01 | Information disclosure | CWE-200 | host/version/library-path details leak to clients |

---

## Standalone modules: json & xml — security test battery

Both are now first-class standalone Lua modules, independent of http, with full C++↔Lua
type conversion. The aggressive battery below is automated in
`modules/json/lua/tests/json_test.lua` and `modules/xml/lua/tests/xml_test.lua`.

### json

API: `encode(value [, {pretty=true | indent=N}])` (alias `stringify`),
`decode(text)` (alias `parse`).
Type mapping: string, number (int/float), boolean, `nil`↔`null`, sequence↔`[]`, map↔`{}`.

| ID | Vulnerability | Exploit exercised |
|----|---------------|-------------------|
| JSON-01 | Array/object confusion | a sequence must encode as `[]` and a map as `{}` |
| JSON-02 | NaN/Infinity crash | `1/0`, `-1/0`, `0/0` encode as `null`, never throw |
| JSON-03 | Invalid UTF-8 crash | raw bytes (`\xff\xfe\x80`) are replaced; encoder never throws across the C boundary |
| JSON-04 | Deep-nesting parse overflow | `[`×5000 is rejected before the recursive parser can overflow |
| JSON-05 | Deep-nesting encode | a 5000-deep table is bounded (depth-capped), not a crash |
| JSON-06 | Binary unsafety | an embedded NUL survives an encode→decode round-trip (length preserved) |
| JSON-07 | Malformed/truncated/empty input | `{not json`, `[1,2,`, `""` are rejected with an error |
| JSON-08 | Duplicate keys | `{"a":1,"a":2}` resolves deterministically (last wins) |
| JSON-09 | Large flat input | a 10000-element flat array is handled |

### xml

API: `encode(node [, {pretty=true | indent=N}])` (alias `stringify`),
`decode(text)` (alias `parse`).
Node model (lossless round-trip): `{ name, attributes = {..}, children = {..}, text }`.

| ID | Vulnerability | Exploit exercised |
|----|---------------|-------------------|
| XML-01 | XXE — file read | `<!ENTITY xxe SYSTEM "file:///etc/passwd">` never exposes file contents (pugixml loads no external entities) |
| XML-02 | XXE — SSRF | an external-entity URL is never fetched |
| XML-03 | Billion laughs / entity expansion | nested entities are not expanded; no memory blowup or crash |
| XML-04 | Deep-nesting parse overflow | XML→node conversion is depth-capped (256); deep input does not overflow the stack |
| XML-05 | Output injection | text and element names are escaped/sanitized on encode |
| XML-06 | Element-name injection | invalid name characters become `_` |
| XML-07 | Malformed/non-xml/empty input | `<unclosed>`, `not xml`, `""` are rejected with an error |

---

## References

- OWASP WSTG v4.2 — https://owasp.org/www-project-web-security-testing-guide/v42/
- OWASP ASVS — https://owasp.org/www-project-application-security-verification-standard/
- OWASP API Security Top 10 (2023) — https://owasp.org/API-Security/editions/2023/en/
- OWASP Cheat Sheets — CSRF, Session Management, WebSocket, JWT, Cookie, REST, XXE Prevention.
- PortSwigger Web Security Academy — JWT, CORS, SSRF, request smuggling, WebSockets.
- PayloadsAllTheThings — Directory Traversal, CORS, SSRF, JWT, XXE.
- CWE Top 25 + CWE-1399 (memory safety) — https://cwe.mitre.org/
- HackTricks — JWT, HTTP request smuggling, SSRF, Lua sandbox escape, zip slip.
- RFCs — 9110/9112 (HTTP), 6265bis (Cookies), 7519/8725 (JWT/BCP), 6266 (Content-Disposition),
  5987 (header encoding), 7233 (Range), 9113 (HTTP/2).
