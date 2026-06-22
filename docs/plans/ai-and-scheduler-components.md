# Mega plan — `ai` and `scheduler` components

This is the working checklist and research record for two proposed additions to Varn:

1. `ai` — a component with complete clients for text and image generation across the major providers (OpenAI, Anthropic Claude, Google Gemini, xAI Grok, DeepSeek, Moonshot/Kimi, and the OpenAI-compatible long tail), with full support for non-streaming responses, streaming, tools/function calling, and each provider's native features.
2. `scheduler` — a durable background-task scheduler (like FastAPI background tasks but resilient): immediate, run-at-datetime, and fixed-interval tasks, with status, full history, and add/remove/query, that survives server restarts and resets killed in-progress tasks back to the queue.

> Sourcing note. The OpenAI section was refreshed against the live developer docs (mid-2026). The Claude, Gemini, OpenAI-compatible, and scheduler-framework sections are written from prior knowledge (cutoff early 2026) because the live web research pass was blocked by API load at planning time. Every provider section carries a **LIVE VERIFY** task — pin exact endpoints, headers, model IDs, and event names against the official docs before implementing that client.

---

## Part 0 — Internal capability audit (done) and the one core change required

What Varn exposes today, with the verdict for each component.

### Available today
- **HTTP client** (`require("http").client`): `client.request(opts)`, `client.get(url, opts)`, `client.post(url, opts)`, plus the raw `requestRaw`. Options: `url`, `method`, `headers`, `body`, `query`, `json` (serializes body + sets content-type), `timeoutSeconds`, `verifyTls`, `insecure`, `maxResponseBytes` (default 64 MiB). Returns a promise resolving to `{ status, ok, headers, body, json() }`. Runs the blocking request on `ioPool` and resolves on the event loop. Files: [HttpClientModule.cpp](../../modules/http/src/HttpClientModule.cpp), [HttpClientPerform.h](../../modules/http/include/varn/http/HttpClientPerform.h).
- **JSON** (`require("json")`): encode/decode, nested objects/arrays, handles base64 as plain strings.
- **base64** (`require("crypto")`): `base64Encode` / `base64Decode` / url-safe variants — needed to write image-generation bytes to disk.
- **fs** (`require("fs")`): `readFile`, `writeFile`, `append`, `exists`, `mkdir`, `stat`, `readdir`, `rename`, `copy`, `removeRecursive`, `open`/handle, `mkdtemp`. Enough for a durable file/jsonl store.
- **async** (`require("async")`): `sleep(ms)`, `spawn(fn)`, `run(fn)`, `promise(fn)`, `deferred()`, plus combinators `all`/`allSettled`/`race`/`any`/`timeout`/`mapLimit`. Backed by `EventLoop::postDelayed(delayMs, job)` ([EventLoop.h](../../modules/core/include/varn/runtime/EventLoop.h)).
- **ffi + sqlite**: a working sqlite3 example exists ([sqlite3.lua](../../modules/ffi/lua/examples/sqlite3.lua)) — sqlite via ffi is viable for a durable, transactional store.
- **Component pattern**: a directory under `components/` with `init.lua` returning a module table, built on `socket`/`async`/`http`/etc. (see `components/redis`, `components/pool`). Long-lived socket work runs inside an `async.spawn`/`async.run` coroutine.
- **Shared async dispatch**: `varn::async::runOnPool(L, rt, pool, tag, work)` ([AsyncTask.cpp](../../modules/async/src/AsyncTask.cpp)) for running blocking work on a pool and settling a promise — the pattern any new C++ surface should reuse.

### Gaps / findings
- ❌ **No streaming on the HTTP client.** `HttpClientPerform::perform` returns the full body as one string (`VARN/1 <status> <len>\n<body>`); there is no per-chunk callback. The emscripten path also resolves a single full body.
- ❌ **Response headers are dropped.** The client wire carries only status + body, so the response `headers` table is always empty. Rate-limit headers, `retry-after`, request-ids, and SSE content-type are not visible to Lua.
- ⚠️ **No repeating-timer primitive**, but a periodic tick is trivially built with `async.spawn` + a loop of `async.sleep(intervalMs):await()`; the pending timer keeps the event loop (and process) alive.

