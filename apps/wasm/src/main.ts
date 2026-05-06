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

  const textarea = document.createElement("textarea");
  textarea.rows = 16;
  textarea.spellcheck = false;
  textarea.autocomplete = "off";
  textarea.className =
    "min-h-[220px] w-full resize-y rounded-xl border border-zinc-800 bg-zinc-950/80 px-3 py-2 font-mono text-sm leading-relaxed text-cyan-50 outline-none ring-cyan-500/40 transition focus:border-cyan-600/60 focus:ring-2";
  textarea.value = DEFAULT_LUA;

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

  const worker = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
  let ready = false;

  const post = (msg: MainToWorker) => {
    worker.postMessage(msg);
  };

  worker.onmessage = (ev: MessageEvent<WorkerToMain>) => {
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

  worker.onerror = (e) => {
    appendLine(`worker fault: ${e.message}`);
    status.textContent = "Worker fault.";
    runBtn.disabled = false;
  };

  post({ type: "init" });
  runBtn.disabled = true;

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
    post({ type: "stop" });
    status.textContent = "Stop requested (hook will abort Lua).";
  });

  clearBtn.addEventListener("click", () => {
    consolePre.textContent = "";
    status.textContent = "Console cleared.";
  });
}

mount();
