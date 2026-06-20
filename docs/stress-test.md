# 🔥 Stress testing with k6

[k6](https://k6.io) drives load against a running Varn HTTP server so you can measure throughput, latency, and where it starts to degrade. Varn does not bundle k6: you run it as a separate client against a server you start yourself.

## 📦 Install k6

```bash
# macOS
brew install k6

# linux (debian/ubuntu)
sudo gpg -k && sudo gpg --no-default-keyring --keyring /usr/share/keyrings/k6-archive-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D69
echo "deb [signed-by=/usr/share/keyrings/k6-archive-keyring.gpg] https://dl.k6.io/deb stable main" | sudo tee /etc/apt/sources.list.d/k6.list
sudo apt-get update && sudo apt-get install k6

# windows
winget install k6 --source winget
```

## 🚀 Start a server to test

Build Varn and run one of the HTTP examples. `server_demo.lua` exposes a few routes that exercise different paths through the server.

```bash
python3 varn.py build
VARN_PORT=3000 ./build/bin/varn modules/http/lua/examples/server_demo.lua
```

It listens on `http://localhost:3000` with these routes:

| Route | Method | What it does |
|-------|--------|--------------|
| `/api/hello?name=...` | GET | json reply after a small simulated delay |
| `/api/echo` | POST | echoes the posted body back as json |
| `/api/file` | GET | reads a file from disk and returns its sha256 |
| `/` and static paths | GET | static files from `apps/lua/public` |

## ✍️ Write the test

Save this as `stress-test.js`. It ramps the virtual user count up to a sustained load, pushes past it, then recovers, and fails the run if errors or latency cross the thresholds.

```javascript
import http from "k6/http";
import { check, sleep } from "k6";

// base url of a running varn server, override with: k6 run -e BASE_URL=http://host:port stress-test.js
const BASE_URL = __ENV.BASE_URL || "http://localhost:3000";

export const options = {
    stages: [
        { duration: "30s", target: 50 },  // warm up to 50 virtual users
        { duration: "1m", target: 200 },  // ramp to the target load
        { duration: "2m", target: 200 },  // hold the target load
        { duration: "1m", target: 500 },  // stress beyond the target
        { duration: "30s", target: 0 },   // ramp down
    ],
    thresholds: {
        http_req_failed: ["rate<0.01"],                   // under 1% of requests may fail
        http_req_duration: ["p(95)<500", "p(99)<1000"],   // 95th and 99th percentile latency budgets
    },
};

export default function () {
    // a json read endpoint with a small simulated delay.
    const hello = http.get(`${BASE_URL}/api/hello?name=k6`);
    check(hello, {
        "hello is 200": (r) => r.status === 200,
        "hello is json": (r) => (r.headers["Content-Type"] || "").includes("application/json"),
    });

    // a write endpoint that echoes the posted body back.
    const payload = JSON.stringify({ ping: Date.now() });
    const echo = http.post(`${BASE_URL}/api/echo`, payload, {
        headers: { "Content-Type": "application/json" },
    });
    check(echo, { "echo is 200": (r) => r.status === 200 });

    sleep(1);
}
```

## ▶️ Run it

```bash
k6 run stress-test.js

# point at a remote server
k6 run -e BASE_URL=http://192.168.0.10:3000 stress-test.js

# quick smoke check before a full run
k6 run --vus 5 --duration 30s stress-test.js
```

## 📊 Read the results

k6 prints a summary at the end. The metrics that matter most for a stress test:

| Metric | Meaning |
|--------|---------|
| `http_reqs` | total requests and requests per second |
| `http_req_duration` | response time, watch the `p(95)` and `p(99)` percentiles rather than the average |
| `http_req_failed` | share of failed requests, the first sign the server is saturated |
| `vus` | active virtual users at the moment the summary was taken |

A `THRESHOLD` line marked failed means the server crossed a budget defined in `options.thresholds`, and `k6` exits non-zero so it fails a CI step.

## ⚙️ Tune the server for load

The server runs one event loop per process, so the main scaling knob is the number of
processes. Set `VARN_WORKERS` to the core count to spread connections across all cores:

```bash
VARN_WORKERS=8 ./build/bin/varn your_server.lua
```

`listen` options shape how a single worker absorbs concurrency. Raise them when the test shows
queuing or rejected connections.

```lua
server:listen({
    host = "0.0.0.0",
    port = 3000,
    maxQueued = 1024,             -- accept backlog before new connections are refused
    requestTimeoutMs = 15000,     -- per-request deadline
    keepAliveTimeoutSeconds = 30, -- idle and slow-connection reaping
})
```

| Knob | Effect under load |
|------|-------------------|
| `VARN_WORKERS` | one process per core; on Linux the kernel load-balances connections across them |
| `maxQueued` | longer accept backlog before new connections are refused |
| `requestTimeoutMs` | bounds how long a slow request stays open |
| `keepAliveTimeoutSeconds` | closes idle keep-alive and slow-read connections |

## 💡 Tips

- Run k6 from a different machine than the server so the load generator does not compete for the same CPU.
- Raise the open-file limit on the client (`ulimit -n`) when driving thousands of virtual users.
- Test one route per scenario when you need to attribute latency, then combine them for a realistic mix.
- For HTTPS, point `BASE_URL` at the `https://` server from `https_json_server.lua` and pass `--insecure-skip-tls-verify` for a self-signed certificate.