### Verdicts
- **`ai` (non-streaming text + image)** → buildable as a pure Lua component today over `http.client` + `json` + `crypto` + `fs`. No core change needed.
- **`ai` (streaming / SSE token-by-token)** → **requires one new core capability**: a streaming HTTP request that invokes a Lua callback per received chunk and exposes the response status + headers. See Part 3.
- **`scheduler`** → buildable as a pure Lua component today: durable store via ffi+sqlite (preferred) or fs+jsonl, a tick loop via `async.spawn`/`async.sleep`, startup recovery to reclaim orphaned in-progress tasks, named handlers + serializable payloads. No core change strictly required; an optional shutdown/signal hook would make graceful drain nicer (Part 3, optional).

---

## Part 1 — `ai` component

### 1.1 Design decisions
- [ ] One component `components/ai/` exposing `local ai = require("ai")`.
- [ ] Provider drivers behind a normalized interface: `ai.client({ provider, apiKey, model, baseUrl?, headers?, ... })` returns a client with `:generate(req)` (full), `:stream(req, onEvent)` (streaming), `:image(req)` (image generation), and `:embed(req)` if the provider supports it.
- [ ] **Two wire families**, not one: (a) **OpenAI-compatible** Chat Completions covers OpenAI, Grok, DeepSeek, Kimi, Mistral, Groq, Together, OpenRouter, Ollama; (b) **native adapters** for Anthropic Messages and Google Gemini, which are structurally different. The normalized request/response is the public surface; adapters translate to/from each wire.
- [ ] Normalized request: `{ model, messages[], system?, temperature?, maxTokens?, topP?, stop?, tools?, toolChoice?, responseFormat?, stream?, reasoning?, ...providerExtras }`.
- [ ] Normalized message: `{ role = "system"|"user"|"assistant"|"tool", content = string | parts[] }` where a part is `{ type="text", text }`, `{ type="image", url|data, mediaType }`, `{ type="tool_call", id, name, arguments }`, `{ type="tool_result", id, content }`.
- [ ] Normalized response: `{ text, finishReason, toolCalls[], usage = { inputTokens, outputTokens, totalTokens }, raw }`.
- [ ] Streaming yields normalized events: `{ type="text_delta", text }`, `{ type="tool_call_delta", index, id?, name?, argumentsDelta }`, `{ type="reasoning_delta", text }`, `{ type="done", finishReason, usage }`, `{ type="error", message }`.
- [ ] Never hide provider power: expose a `raw`/`extra` passthrough so callers can set provider-specific fields and read provider-specific response data.
- [ ] Secrets come from the caller (or `process.env`); never persisted or logged.

### 1.2 Provider matrix

