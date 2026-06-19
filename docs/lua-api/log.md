# 📝 log

Leveled logging. `log.debug(...)`, `log.info(...)`, `log.warn(...)`, and `log.error(...)`
each take any number of values, like `print`: they are converted with `tostring`, joined by
tabs, and written as one line with the level tag.

## Examples

### `levels.lua`

```lua
-- prints one line at each severity level
local log = require("log")

log.debug("debug", "line", 1)
log.info("info", "line", 2)
log.warn("warn", "line", 3)
log.error("error", "line", 4)
print("log levels emitted (check sink: spdlog/stdout/dummy per build)")
```
