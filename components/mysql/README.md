# 🐬 mysql

Async MySQL client that speaks the MySQL text protocol (`COM_QUERY`) directly over the native socket. It authenticates with `caching_sha2_password` (the MySQL 8 default, using the server's RSA public key for full auth over a plain connection) as well as `mysql_native_password`, so it connects to a stock modern MySQL out of the box. Every socket operation yields on the event loop, so one process keeps many queries in flight instead of blocking on each.

```lua
local async = require("async")
local mysql = require("mysql")

async.run(function()
    local client = mysql.connect({
        host = "127.0.0.1",
        port = 3306,
        user = "root",
        password = "secret",
        database = "app",
    })

    local rows = client:query("select id, name from users")
    for _, row in ipairs(rows) do
        print(row.id, row.name)
    end

    client:close()
end)
```

Every packet read and write runs on the event loop, so the whole lifecycle must live inside an async coroutine (`async.run` / `async.spawn`). Native-only — it builds on the native `socket` module, which is unavailable in the browser.

## Client

| Function | What it does |
|---|---|
| `mysql.connect(options)` | Open a socket, run the handshake, and return a ready client; `options` takes `host` (default `127.0.0.1`), `port` (default 3306), `user` (default `root`), `password`, `database`, and `tls = true` to negotiate TLS during the handshake (with `insecure = true` to skip certificate verification). |
| `client:query(sql)` | Run a `COM_QUERY` and return an array of row tables keyed by column name; all values come back as strings with no type decoding, and there is no parameter binding or prepared statements, so build and escape the SQL and convert numerics at the call site. |
| `client:close()` | Send `COM_QUIT` and close the socket. |

Exercising this component needs a live MySQL server, so it ships no `examples/` or `tests/` directory.

## Note

This socket-based `mysql` component is distinct from the FFI-backed MySQL driver inside the [vdo](../vdo) component. `vdo` offers prepared statements, typed decoding, and `:name`/`?` parameter binding; reach for it when you need those. This component is a direct, dependency-light text-protocol client and pairs with the [pool](../pool) component to share connections.
