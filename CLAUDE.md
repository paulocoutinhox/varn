# Varn — project guide for Claude

Varn is an all-in-one application platform: write apps in Lua on a fast C++ core that behaves the same on desktop, mobile, and the browser (WebAssembly). The C++ `modules/` expose native capabilities to Lua via `require`; the Lua `components/` are higher-level libraries built on top of them.

This file is binding. Follow it exactly instead of re-deriving conventions each session.

## Core principles (non-negotiable)

- **No gambiarras, no fallbacks, no dead/legacy code, no backward-compat shims.** Write it the way an experienced product C++ engineer at a top company would, using current best practices.
- **No `else` for unknown cases that create surprising implicit behavior.** Handle the known cases explicitly; do not paper over the unknown.
- **Code and comments are in English.**
- **Do the work only when it genuinely makes sense and is actually needed — never just to show work.**
- **IMPORTANT: never run git commit/push on your own.** Leave the working tree dirty; the user runs git.
- Build with `cmake --build build -j 4` (this machine can freeze with all cores).

## Commands

Everything goes through `python3 varn.py <task>`:

- `build` — build the native `varn` executable (then `./build/bin/varn script.lua` runs a script).
- `test` — run the cross-platform Lua suite (`modules/*/lua/tests/*.lua` plus the backend-free component tests), each test gets a fresh `VARN_TEST_DIR`.
- `test-cpp` — build and run the googletest C++ target.
- `test-db` — start the docker backend services (`tests/docker-compose.yml`: redis, mysql, postgres) and run the integration tests that need a live backend (redis, mysql, vdo over all three drivers, scheduler); `test-db --ai-live` additionally runs the real-api ai smoke test (each provider gated on its `*_API_KEY`), and `test-db --down` tears the services down.
- `format` — run clang-format over `modules/` and `src/`.
- `wasm` / `app-wasm` / `serve` / `site-deploy` — build the wasm engine, bundle the browser app, dev server, publish the site.
- `lib` — build the embeddable `varn` shared library (`find_package(varn)` package); `lib --prefix <dir> --install` installs the `varn` component into a clean prefix.
- Run a single Lua test directly: `./build/bin/varn modules/<mod>/lua/tests/<name>_test.lua` (set `VARN_TEST_DIR` to a scratch dir).

## Architecture and organization

- **Modules** (`modules/<name>/`) are independent C++ units exposed to Lua through `require("<name>")`. Layout per module: `include/varn/<name>/` (public headers), `src/` (impl), `src/drivers/<driver>/` (swappable backends), `lua/examples/`, `lua/tests/`, `README.md`, `<name>.cmake`.
- **Drivers** are backends selected at build time (e.g. `poco`, `std`, `openssl`, `portable`, `emscripten_fetch`, `dummy`). The `dummy` driver throws `"... not available in this build"`. Keep all drivers of a module consistent in behavior and signatures.
- **Runtime** (`modules/core/.../runtime/`): one `EventLoop`, two `TaskPool`s — `taskPool()` (general work) and `ioPool()` (blocking I/O). Blocking work runs on a pool and resolves a `Promise` on the event loop; use `varn::async::AsyncTask::runOnPool(...)` for that pattern. `async` exposes `sleep`, `spawn`, `run`, `promise`, `deferred` plus combinators (`all`, `allSettled`, `race`, `any`, `timeout`, `mapLimit`).
- **Runtime invariants (binding):**
  - A job posted to the loop or a pool must never let an exception escape its boundary — `EventLoop::post`/`postDelayed` and `TaskPool::post` wrap the work so an escaping throw cannot `std::terminate` the process and the `WorkLedger` entry is always released.
  - A secure (TLS) connection drives blocking I/O on the `ioPool`, so its socket fd is released by RAII once the last in-flight pool task drops the connection — never eager-close a secure fd from the loop while a pool op may be in flight (see `PocoStreamConnection::close`).
  - Secure (TLS) reads, writes and the handshake are serialized per connection through a strand (`PocoStreamConnection::enqueueSecure`) so no two operations ever touch the same OpenSSL `SSL` object concurrently on the `ioPool`; overlapping ops on one secure connection run in order, they do not run in parallel.
  - Loop-owned state reachable from the cross-thread public C API (e.g. the `Runtime` server list, since `Runtime::stop()` can be called from another thread) is mutex-guarded.
  - Reading an untrusted path into memory rejects non-regular files (`FsStorage::readAll`) so an endless stream like a device or fifo cannot loop forever.
  - Lua chunks are always loaded text-only (`luaL_loadfilex`/`luaL_loadbufferx` with mode `"t"`) — never enable bytecode loading, since the wasm host runs source supplied by the page.
  - Network output buffers (the WebSocket send queue, streamed responses) are bounded and the connection is dropped when a peer stops draining — never buffer outbound data without a limit.
  - Captured child-process output is bounded and a child that overruns the cap is killed, so `process.exec` on an endless producer cannot exhaust host memory.
  - The worker supervisor restarts a worker only on an abnormal exit and backs off a crash loop, so a fast-failing child can never become a fork storm.
