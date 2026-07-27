# ⚙️ process

Run commands and read the process environment. `exec` runs off the main loop and returns a
promise; the environment and working-directory accessors are synchronous.

- `process.exec(command)` → promise resolving to a table `{stdout, stderr, code}`. The command runs through the platform shell — `/bin/sh -c` on POSIX and `cmd.exe /c` on Windows — so shell features like pipes and redirection work with each shell's own syntax. Both streams are captured binary-safe up to a combined 64 MiB cap, past which the child is killed so an endless producer cannot exhaust host memory. `code` is the exit status; a child killed by a POSIX signal reports `128 + signal` (a cap overrun is `137`), while a terminated Windows process reports its own termination code.
- `process.env` → a table mapping each environment variable name to its value, captured when the module is required.
- `process.getenv(name, default?)` → the value of `name`, or `default` (or `nil`) when it is unset.
- `process.cwd()` → the current working directory as a string.
- `process.argv` → an array of the script arguments. This is the same data as the global `arg` table, normalized to a 1-based array that drops `arg[0]` (the script path) and the host options, leaving just the arguments your script received.
- `process.available` → `true` when this build can run commands, `false` on the stub builds (iOS, tvOS, watchOS, visionOS, and wasm) where `process.exec` rejects with "not available in this build".

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