| Provider | Wire family | Base URL | Auth | Text stream | Tools | Image gen | Native extras |
|---|---|---|---|---|---|---|---|
| OpenAI | Responses + Chat | `https://api.openai.com/v1` | `Authorization: Bearer` | yes (SSE) | yes | yes (gpt-image, dall·e) | reasoning effort, hosted tools, structured outputs |
| Anthropic | Messages (native) | `https://api.anthropic.com` | `x-api-key` + `anthropic-version` | yes (SSE) | yes | **no** | extended thinking, prompt caching, server tools |
| Gemini | generateContent (native) | `https://generativelanguage.googleapis.com` | `x-goog-api-key` / `?key=` | yes (SSE) | yes | yes (Imagen + native) | grounding/search, code exec, thinking |
| xAI Grok | OpenAI-compatible | `https://api.x.ai/v1` | Bearer | yes | yes | yes (`/images/generations`) | live search, reasoning |
| DeepSeek | OpenAI-compatible | `https://api.deepseek.com` | Bearer | yes | yes | no | `reasoning_content` (reasoner) |
| Moonshot/Kimi | OpenAI-compatible | `https://api.moonshot.cn/v1` | Bearer | yes | yes | no | context caching, partial mode |
| Mistral | OpenAI-ish | `https://api.mistral.ai/v1` | Bearer | yes | yes | no | vision (pixtral), OCR |
| Groq | OpenAI-compatible | `https://api.groq.com/openai/v1` | Bearer | yes | yes | no | very low latency |
| Together | OpenAI-compatible | `https://api.together.xyz/v1` | Bearer | yes | yes | yes (FLUX) | wide model catalog |
| OpenRouter | OpenAI-compatible | `https://openrouter.ai/api/v1` | Bearer | yes | yes | varies | `HTTP-Referer`/`X-Title` headers, `vendor/model` ids |
| Ollama (local) | OpenAI-compat + native | `http://localhost:11434` | none | yes | yes | no | local models, `/api/chat` native |

### 1.3 OpenAI (refreshed against live docs, mid-2026)
- Base `https://api.openai.com/v1`; `Authorization: Bearer $KEY`; optional `OpenAI-Organization`, `OpenAI-Project`. `v1` path is the version.
- **Responses API** `POST /v1/responses` is current/recommended; **Chat Completions** `POST /v1/chat/completions` remains fully supported. The Assistants API is being retired (Aug 26, 2026) — do not target it.
- Responses request: `model`, `input` (string or items), `instructions`, `max_output_tokens`, `temperature`, `top_p`, `reasoning:{effort,summary}`, `text:{format,verbosity}`, `tools`, `tool_choice`, `parallel_tool_calls`, `store`, `previous_response_id`, `stream`, `background`, `metadata`. Response: typed `output[]` (`reasoning`, `message` with `content[].output_text`) + flattened `output_text`; usage uses `input_tokens`/`output_tokens`/`total_tokens`.
- Chat request: `messages[]` (`system`/`developer`/`user`/`assistant`/`tool`), `max_completion_tokens` (use this for reasoning models; `max_tokens` legacy), `response_format`, `tools`, `tool_choice`, `parallel_tool_calls`, `seed`, `logprobs`, `stream`, `stream_options:{include_usage}`. Response `choices[].message`; usage uses `prompt_tokens`/`completion_tokens`/`total_tokens`; `finish_reason` ∈ stop/length/tool_calls/content_filter.
- **Streaming**: Chat → `chat.completion.chunk` with `choices[].delta`, terminator literal `data: [DONE]`. Responses → typed semantic events (`response.output_text.delta` with `delta`, `response.function_call_arguments.delta`, terminals `response.completed`/`incomplete`/`failed`), **no `[DONE]`** — ends on a terminal event. The client must handle both terminators.
- **Tools**: Chat nests under `function:{name,description,parameters,strict}`, returns `choices[].message.tool_calls[]` (`function.arguments` is a JSON string), submit results as `{role:"tool",tool_call_id,content}`. Responses flattens `{type:"function",name,description,parameters}`, returns `function_call` items (`call_id`), submit results as `{type:"function_call_output",call_id,output}`. Shared `tool_choice` (auto/none/required/specific) and `parallel_tool_calls`. Responses also has hosted tools (web_search, file_search, code_interpreter, image_generation, computer_use, mcp).
- **Structured output**: Chat `response_format:{type:"json_schema",json_schema:{name,strict,schema}}` (nested); Responses `text:{format:{type:"json_schema",name,strict,schema}}` (flattened). Strict rules: all props in `required`, `additionalProperties:false`, root must be object; optional via `["type","null"]`.
- **Vision**: Chat `{type:"image_url",image_url:{url,detail}}`; Responses `{type:"input_image",image_url:"...",detail}` or `file_id`. URL, `data:` base64, or Files API id. (Re-verify per-request size/image caps live.)
- **Image generation**: `POST /v1/images/generations`. GPT-Image models always return `b64_json` (no `url`, no `response_format`); params `prompt`, `size`, `quality`, `background`, `output_format`, `output_compression`, `moderation`. `dall-e-3`: `n=1`, `size` 1024/1792 variants, `quality` standard/hd, `style`, `response_format` url|b64_json, returns `revised_prompt`. Edits `/v1/images/edits` (multipart, mask = inpainting), variations `/v1/images/variations` (dall-e-2 only).
- **Errors**: `{error:{message,type,param,code}}`; 400/401/403/429/500/503; retry 429/500/503 with backoff and honor `Retry-After`. Rate-limit headers `x-ratelimit-*`.
- **Native extras to expose**: `reasoning.effort` (none/minimal/low/medium/high/xhigh), `reasoning.summary`, `logprobs`/`top_logprobs`, `seed`+`system_fingerprint`, `service_tier` (auto/flex/priority), prompt caching (`prompt_cache_key`), `max_tool_calls`, Responses conversation state (`store`/`previous_response_id`/`background`).
- [ ] LIVE VERIFY at implementation: exact current model IDs for text and image (the family has moved past gpt-5/o3 naming), and the vision size/count caps.

