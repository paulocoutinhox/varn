-- log: every severity helper exists and accepts multiple arguments without error.
local log = require("log")

for _, level in ipairs({ "debug", "info", "warn", "error" }) do
    assert(type(log[level]) == "function", level .. " should be a function")
end

log.debug("log", "debug", 1)
log.info("log", "info", 2)
log.warn("log", "warn", 3)
log.error("log", "error", 4)

print("log ok")
