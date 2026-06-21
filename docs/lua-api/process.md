# ⚙️ process

Run commands and read the process environment. `exec` runs off the main loop and returns a
promise; the environment and working-directory accessors are synchronous.

- `process.exec(command)` → promise resolving to a table `{stdout, stderr, code}`. The command runs through `/bin/sh -c`, so shell features like pipes and redirection work. Both streams are captured binary-safe with no size cap, and `code` is the exit status (`128 + signal` when the command is killed by a signal).
- `process.env` → a table mapping each environment variable name to its value, captured when the module is required.
- `process.getenv(name, default?)` → the value of `name`, or `default` (or `nil`) when it is unset.
- `process.cwd()` → the current working directory as a string.
- `process.argv` → an array of the script arguments. This is the same data as the global `arg` table, normalized to a 1-based array that drops `arg[0]` (the script path) and the host options, leaving just the arguments your script received.

Commands are not sandboxed; confine untrusted input before passing it here. A command containing a null byte is rejected rather than silently truncated. On builds without process support (such as the browser build) `process.exec` rejects with "not available in this build".

## Examples

### `exec_and_env.lua`

```lua
-- runs a command, prints its capture, and reports the working directory.
local async = require("async")
local process = require("process")

async.run(function()
    local result, err = process.exec("echo varn && echo oops 1>&2"):await()
    assert(not err, err)

    print("code:   " .. result.code)
    print("stdout: " .. result.stdout)
    print("stderr: " .. result.stderr)

    print("home:   " .. process.getenv("HOME", "(unset)"))
    print("cwd:    " .. process.cwd())

    if #process.argv > 0 then
        print("argv:   " .. table.concat(process.argv, " "))
    end
end)
```

## Under the hood

On native builds commands run through `fork` + `execl("/bin/sh", ...)` with pipes for stdout
and stderr, capturing each stream until end of file. The browser build uses a stub driver that
rejects `exec`.