### 1.4 Anthropic Claude — written from prior knowledge, LIVE VERIFY before building
- Base `https://api.anthropic.com`; `POST /v1/messages`. Headers: `x-api-key`, `anthropic-version: 2023-06-01`, `content-type: application/json`, optional `anthropic-beta`.
- Request: `model`, **`max_tokens` (required)**, `messages[]` (`role` user/assistant, `content` string or block array), `system` (top-level string or block array — not a message), `temperature`, `top_p`, `top_k`, `stop_sequences`, `stream`, `tools`, `tool_choice`, `thinking:{type:"enabled",budget_tokens}`, `metadata`.
- Content blocks: `{type:"text",text}`; `{type:"image",source:{type:"base64",media_type,data}}` (also url source); `{type:"document",source:{...}}` (PDF); `{type:"tool_use",id,name,input}`; `{type:"tool_result",tool_use_id,content,is_error}`; `{type:"thinking",thinking,signature}`.
- Response: `{id,type:"message",role:"assistant",model,content:[blocks],stop_reason: end_turn|max_tokens|stop_sequence|tool_use, stop_sequence, usage:{input_tokens,output_tokens,cache_creation_input_tokens,cache_read_input_tokens}}`.
- **Streaming SSE**: `message_start` → (`content_block_start`, `content_block_delta` with `text_delta`/`input_json_delta`/`thinking_delta`/`signature_delta`, `content_block_stop`)* → `message_delta` (stop_reason, usage) → `message_stop`; plus periodic `ping` and `error` events. Each event has an `event:` line and JSON `data:`.
- **Tools**: `tools:[{name,description,input_schema:{type:"object",properties,required}}]`; model emits `tool_use` blocks; reply with a user message containing `tool_result` blocks. `tool_choice:{type:auto|any|tool|none,name?}`. Parallel = multiple `tool_use` blocks. Server tools (web search, computer use, bash, text editor, code execution) are gated by beta headers and specific type names.
- **Extended thinking**: `thinking:{type:"enabled",budget_tokens:N}`; responses include `thinking` blocks (preserve `signature` when replaying); streaming via `thinking_delta`+`signature_delta`.
- **Prompt caching**: `cache_control:{type:"ephemeral"}` on a block; usage reports `cache_creation_input_tokens`/`cache_read_input_tokens`.
- **No image generation** — the `ai.image()` path must reject for Claude and route image gen to a provider that supports it.
- Token counting: `POST /v1/messages/count_tokens`. Errors: `{type:"error",error:{type,message}}` (invalid_request/authentication/rate_limit/overloaded/api error); 429 + `retry-after`; `request-id` header.
- [ ] LIVE VERIFY: current `anthropic-version`, model IDs, server-tool type names + beta header values, any url image source support.

