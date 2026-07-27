-- integration test for the pure-lua mysql client speaking the wire protocol over the native socket, pointed at a server via VARN_MYSQL_HOST / VARN_MYSQL_PORT / VARN_MYSQL_USER / VARN_MYSQL_PASS / VARN_MYSQL_DB
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local async = require("async")
local mysql = require("mysql")

local host = os.getenv("VARN_MYSQL_HOST") or "127.0.0.1"
local port = tonumber(os.getenv("VARN_MYSQL_PORT") or "3306")
local user = os.getenv("VARN_MYSQL_USER") or "root"
local pass = os.getenv("VARN_MYSQL_PASS") or "varnpass"
local db = os.getenv("VARN_MYSQL_DB") or "varntest"

async.run(function()
    local client = mysql.connect({ host = host, port = port, user = user, password = pass, database = db })

    -- a fresh table drives create, insert and select through the text protocol
    client:query("DROP TABLE IF EXISTS varn_items")
    client:query("CREATE TABLE varn_items (id INT PRIMARY KEY, name VARCHAR(50))")
    client:query("INSERT INTO varn_items (id, name) VALUES (1, 'alpha'), (2, 'beta')")

    local rows = client:query("SELECT id, name FROM varn_items ORDER BY id")
    assert(#rows == 2, "two rows should be returned")
    assert(tonumber(rows[1].id) == 1 and rows[1].name == "alpha", "the first row matches")
    assert(tonumber(rows[2].id) == 2 and rows[2].name == "beta", "the second row matches")

    -- an aggregate proves multi-column decoding and a server-side computation
    local count = client:query("SELECT COUNT(*) AS n FROM varn_items")
    assert(tonumber(count[1].n) == 2, "the count aggregate should be 2")

    client:query("DROP TABLE varn_items")
    client:close()
    print("mysql integration ok")
end)
