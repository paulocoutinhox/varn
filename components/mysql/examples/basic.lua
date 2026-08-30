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
