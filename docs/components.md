# 🧱 Components

Components are reusable libraries written in pure Lua on top of what Varn already provides (`ffi`,
`socket`, `async`). They are **not** built into the C++ core — they ship as source under
`components/` so you can read, copy, or vendor them into your own project.

Because they are not native modules, they are not on the default `require` path. Add the components
directory to `package.path` before requiring them:

```lua
package.path = package.path .. ";./components/?.lua;./components/?/init.lua"
```

The bundled examples set this up from their own location, so they run from any directory.

## Components

| Component | Description |
|-----------|-------------|
| [🗄️ vdo](components/vdo.md) | PDO-style database access for SQLite, MySQL/MariaDB, and PostgreSQL |
| [🧰 redis](components/redis.md) | Redis client speaking RESP2 over a single connection |
| [🔧 env](components/env.md) | Load a `.env` file and read typed environment variables |
| [✅ validate](components/validate.md) | Validate a table against a declarative schema |
| [🧪 test](components/test.md) | A tiny `describe` / `it` / `expect` test runner |
| [🔁 retry](components/retry.md) | Retry-with-backoff plus interval and timeout scheduling |

Each example lives next to its component under `components/<component>/examples/`.
