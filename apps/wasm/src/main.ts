import "./style.css";

import type { MainToWorker, RunResult, WorkerToMain } from "./worker";

const DEFAULT_LUA = `local function fib(n)
  if n < 2 then
    return n
  end
  return fib(n - 1) + fib(n - 2)
end

print("fib(12) =", fib(12))

for i = 1, 4 do
  print(string.format("loop %d", i))
end
`;

// runnable examples tuned for the browser build, omitting crypto and ffi which are unavailable in wasm.
const EXAMPLES: ReadonlyArray<{ label: string; code: string }> = [
  { label: "Lua — fibonacci", code: DEFAULT_LUA },
  {
    label: "Lua — strings & patterns",
    code: `local text = "varn: fast, small, embeddable"

-- split the sentence into words with a pattern
for word in text:gmatch("%a+") do
  print(word)
end

print("upper:", text:upper())
print("commas:", select(2, text:gsub(",", "")))
`,
  },
  {
    label: "Lua — tables & iteration",
    code: `local fruits = { "apple", "banana", "cherry" }

-- ordered iteration over an array
for index, name in ipairs(fruits) do
  print(index, name)
end

local counts = { apple = 3, banana = 5 }
counts.cherry = 1

-- keyed iteration over a map
for name, n in pairs(counts) do
  print(name, "=", n)
end
`,
  },
  {
    label: "json — encode & decode",
    code: `local json = require("json")

local text = json.encode({ name = "varn", tags = { "fast", "small" }, version = 1 })
print("encoded:", text)

local value = json.decode(text)
print("name:", value.name)
print("first tag:", value.tags[1])

print("pretty:")
print(json.encode({ user = { id = 1, roles = { "admin" } } }, { pretty = true }))
`,
  },
  {
    label: "xml — encode & decode",
    code: `local xml = require("xml")

-- build a document from the node model
local doc = xml.encode({
  name = "note",
  attributes = { priority = "high" },
  children = {
    { name = "to", text = "Lua" },
    { name = "from", text = "C++" },
  },
}, { pretty = true })
print(doc)

-- parse it back into the same node model
local node = xml.decode(doc)
print("root:", node.name)
print("priority:", node.attributes.priority)
print("first child:", node.children[1].name, node.children[1].text)
`,
  },
  {
    label: "platform — system info",
    code: `local p = require("platform")

print("os", p.os())
print("arch", p.arch())
print("cpus", p.cpuCount())
print("pointer bytes", p.pointerSize())
print("endianness", p.endianness())
print("host version", p.hostVersion())
`,
  },
  {
    label: "log — levels",
    code: `local log = require("log")

log.debug("debug", 1)
log.info("info", "hello")
log.warn("careful")
log.error("boom", { code = 7 })

print("log lines were emitted")
`,
  },
  {
    label: "async — sleep & spawn",
    code: `local async = require("async")

print("start")
async.spawn(function()
  print("task started")
  async.sleep(1500):await()
  print("task woke up after 1500ms")
end)
print("main chunk returned first")
`,
  },
  {
    label: "fs — read & write (MEMFS)",
    code: `local async = require("async")
local fs = require("fs")

async.spawn(function()
  fs.writeFile("demo.txt", "hello from lua\\n"):await()

  local content = fs.readFile("demo.txt"):await()
  print("read back:", content)
end)
`,
  },
  {
    label: "zip — create & list",
    code: `local async = require("async")
local zip = require("zip")

async.spawn(function()
  -- write a source file into MEMFS
  local file = io.open("hello.txt", "w")
  file:write("zipped!\\n")
  file:close()

  local _, err = zip.create("demo.zip", { { file = "hello.txt", entry = "a/hello.txt" } }):await()
  if err then
    print("zip error:", err)
    return
  end

  local entries = zip.list("demo.zip"):await()
  print("entries:", table.concat(entries, ", "))
end)
`,
  },
  {
    label: "pcall — error handling",
    code: `-- pcall turns a runtime error into a value you can inspect
local ok, err = pcall(function()
  error("something went wrong")
end)
print("ok:", ok)
print("err:", err)

local divided, result = pcall(function(a, b)
  return a / b
end, 10, 2)
print("divided:", divided, "result:", result)
`,
  },
  {
    label: "http.client — fetch",
    code: `local async = require("async")
local http = require("http")

async.spawn(function()
  local resp, err = http.client.get("https://httpbin.org/get"):await()
  if err then
    print("request failed:", err)
    return
  end

  print("status:", resp.status)
end)
`,
  },
];

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  node.className = className;
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function mount(): void {
  const root = document.querySelector<HTMLDivElement>("#app");
  if (!root) {
    return;
  }

  root.className =
    "min-h-dvh bg-zinc-950 text-zinc-100 selection:bg-cyan-500/30 selection:text-cyan-50";

  const shell = el("div", "mx-auto flex max-w-5xl flex-col gap-6 px-4 py-8 md:px-8");
  const header = el("header", "space-y-2");
  header.appendChild(el("p", "text-xs font-semibold uppercase tracking-[0.2em] text-cyan-400/90", "Varn"));
  header.appendChild(
    el("h1", "text-2xl font-semibold tracking-tight text-white md:text-3xl", "WebAssembly"),
  );
  header.appendChild(
    el(
      "p",
      "max-w-2xl text-sm leading-relaxed text-zinc-400",
      "Runs Lua in a Web Worker with Asyncify so you can stop long loops. Output is captured from print().",
    ),
  );

  const grid = el("div", "grid gap-6 lg:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]");
  const editorCard = el("section", "flex flex-col gap-3 rounded-2xl border border-zinc-800/80 bg-zinc-900/40 p-4 shadow-xl shadow-black/40 backdrop-blur");
  editorCard.appendChild(el("h2", "text-sm font-medium text-zinc-300", "Editor"));

  const exampleRow = el("div", "flex items-center gap-2");
  exampleRow.appendChild(el("label", "text-xs font-medium text-zinc-400", "Example"));
  const exampleSelect = document.createElement("select");
  exampleSelect.className =
    "flex-1 rounded-lg border border-zinc-800 bg-zinc-950/80 px-2 py-1.5 text-sm text-zinc-200 outline-none transition focus:border-cyan-600/60";
  for (const ex of EXAMPLES) {
    const opt = document.createElement("option");
    opt.value = ex.label;
    opt.textContent = ex.label;
    exampleSelect.appendChild(opt);
  }
  exampleRow.appendChild(exampleSelect);
  editorCard.appendChild(exampleRow);

  const textarea = document.createElement("textarea");
  textarea.rows = 16;
  textarea.spellcheck = false;
  textarea.autocomplete = "off";
  textarea.className =
    "min-h-[220px] w-full resize-y rounded-xl border border-zinc-800 bg-zinc-950/80 px-3 py-2 font-mono text-sm leading-relaxed text-cyan-50 outline-none ring-cyan-500/40 transition focus:border-cyan-600/60 focus:ring-2";
  textarea.value = DEFAULT_LUA;

  exampleSelect.addEventListener("change", () => {
    const chosen = EXAMPLES.find((ex) => ex.label === exampleSelect.value);
    if (chosen) {
      textarea.value = chosen.code;
    }
  });

  const toolbar = el("div", "flex flex-wrap gap-2");
  const btnPrimary =
    "inline-flex items-center justify-center rounded-lg bg-cyan-500 px-4 py-2 text-sm font-semibold text-zinc-950 shadow-lg shadow-cyan-500/25 transition hover:bg-cyan-400 focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-cyan-300 disabled:cursor-not-allowed disabled:opacity-40";
  const btnMuted =
    "inline-flex items-center justify-center rounded-lg border border-zinc-700 bg-zinc-900 px-4 py-2 text-sm font-medium text-zinc-200 transition hover:border-zinc-500 hover:bg-zinc-800 focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-zinc-400 disabled:cursor-not-allowed disabled:opacity-40";
  const runBtn = el("button", btnPrimary, "Run");
  const stopBtn = el("button", btnMuted, "Stop");
  const clearBtn = el("button", btnMuted, "Clear output");
  runBtn.type = "button";
  stopBtn.type = "button";
  clearBtn.type = "button";

  editorCard.appendChild(textarea);
  toolbar.appendChild(runBtn);
  toolbar.appendChild(stopBtn);
  toolbar.appendChild(clearBtn);
  editorCard.appendChild(toolbar);

  const outCard = el("section", "flex min-h-[320px] flex-col gap-3 rounded-2xl border border-zinc-800/80 bg-zinc-900/40 p-4 shadow-xl shadow-black/40 backdrop-blur");
  outCard.appendChild(el("h2", "text-sm font-medium text-zinc-300", "Console"));
  const consolePre = document.createElement("pre");
  consolePre.className =
    "min-h-[220px] flex-1 overflow-auto rounded-xl border border-zinc-800 bg-black/50 p-3 font-mono text-xs leading-relaxed text-emerald-200/95";
  consolePre.textContent = "Ready.\n";
  const status = el("p", "text-xs text-zinc-500", "Worker idle.");

  outCard.appendChild(consolePre);
  outCard.appendChild(status);

  grid.appendChild(editorCard);
  grid.appendChild(outCard);
  shell.appendChild(header);
  shell.appendChild(grid);
  root.appendChild(shell);

  const appendLine = (line: string) => {
    consolePre.textContent += line.endsWith("\n") ? line : `${line}\n`;
    consolePre.scrollTop = consolePre.scrollHeight;
  };

  let worker: Worker;
  let ready = false;

  const post = (msg: MainToWorker) => {
    worker.postMessage(msg);
  };

  const onMessage = (ev: MessageEvent<WorkerToMain>) => {
    const msg = ev.data;
    if (msg.type === "ready") {
      ready = true;
      status.textContent = "Worker ready.";
      runBtn.disabled = false;
      return;
    }
    if (msg.type === "done") {
      const r: RunResult = msg.result;
      if (r.output) {
        appendLine(r.output.trimEnd());
      }
      if (!r.ok && r.error) {
        appendLine(`error: ${r.error}`);
      }
      status.textContent = r.ok ? "Finished." : "Finished with errors.";
      runBtn.disabled = false;
      return;
    }
    if (msg.type === "error") {
      appendLine(`worker: ${msg.message}`);
      status.textContent = "Error.";
      runBtn.disabled = false;
    }
  };

  const onError = (e: ErrorEvent) => {
    appendLine(`worker fault: ${e.message}`);
    status.textContent = "Worker fault.";
    runBtn.disabled = false;
  };

  const spawnWorker = () => {
    worker = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
    worker.onmessage = onMessage;
    worker.onerror = onError;
    ready = false;
    runBtn.disabled = true;
    post({ type: "init" });
  };

  spawnWorker();

  runBtn.addEventListener("click", () => {
    if (!ready) {
      appendLine("worker: still loading wasm…");
      return;
    }
    runBtn.disabled = true;
    status.textContent = "Running…";
    post({ type: "run", source: textarea.value });
  });

  stopBtn.addEventListener("click", () => {
    // hard stop terminates the worker so a sleeping or runaway chunk ends immediately, then spawns a fresh worker so the run button becomes available again.
    worker.terminate();
    appendLine("stopped.");
    status.textContent = "Stopped.";
    spawnWorker();
  });

  clearBtn.addEventListener("click", () => {
    consolePre.textContent = "";
    status.textContent = "Console cleared.";
  });
}

mount();
