# 🧭 Roadmap

A per-module gap analysis against what Express, Fastify, FastAPI, Django, and the Node/Python
standard libraries give developers **day-to-day**. The bar is real-world apps and keeping Varn
**simple to use** — the 20% of features reached for 80% of the time, not exhaustive parity. Items
are tagged **[Essential]** (real apps get painful without it), **[Important]** (commonly needed),
**[Nice]** (real but lower frequency), with a rough size.

## Already covered — do not rebuild

- **Web (`http`)**: routing with param constraints, named routes and reverse URLs, route groups and
  versioning, global/group/route middleware, shipped middleware (CORS, security headers, CSRF,
  rate-limit, API-key, JWT + role guards), sessions with rotation, body parsing
  (json/form/multipart + uploads), full cookie attributes, static files (ETag/range/304/cache +
  `sendfile`), WebSockets, centralized error and 404 handlers, chunked streaming, request hardening.
- **Data**: `json`, `xml`, and the **`vdo`** component (PDO-style SQL for SQLite/MySQL/Postgres with
  prepared statements and transactions) and **`redis`** component. The database and cache stories are
  already filled — by components, which keeps the C++ core lean.
- **System**: `fs` (read/write/exists/mkdir/remove + streaming `open` handle), `socket` (TCP/UDP),
  `zip`, `ffi`, `platform`, and an `http` client.
- **Crypto**: `digest`, `hmac`, `randomBytes`, constant-time `equals`.
- **Runtime**: `async` (sleep/spawn/run/await), leveled `log`, multi-process scaling.

## Gaps by module

### `crypto` (native)
- **[Essential]** Password hashing — `crypto.hashPassword`/`verifyPassword` (scrypt or bcrypt via the
  already-linked OpenSSL). Raw `digest("sha256")` is wrong for passwords, and the auth middleware
  already invites logins. *medium*
- **[Essential]** `base64` / `base64url` / `hex` encode-decode exposed to Lua — the codec already
  exists in C++ (used by JWT), just not surfaced. *small*
- **[Important]** `crypto.uuid()` (v4/v7) over the existing CSPRNG — IDs, request/correlation keys. *small*
- **[Important]** AES-256-GCM `encrypt`/`decrypt` — data at rest (cookies, PII, secrets). *medium*
- **[Nice]** PBKDF2 / HKDF key derivation. *small*

### `fs` (native)
- **[Essential]** `fs.stat` — size, mtime, `isDir`/`isFile`. *small*
- **[Essential]** `fs.readdir` / directory listing — the engine already walks directories internally
  for static serving; it is just not bound to Lua. *small*
- **[Important]** `fs.rename`/`move`, `fs.copy`, `fs.append`. *small*
- **[Nice]** temp file/dir (`mkdtemp`), `glob`/recursive walk. *small*

### `http` server (native)
- **[Essential]** gzip/brotli response compression negotiated on `Accept-Encoding` — nearly every
  production JSON/HTML app enables it. *medium*
- **[Important]** Server-Sent Events helper (`ctx:sse()`) — live dashboards, progress, LLM token
  streaming; today only raw chunked `write` exists. *small-medium*
- **[Important]** WebSocket broadcast / rooms — chat, presence, live updates; today a handler cannot
  reach other connections. *medium*
- **[Important]** `Cache-Control`/ETag on dynamic responses (static files already have them). *small*
- **[Nice]** Content negotiation (`ctx:accepts`), wildcard/optional route params. *small*

### `http` client (native primitive + thin Lua wrapper)
- **[Important]** Ergonomic client — JSON body/response, a query-param table, and a
  `{status, headers, json()}` result instead of the raw wire string every example hand-parses today. *small-medium*

### `async` (native)
- **[Important]** Promise combinators — `all` / `race` / `any` / `allSettled`, plus a timeout and a
  concurrency limit. Anyone making parallel HTTP/DB calls writes fragile counters by hand today. *medium*

### `log` (native)
- **[Important]** Runtime level config, structured key/value fields, and a file/rotating sink. *medium*

### `socket` (native)
- **[Important]** TLS client sockets — talk to databases, brokers, TLS services. *medium*
- **[Nice]** Unix domain sockets. *medium*

### `process` (new native module)
- **[Essential]** `process.exec`/`spawn` capturing stdout/stderr/exit code — examples already shell
  out via `os.execute`, which cannot capture output. *medium*
- **[Important]** environment read/enumerate, `cwd`. *small*

## Cross-cutting — best as pure-Lua components (keep the core simple, like `vdo`/`redis`)

- **[Important]** **config/env** — `.env` loading + typed `env.get("PORT", 8080)` / `env.require(...)`. *small*
- **[Important]** **validation** — schema validation for request bodies (FastAPI/zod/joi territory),
  pairing with `http` + `json`; removes the most-repeated handler boilerplate. *medium*
- **[Important]** **datetime** — parse/format/arithmetic/ISO-8601/timezones; Lua's `os.date` is weak. *medium*
- **[Important]** **test** — `describe`/`it` + assertions + a runner, to dogfood the suite. *medium*
- **[Nice]** **retry/backoff** and a **scheduler/cron** (interval/repeat) helper. *small*

## Suggested order (first-week-of-a-real-app value)

1. Password hashing, `base64`/`hex`, `uuid` (`crypto`) — logins and IDs.
2. `fs.stat` + `fs.readdir` + `rename`/`copy`/`append` — everyday file work.
3. http-client JSON/query ergonomics — stop hand-parsing the wire.
4. `config/env` component — `.env` and typed config.
5. `async` combinators — parallel calls.
6. `datetime` and `validation` components.
7. gzip compression (`http`).
8. `process.exec`.
