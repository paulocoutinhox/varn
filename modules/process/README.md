# ⚙️ process

Run shell commands and read the process environment — `exec` runs off the main loop and resolves a
promise, while the environment, working-directory, and argument accessors are synchronous.

```lua
local async = require("async")
local process = require("process")

async.run(function()
    print(process.exec("echo varn"):await().stdout)
end)
```

## Capabilities

| Function | What it does |
|---|---|
| `process.exec(command)` | Run `command` through `/bin/sh -c`; resolves a promise with `{stdout, stderr, code}` (`code` is `128 + signal` when killed). |
| `process.env` | Table of environment variable names to values, captured when the module is required. |
| `process.getenv(name, default?)` | The value of `name`, or `default` (or `nil`) when it is unset. |
| `process.cwd()` | The current working directory as a string. |
| `process.argv` | A 1-based array of the script arguments, dropping the script path and host options. |
| `process.available` | `true` when this build can run commands, `false` on builds with the stub driver. |

## Availability

Commands run via `fork`/`exec` (POSIX) or `CreateProcess` (Windows). Among Apple platforms only macOS
permits that, so iOS, tvOS, watchOS, and visionOS use the stub. The browser (wasm) has no process model
at all. On those targets `process.available` is `false` and `process.exec` rejects with
"not available in this build". See the [platform matrix](../../docs/platform-availability.md).

## Reference, examples, and tests

- Full reference: [docs/lua-api/process.md](../../docs/lua-api/process.md)
- Runnable examples: [lua/examples/](lua/examples/)
- Tests run in CI on Linux, macOS, and Windows: [lua/tests/](lua/tests/)