### 1.5 Google Gemini — written from prior knowledge, LIVE VERIFY before building
- Base `https://generativelanguage.googleapis.com`; auth `x-goog-api-key: $KEY` (or `?key=$KEY`). Version path `v1beta` (some endpoints `v1`).
- Text: `POST /v1beta/models/{model}:generateContent`. Streaming: `POST /v1beta/models/{model}:streamGenerateContent?alt=sse` (SSE chunks each `{candidates:[{content:{parts:[{text}]}}]}`).
- Request: `contents:[{role:"user"|"model",parts:[...]}]`, parts ∈ `{text}` / `{inlineData:{mimeType,data}}` / `{fileData:{mimeType,fileUri}}` / `{functionCall:{name,args}}` / `{functionResponse:{name,response}}`. Plus `systemInstruction:{parts:[{text}]}`, `generationConfig:{temperature,maxOutputTokens,topP,topK,stopSequences,responseMimeType,responseSchema,thinkingConfig:{thinkingBudget,includeThoughts}}`, `safetySettings`, `tools:[{functionDeclarations:[...]},{googleSearch:{}},{codeExecution:{}}]`, `toolConfig:{functionCallingConfig:{mode:AUTO|ANY|NONE,allowedFunctionNames}}`.
- Response: `{candidates:[{content:{parts,role},finishReason,safetyRatings,groundingMetadata,citationMetadata}],usageMetadata:{promptTokenCount,candidatesTokenCount,totalTokenCount,thoughtsTokenCount},modelVersion}`.
- **Function calling**: model returns `functionCall` parts; reply with `functionResponse` parts. Modes via `functionCallingConfig`.
- **Structured output**: `generationConfig.responseMimeType="application/json"` + `responseSchema`.
- **Multimodal**: `inlineData` base64 for image/audio/video; large files via Files API `POST /upload/v1beta/files` then reference by `fileData.fileUri`.
- **Image generation**: (a) Imagen — `POST /v1beta/models/{imagen-model}:predict` with `{instances:[{prompt}],parameters:{sampleCount,aspectRatio,personGeneration}}` → `predictions:[{bytesBase64Encoded,mimeType}]`; (b) Gemini native image — `generateContent` with an image-capable model and `responseModalities:["TEXT","IMAGE"]`, image returned as an `inlineData` part.
- Errors: `{error:{code,message,status}}`.
- [ ] LIVE VERIFY: current text/image model IDs, `thinkingConfig` shape, native image-gen request shape, `v1` vs `v1beta` per endpoint.

### 1.6 OpenAI-compatible providers — prior knowledge, LIVE VERIFY base URLs + model IDs
- **Common shape**: `base_url` + `Authorization: Bearer $KEY` + `POST /chat/completions` with `{model,messages,tools,tool_choice,response_format,stream,stream_options}`; streaming = `chat.completion.chunk` + `data: [DONE]`. One adapter covers all of these.
- **Per-provider deviations the client must handle**:
  - **Grok** (`https://api.x.ai/v1`): chat compatible; image gen via `/v1/images/generations` (grok image model); live-search params; reasoning models.
  - **DeepSeek** (`https://api.deepseek.com`, also `/v1`): `deepseek-chat` vs `deepseek-reasoner`; reasoner adds `reasoning_content` on both message and streaming delta — surface as `reasoning_delta`. No image gen.
  - **Moonshot/Kimi** (`https://api.moonshot.cn/v1`; also an Anthropic-compatible endpoint): context caching; partial/prefix mode; `moonshot-v1-*` and `kimi-*` models. No image gen.
  - **Mistral** (`https://api.mistral.ai/v1`): chat + tools + vision (pixtral) + document OCR; mostly OpenAI-shaped with minor differences. No general image gen.
  - **Groq** (`https://api.groq.com/openai/v1`): straight OpenAI-compatible, very fast.
  - **Together** (`https://api.together.xyz/v1`): chat compatible + image gen `/v1/images/generations` (FLUX models).
  - **OpenRouter** (`https://openrouter.ai/api/v1`): aggregator; `vendor/model` ids; optional `HTTP-Referer` + `X-Title` headers; per-model capability varies.
  - **Ollama** (`http://localhost:11434`): OpenAI-compatible `/v1/chat/completions` and native `/api/chat`; no auth by default; local models.
