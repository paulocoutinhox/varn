# Comparison benchmark — Varn vs Node vs Python

A fair, apples-to-apples throughput comparison: the **same `/json` route** (serialize
`{"hello":"world"}`) on three runtimes, driven by the **same `wrk` load**.

- **Varn** — `http.createServer`, no middleware (`servers/varn_server.lua`).
- **Node** — raw `http` module, no framework (`servers/node_server.js`).
- **Python** — a raw ASGI app on `uvicorn`, no framework (`servers/asgi_server.py`).

No web framework is used on any side, so the numbers reflect each runtime's HTTP core, not a
library. All three serialize a small JSON object per request.

## Requirements

- `wrk` (load generator): `brew install wrk` or `apt install wrk`.
- `node` and `uvicorn` (`pip install uvicorn`).
- A built Varn binary: `python3 varn.py build`.

## Run

Single core (one process each — the per-instance comparison):

```bash
THREADS=4 CONNECTIONS=200 DURATION=10s bash bench/run.sh
```

All cores (Varn `VARN_WORKERS`, Node `cluster`, uvicorn `--workers`):

```bash
WORKERS=8 THREADS=8 CONNECTIONS=400 DURATION=10s bash bench/run.sh
```

Knobs (env): `THREADS`, `CONNECTIONS`, `DURATION`, `WORKERS`, `PORT_BASE`, `VARN`.

## Reading the results

`req/s` is throughput (higher is better); `lat(avg)`/`lat(p99)` are response latency (lower is
better). Run the load generator on a **different machine** than the servers for accurate
high-throughput numbers — sharing cores with `wrk` caps every runtime at the same ceiling.

## Multi-core note

Varn workers and the Node/uvicorn equivalents all rely on `SO_REUSEPORT`. The **Linux** kernel
load-balances new connections across the workers, so multi-core scaling is near-linear there.
**macOS/BSD** does not distribute accepts evenly, so a `WORKERS>1` run on a Mac understates
every fork-based server — measure multi-core scaling on Linux.
