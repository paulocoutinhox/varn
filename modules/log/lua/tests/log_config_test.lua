-- log config: setLevel filtering, structured fields, and the file sink round-trip.
local log = require("log")

local dir = assert(os.getenv("VARN_TEST_DIR"), "VARN_TEST_DIR is not set; run tests with: python3 varn.py test")

-- the configuration surface exists as functions.
assert(type(log.setLevel) == "function", "setLevel should be a function")
assert(type(log.toFile) == "function", "toFile should be a function")

-- setLevel accepts every documented level without erroring.
for _, level in ipairs({ "debug", "info", "warn", "error" }) do
    assert(pcall(log.setLevel, level), level .. " should be a valid level")
end

-- an unknown level is rejected.
assert(not pcall(log.setLevel, "verbose"), "unknown level should error")

-- raising the floor to error drops lower levels without crashing.
log.setLevel("error")
assert(pcall(log.debug, "suppressed debug"), "debug below the floor should not crash")
assert(pcall(log.info, "suppressed info"), "info below the floor should not crash")
assert(pcall(log.warn, "suppressed warn"), "warn below the floor should not crash")

-- structured fields render as space-separated key=value and never crash.
log.setLevel("debug")
assert(pcall(log.info, "request done", { method = "GET", status = 200, ms = 12 }), "structured fields should not crash")
assert(pcall(log.error, "failed", { code = 500 }), "error with fields should not crash")

-- the file sink writes a file that can be read back with the logged content.
local path = dir .. "/log_config_test.log"
os.remove(path)
log.toFile(path)

local marker = "varn-log-file-marker-" .. tostring(os.time())
log.info(marker, { sink = "file", n = 7 })

local file = assert(io.open(path, "rb"), "log file should exist after toFile")
local content = file:read("*a")
file:close()

assert(content:find(marker, 1, true), "logged marker should be present in the file")
assert(content:find("sink=file", 1, true), "structured field should be present in the file")
assert(content:find("n=7", 1, true), "numeric structured field should be present in the file")

os.remove(path)

print("log config ok")