- [ ] LIVE VERIFY: each base URL, current model IDs, and whether `reasoning_content`/image endpoints still match.

### 1.7 Unified abstraction tasks
- [ ] Define the normalized request/message/response/stream-event tables (1.1) and document them.
- [ ] Adapter: **OpenAI Chat Completions** (covers OpenAI-chat, Grok, DeepSeek, Kimi, Mistral, Groq, Together, OpenRouter, Ollama).
- [ ] Adapter: **OpenAI Responses** (optional, for OpenAI hosted tools + reasoning).
- [ ] Adapter: **Anthropic Messages**.
- [ ] Adapter: **Gemini generateContent**.
- [ ] Tool-calling normalization across all four families (request tool schema, model tool-call extraction, result submission).
- [ ] Image generation: `ai.image({provider, model, prompt, size, ...})` → normalized `{images=[{b64|url, mediaType}], raw}`; helper `ai.saveImage(result, path)` using crypto.base64Decode + fs.writeFile.
- [ ] Provider registry with per-provider defaults (base URL, auth header name/scheme, extra headers, model family).
- [ ] Robust error handling: map provider error envelopes to a uniform error; retry 429/5xx with exponential backoff; honor `Retry-After` once headers are available (Part 3).

### 1.8 Streaming (blocked on Part 3 core change)
- [ ] After the core streaming HTTP API lands, build the SSE line parser (split on `\n`, accumulate `event:`/`data:` until blank line, handle multi-line `data:`).
- [ ] Per-family event decoding: OpenAI Chat (`delta` + `[DONE]`), OpenAI Responses (semantic events, terminal event), Anthropic (`content_block_delta` etc.), Gemini (`?alt=sse` chunks).
- [ ] Surface incremental tool-call argument assembly and reasoning deltas.
- [ ] Backpressure / cancellation: allow the caller to stop a stream early.

### 1.9 `ai` deliverables
- [ ] `components/ai/init.lua` + adapters.
- [ ] Examples per capability: text (full), text (stream), tools/function-calling round trip, vision input, structured output, image generation + save, reasoning/thinking, one OpenAI-compatible provider via base-url override.
- [ ] Tests: a mockable transport (inject a fake `http.client`/streaming transport) so providers are tested without network; unit tests for each adapter's request build + response/stream parse; an opt-in live smoke test gated on env keys (skipped in CI).
- [ ] `components/ai/README.md` with the capability table + per-provider notes (objective, no history, comment style per project rules).

---

## Part 2 — `scheduler` component

### 2.1 Design (synthesized from Celery / RQ / BullMQ / Sidekiq / APScheduler)
- [ ] Single-process, durable, named-handler scheduler. Public surface `local scheduler = require("scheduler")`.
- [ ] **Named handlers, serializable payloads** (the key constraint borrowed from every durable queue): closures cannot be persisted, so a task stores a handler *name* + a JSON payload. `scheduler.handler("send_email", function(payload, ctx) ... end)` registers a handler at startup; tasks reference it by name.
- [ ] Triggers: `scheduler.enqueue(name, payload, opts)` (immediate), `scheduler.schedule(name, payload, runAt, opts)` (datetime), `scheduler.every(name, payload, intervalMs, opts)` (fixed interval). `opts`: `id?`, `priority?`, `maxAttempts?`, `backoff?`, `unique?`.
- [ ] Management: `scheduler.cancel(id)`, `scheduler.remove(id)`, `scheduler.get(id)` → status + history, `scheduler.list(filter)`, `scheduler.pause()`/`resume()`.
- [ ] `scheduler.start()` boots the tick loop and runs recovery; idempotent.

