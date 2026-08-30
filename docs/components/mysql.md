# 🐬 mysql

An async MySQL client written in Lua that speaks the MySQL text protocol (`COM_QUERY`) directly over
the native `socket` module. It authenticates with `caching_sha2_password` (the MySQL 8 default, using
the server's RSA public key for full auth over a plain connection) as well as
`mysql_native_password`, so it connects to a stock modern MySQL out of the box. Every packet read and
write yields on the event loop, so one process keeps many queries in flight instead of blocking on
each.

Add the components directory to `package.path` first (see [components](../components.md)):

```lua
package.path = package.path .. ";./components/?.lua;./components/?/init.lua"
local mysql = require("mysql")
```

The whole lifecycle must live inside an async coroutine (`async.run` / `async.spawn`). It is
native-only, since it builds on the native `socket` module, which is unavailable in the browser.

## Connecting

`mysql.connect(options)` → a ready, authenticated client:

| Option | Default | Meaning |
|--------|---------|---------|
| `host` | `"127.0.0.1"` | server host |
| `port` | `3306` | server port |
| `user` | `"root"` | login user |
| `password` | — | login password |
| `database` | — | database selected during the handshake |
| `tls` | `false` | negotiate TLS during the handshake |
| `insecure` | `false` | with `tls`, skip certificate verification |

```lua
local async = require("async")

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

A failed connect, handshake, or authentication raises, so wrap it in `pcall` to inspect the error.

## Querying

| Function | What it does |
|---|---|
| `client:query(sql)` | Run a `COM_QUERY` and return an array of row tables keyed by column name. |
| `client:close()` | Send `COM_QUIT` and close the socket. |

Every value comes back as a **string** — the text protocol carries no types, and this client does no
decoding — so convert numerics at the call site with `tonumber`. There is no parameter binding and no
prepared statements: build and escape the SQL yourself, or reach for [vdo](vdo.md) when you need
placeholders and typed results.

A statement that returns no result set (`CREATE`, `INSERT`, `DROP`) resolves to an empty table. A
server error reply is raised as a Lua error.

## Choosing between `mysql` and `vdo`

Both talk to MySQL, and they are meant for different jobs:

| | `mysql` | [`vdo`](vdo.md) |
|---|---|---|
| Transport | native `socket`, pure Lua wire protocol | `ffi` into `libmysqlclient` |
| Host requirement | none | the client library installed |
| Parameter binding | no | `?` and `:name` |
| Prepared statements | no | yes |
| Value types | every column is a string | decoded |
| Portability | MySQL/MariaDB only | SQLite, MySQL/MariaDB, PostgreSQL |

Reach for `mysql` when you want a dependency-light client and are writing the SQL yourself, and pair
it with [pool](pool.md) to share connections. Reach for `vdo` when you want binding, types, or one
API across several databases.

## Examples

### `basic.lua`

```lua
-- creates a table, inserts rows and reads them back over the mysql text protocol, pointed at a server via VARN_MYSQL_HOST / VARN_MYSQL_PORT / VARN_MYSQL_USER / VARN_MYSQL_PASS / VARN_MYSQL_DB
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local async = require("async")
local mysql = require("mysql")

async.run(function()
    local client = mysql.connect({
        host = os.getenv("VARN_MYSQL_HOST") or "127.0.0.1",
        port = tonumber(os.getenv("VARN_MYSQL_PORT") or "3306"),
        user = os.getenv("VARN_MYSQL_USER") or "root",
        password = os.getenv("VARN_MYSQL_PASS"),
        database = os.getenv("VARN_MYSQL_DB") or "varntest",
    })

    client:query("DROP TABLE IF EXISTS varn_example_users")
    client:query("CREATE TABLE varn_example_users (id INT PRIMARY KEY, name VARCHAR(50), role VARCHAR(20))")
    client:query("INSERT INTO varn_example_users (id, name, role) VALUES (1, 'alice', 'admin'), (2, 'bob', 'user')")

    -- every column comes back as a string, so numerics are converted at the call site
    local rows = client:query("SELECT id, name, role FROM varn_example_users ORDER BY id")
    for _, row in ipairs(rows) do
        print(string.format("user %d: %s (%s)", tonumber(row.id), row.name, row.role))
    end

    local admins = client:query("SELECT COUNT(*) AS n FROM varn_example_users WHERE role = 'admin'")
    print("admins:", tonumber(admins[1].n))

    client:query("DROP TABLE varn_example_users")
    client:close()

    print("mysql basic ok")
end)
```