- **Lua is compiled as C++**, so a Lua `error()` unwinds as a C++ exception caught by `pcall`; the wasm build needs `-fexceptions`.
- **Components** (`components/<name>/`) are Lua libraries on top of the modules, loaded with `require`. Layout: `init.lua` (returns the module table), `examples/`, `tests/`, `README.md`. They run server-side on the native runtime (they use `socket`/`http`/`vdo`/etc., unavailable in the browser). Examples: `ai`, `scheduler`, `vdo`, `redis`, `mysql`, `pool`.
- **Targets** (`-DVARN_TARGET=`): `cli` (desktop executable), `wasm` (browser), `apple` (`Varn.xcframework`), `android` (`aar`), and `lib` (an embeddable shared library exporting only the C API, installed as a `find_package(varn)` → `varn::varn` package). The same Lua script runs on all of them.
- **Embedding**: the C ABI is `modules/api/include/varn/varn.h` (`varn_runtime_new`/`register`/`run_file`/`run_string`/`stop`/`free`, `varn_version`). `varn_runtime_register` exposes a native function to Lua under the global `host` table, marshalling the argument and result through json, so a host app drives varn (and reaches native capabilities like UI) without touching Lua types. `python3 varn.py lib --prefix <dir> --install` builds and installs the package; the shared library exports only the C API (a linker script hides every statically linked dependency). See [docs/embedding.md](docs/embedding.md) and [examples/embedding](examples/embedding).

## C++ style and formatting

Match the existing visual, structural, and architectural pattern. Compact, professional, consistent. clang-format is LLVM-based with **Allman braces** (run `python3 varn.py format`).

- Avoid excess vertical space; use only the blank lines needed to separate reading contexts. **Separate blocks of different responsibility with one blank line.**
- Never leave multiple `if`s, validations, loops, state mutations, and returns visually glued together. A function must read with an identifiable beginning, middle, and end at a glance.
- Prefer early returns; avoid unnecessary nesting; **no unnecessary `else` after a `return`.**
- Extract a function only when one is doing too much — never to shrink size or for artificial abstraction that hides the main flow.
- Keep includes clean, direct, organized. Keep headers, implementation, namespaces, types, names, and responsibilities consistent.
- Prefer `const`, references, and smart pointers for clarity, safety, and ownership. Avoid macros, unsafe casts, and raw owning pointers when a safer project-consistent alternative exists.
- **No free functions exposed at namespace scope — group helpers as static methods of a class** (like `varn::lua::LuaHelpers`). The only exceptions are file-local helpers in an anonymous namespace and the C-ABI interop functions that must stay free.
- **Do not put comments in headers describing methods, sections, or members.**
- **Lambdas: clang-format mangles C++ lambdas, so wrap each non-trivial lambda region in `// clang-format off` … `// clang-format on` and hand-format it cleanly.**
- TLS client contexts must point verification at the OS CA bundle (the bundled OpenSSL ships no trust store) — see `resolveCaBundle` in the poco drivers.

## Comments

- **Single-line comments starting with `//` or `#` (and Lua `--`) begin lowercase; normal prose/block comments are written normally.**
- A comment is **one objective line, one complete sentence**, explaining intent or context — never restating what the code literally does.
- **No trailing period on a single-line comment, no colon, and no `;` splitting clauses.** If you genuinely need multiple sentences across lines, finish each line's sentence with punctuation; never continue one sentence onto the next line.
- No obvious, decorative, redundant, or narrative comments. No artificial section banners like `helpers`, `validators`, `public methods`. No historical or before/after framing — describe the current code or remove the comment.
- Usage examples belong in docs, not in comments.
- Each important block of a complex function gets a short objective intent comment.

## Lua and components

- Component lifecycle runs on the event loop, so socket/http/db work must live inside an async coroutine (`async.run` / `async.spawn`); close what you open in the same scope.
- A new component matches the established pattern: `init.lua` returns a table, lazy-`require` heavy drivers, expose a clean API, ship `examples/`, `tests/`, and an objective `README.md` with a capabilities table.

## Testing and docs

- Each capability has runnable examples and individual Lua tests. CI runs `modules/*/lua/tests` on Windows/Linux/macOS plus self-contained component tests (no external server). Tests needing a database or server (redis, mysql, vdo mysql/pgsql, scheduler) run against real backends through `python3 varn.py test-db`, which brings up `tests/docker-compose.yml` and points each test at it — never in the cross-platform runner.
- The vdo mysql/pgsql drivers `dlopen` `libmysqlclient`/`libpq` through ffi, so `test-db` adds the client-library and openssl directories to the loader path (a macOS SIP-restricted intermediary like `/usr/bin/perl` strips `DYLD_*`, so launch the runner directly).
- Module Lua tests under `modules/*/lua/tests/*.lua` are auto-discovered, but a **component** test only runs in CI once it is added to the list in `tools/core/tests.py` — a component test that needs no external backend (e.g. a pure-logic parser) belongs there.
- Native unit tests are plain googletest in `tests/cpp/*.cpp` (auto-globbed into `varn_tests`); the target's include dirs let a test reach a module's internal headers. Concurrency and internal-state invariants (`WorkLedger`, `TaskPool`, `EventLoop`) are exercised with multi-threaded stress tests there.
- Per-library docs live in each module/component `README.md` with a capabilities section; the main `README.md` is presentation only. Keep docs objective and present-tense.