### 2.2 Persistence schema (sqlite via ffi preferred; fs+jsonl fallback)
- [ ] `tasks` table: `id` (text pk), `name`, `payload` (json text), `state`, `priority` (int), `run_at` (epoch ms), `interval_ms` (nullable, for repeating), `attempts` (int), `max_attempts` (int), `backoff` (json/text), `created_at`, `updated_at`, `lease_epoch` (text/int, the boot id that owns a running task), `last_error` (text), `result` (json text).
- [ ] `runs` table (history): `id` pk, `task_id`, `attempt`, `started_at`, `finished_at`, `state`, `error`, `result`. Retain N most recent per task or a global cap; expose pruning.
- [ ] Index on `(state, run_at)` for efficient due-task polling. Use a transaction for every state transition. (sqlite gives atomic claim + queryable history; jsonl needs an append-only log + periodic compaction and is the fallback when ffi/sqlite is unavailable.)

### 2.3 State machine
- States: `queued` (ready now), `scheduled` (future `run_at`), `running`, `success`, `failed`, `retrying`, `cancelled`.
- Legal transitions: scheduled→queued (when due) ; queued→running (claim) ; running→success ; running→retrying→queued (attempt < max, after backoff) ; running→failed (attempt ≥ max) ; running→queued (orphan reclaim on restart) ; {queued,scheduled,retrying}→cancelled. A repeating task on success re-arms a new `scheduled` row at `now + interval_ms`.
- [ ] Implement the transition function with a single sqlite UPDATE guarded by the expected current state (compare-and-set) so a claim is atomic.

### 2.4 Crash recovery / resilience (the core requirement)
- [ ] On `start()`, generate a fresh **boot epoch** (e.g. a uuid or monotonic id). Any task found in `running` with a `lease_epoch` ≠ current boot epoch was orphaned by a kill/restart → reset to `queued` (attempts unchanged or incremented per policy). This is the single-process equivalent of a visibility-timeout reaper and satisfies "a task running when the process is killed returns to queued".
- [ ] Claim writes `state=running, lease_epoch=<current boot>, started_at=now` in one transaction; only the owner finishes it.
- [ ] Optional in-run reaper: a `heartbeat_at` updated periodically for long tasks + a `stuck_running_timeout` that reclaims a task whose heartbeat went stale even without a restart (guards against a hung handler). Document the at-least-once contract → **handlers should be idempotent**.
- [ ] Recovery rebuilds the full in-memory view from the store on boot, so the list survives restarts.

### 2.5 Scheduling triggers + polling
- [ ] Tick loop: `async.spawn(function() while running do tick(); async.sleep(pollMs):await() end end)`. Default poll ~200–1000 ms; the pending sleep keeps the process alive.
- [ ] Each tick: promote due `scheduled`→`queued` (`run_at <= now`), then claim and dispatch ready `queued` tasks up to a concurrency limit.
- [ ] Misfire/coalesce after downtime: when many interval runs were missed while down, coalesce to a single run and re-arm from `now` (APScheduler-style `coalesce` + `misfire_grace_time`), configurable per task.
- [ ] Run the handler body off the event-loop thread when it blocks (wrap in `runOnPool`/ioPool) so the tick loop stays responsive; capture result/error to settle the task state on the event loop.

### 2.6 Worker model + concurrency
- [ ] Configurable max concurrent running tasks; a claimed task counts against the limit until it settles.
- [ ] Per-task `priority` ordering within the due set.
- [ ] Retry with exponential backoff + jitter; `max_attempts`; terminal `failed` records `last_error` and full `runs` history.

### 2.7 What it must NOT do
- ❌ Persist arbitrary closures/functions — only `{handler name + JSON payload}`.
- ❌ Assume multiple worker processes without leases — this design is single-process; multi-process would need real leases/heartbeats with TTLs (note as a future extension).
- ❌ Block the event-loop thread with handler work.
- ❌ Silently swallow handler errors — record them and transition state.

### 2.8 `scheduler` deliverables
- [ ] `components/scheduler/init.lua` (+ a storage driver split: `sqlite` and `jsonl`).
- [ ] Examples: immediate task, run-at-datetime, every-interval, cancel/remove, query status + history, retry-on-error, and a "kill the process mid-task and watch it return to queued on restart" demo.
- [ ] Tests: state-machine transitions, due-task selection, **crash recovery** (simulate an orphaned `running` row from a previous epoch → asserted reset to `queued`), retry/backoff, interval re-arm + coalescing, cancel/remove, history retention/pruning. Use a temp dir / in-memory sqlite for determinism.
- [ ] `components/scheduler/README.md` (capability table, durability guarantees, the named-handler + idempotency contract).

---

## Part 3 — Core prerequisite: streaming HTTP client (required for `ai` streaming)

The only piece neither component can do today. Scope it as a focused core change.

- [ ] Add a streaming request path to the HTTP client that delivers the response incrementally to Lua: e.g. `http.client.stream(opts, onChunk)` returning a promise that resolves when the stream ends, where `onChunk(chunk)` is invoked on the event loop per received body chunk.
- [ ] Expose **response status and headers** at stream start (and ideally for the non-streaming path too), so the SSE content-type, rate-limit headers, and `retry-after` become visible to Lua.
- [ ] Implement in the Poco client exchange (read the response stream in chunks on `ioPool`, marshal each chunk to the event loop via the runtime — reuse the `runOnPool`/promise plumbing pattern) and in the emscripten fetch driver (incremental fetch / streaming reader).
- [ ] Keep `maxResponseBytes` and timeout semantics; allow cancellation to stop reading.
- [ ] Tests for the streaming client against a local chunked/SSE server (the socket/http test harness can host one), Windows/Linux/macOS in CI.
- [ ] Follow project comment/format rules; wrap any lambdas in `// clang-format off/on`.

> Optional (nice-to-have, not required): a startup/shutdown lifecycle hook a component can register, so the scheduler can drain gracefully on SIGTERM. Without it, durability + boot-time recovery already cover the kill case.

---

## Part 4 — Cross-cutting

- [ ] Both components follow the established `components/` pattern (dir + `init.lua`), built on `http`/`json`/`crypto`/`fs`/`async`/`ffi`.
- [ ] Comment + doc style per project rules: single-line lowercase `//`/`--` comments, no trailing period/colon, no `;`-split, English, intent-not-restatement, no header member comments, objective READMEs with a capabilities section, usage examples in docs not comments.
- [ ] Per-capability examples + individual tests, wired into the test runner; live-network tests gated on env keys and skipped in CI.
- [ ] Decide storage default for `scheduler` (sqlite-via-ffi vs jsonl) and document the trade-off.
- [ ] Update the playground/site examples once the components stabilize (an `ai` text demo is a strong showcase; note wasm cannot reach provider APIs from the browser without a proxy/keys, so keep live-AI demos server-side).

---

## Open decisions for the user
1. **`ai` streaming now or later?** Non-streaming text + image ships as a pure component immediately; streaming needs the Part 3 core change. Build non-streaming first, then streaming?
2. **Scheduler store**: default to **sqlite via ffi** (atomic claims, queryable history, best resilience) or **fs+jsonl** (zero deps, simpler, needs compaction)? Recommendation: sqlite default, jsonl fallback.
3. **Provider breadth for v1 of `ai`**: ship OpenAI + Claude + Gemini + the OpenAI-compatible adapter (covers Grok/DeepSeek/Kimi/Groq/Together/OpenRouter/Ollama) first, then native extras?
4. **Live research refresh**: the Claude/Gemini/compatible/scheduler sections need the LIVE VERIFY pass (blocked by API load now) before coding those adapters — rerun the web research when ready?
