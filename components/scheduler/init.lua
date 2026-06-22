-- durable background task scheduler over the vdo store, where tasks reference a named handler plus a json payload so the queue survives a restart, a task left running when the process dies returns to the queue on the next start, and the tick loop and handlers run on the event loop so the whole lifecycle must live inside an async coroutine (async.run/async.spawn)
local vdo = require("vdo")
local async = require("async")
local crypto = require("crypto")
local json = require("json")

local scheduler = {}

local Scheduler = {}
Scheduler.__index = Scheduler

local SCHEMA = {
    [[CREATE TABLE IF NOT EXISTS scheduler_tasks (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        payload TEXT,
        state TEXT NOT NULL,
        priority INTEGER NOT NULL DEFAULT 0,
        run_at INTEGER NOT NULL,
        interval_seconds INTEGER,
        attempts INTEGER NOT NULL DEFAULT 0,
        max_attempts INTEGER NOT NULL DEFAULT 1,
        lease_epoch TEXT,
        last_error TEXT,
        result TEXT,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL
    )]],
    [[CREATE TABLE IF NOT EXISTS scheduler_runs (
        id TEXT PRIMARY KEY,
        task_id TEXT NOT NULL,
        attempt INTEGER NOT NULL,
        state TEXT NOT NULL,
        started_at INTEGER NOT NULL,
        finished_at INTEGER,
        error TEXT,
        result TEXT
    )]],
    [[CREATE INDEX IF NOT EXISTS scheduler_tasks_ready ON scheduler_tasks (state, run_at)]],
}

local function encode(value)
    if value == nil then
        return nil
    end

    return json.encode(value)
end

local function decode(text)
    if text == nil then
        return nil
    end

    return json.decode(text)
end

function scheduler.new(config)
    config = config or {}

    local instance = setmetatable({
        db = vdo.connect(config.dsn or "sqlite:scheduler.db"),
        handlers = {},
        running = false,
        paused = false,
        active = 0,
        concurrency = config.concurrency or 4,
        pollMs = config.pollMs or 1000,
        backoffSeconds = config.backoffSeconds or 1,
        maxBackoffSeconds = config.maxBackoffSeconds or 300,
    }, Scheduler)

    for _, statement in ipairs(SCHEMA) do
        instance.db:exec(statement)
    end

    return instance
end

function Scheduler:_query(sql, params)
    local statement = self.db:prepare(sql)
    statement:execute(params or {})
    local rows = statement:fetchAll()
    statement:close()
    return rows
end

function Scheduler:_exec(sql, params)
    local statement = self.db:prepare(sql)
    statement:execute(params or {})
    local affected = statement:rowCount()
    statement:close()
    return affected
end

function Scheduler:handler(name, fn)
    self.handlers[name] = fn
    return self
end

function Scheduler:_insert(name, payload, opts, state, runAt, intervalSeconds)
    opts = opts or {}
    local id = opts.id or crypto.uuidV7()
    local now = os.time()

    self:_exec(
        [[INSERT INTO scheduler_tasks (id, name, payload, state, priority, run_at, interval_seconds, attempts, max_attempts, created_at, updated_at)
          VALUES (:id, :name, :payload, :state, :priority, :run_at, :interval, 0, :max, :now, :now)]],
        {
            id = id,
            name = name,
            payload = encode(payload),
            state = state,
            priority = opts.priority or 0,
            run_at = runAt,
            interval = intervalSeconds,
            max = opts.maxAttempts or 1,
            now = now,
        }
    )

    return id
end

-- runs as soon as the next tick reaches it
function Scheduler:enqueue(name, payload, opts)
    return self:_insert(name, payload, opts, "queued", os.time(), nil)
end

-- runs once at the given unix time
function Scheduler:schedule(name, payload, runAt, opts)
    return self:_insert(name, payload, opts, "scheduled", runAt, nil)
end

-- runs now and then re-arms every intervalSeconds after each success
function Scheduler:every(name, payload, intervalSeconds, opts)
    return self:_insert(name, payload, opts, "scheduled", os.time(), intervalSeconds)
end

function Scheduler:get(id)
    local row = self:_query("SELECT * FROM scheduler_tasks WHERE id = :id", { id = id })[1]
    if not row then
        return nil
    end

    row.payload = decode(row.payload)
    row.result = decode(row.result)
    return row
end

function Scheduler:list(filter)
    filter = filter or {}
    if filter.state then
        return self:_query("SELECT * FROM scheduler_tasks WHERE state = :state ORDER BY created_at", { state = filter.state })
    end

    return self:_query("SELECT * FROM scheduler_tasks ORDER BY created_at")
end

function Scheduler:history(taskId)
    return self:_query("SELECT * FROM scheduler_runs WHERE task_id = :id ORDER BY started_at", { id = taskId })
end

-- cancels a task that has not started yet
function Scheduler:cancel(id)
    return self:_exec(
        "UPDATE scheduler_tasks SET state = 'cancelled', updated_at = :now WHERE id = :id AND state IN ('scheduled', 'queued')",
        { now = os.time(), id = id }
    ) == 1
end

function Scheduler:remove(id)
    return self:_exec("DELETE FROM scheduler_tasks WHERE id = :id", { id = id }) == 1
end

function Scheduler:pause()
    self.paused = true
end

function Scheduler:resume()
    self.paused = false
end

function Scheduler:_backoff(attempt)
    local delay = self.backoffSeconds * (2 ^ (attempt - 1))
    if delay > self.maxBackoffSeconds then
        return self.maxBackoffSeconds
    end

    return delay
end

function Scheduler:_recordRun(taskId, attempt, ok, value, startedAt, finishedAt)
    self:_exec(
        [[INSERT INTO scheduler_runs (id, task_id, attempt, state, started_at, finished_at, error, result)
          VALUES (:id, :task, :attempt, :state, :start, :finish, :error, :result)]],
        {
            id = crypto.uuidV7(),
            task = taskId,
            attempt = attempt,
            state = ok and "success" or "failed",
            start = startedAt,
            finish = finishedAt,
            error = (not ok) and tostring(value) or nil,
            result = (ok and value ~= nil) and encode(value) or nil,
        }
    )
end

function Scheduler:_settle(row, ok, value, startedAt)
    local now = os.time()
    local attempt = (row.attempts or 0) + 1
    self:_recordRun(row.id, attempt, ok, value, startedAt, now)

    if ok and row.interval_seconds then
        self:_exec(
            "UPDATE scheduler_tasks SET state = 'scheduled', run_at = :run_at, attempts = 0, last_error = NULL, result = :result, updated_at = :now WHERE id = :id",
            { run_at = now + row.interval_seconds, result = encode(value), now = now, id = row.id }
        )
    elseif ok then
        self:_exec(
            "UPDATE scheduler_tasks SET state = 'success', result = :result, updated_at = :now WHERE id = :id",
            { result = encode(value), now = now, id = row.id }
        )
    elseif attempt < (row.max_attempts or 1) then
        self:_exec(
            "UPDATE scheduler_tasks SET state = 'scheduled', attempts = :attempt, run_at = :run_at, last_error = :error, updated_at = :now WHERE id = :id",
            { attempt = attempt, run_at = now + self:_backoff(attempt), error = tostring(value), now = now, id = row.id }
        )
    else
        self:_exec(
            "UPDATE scheduler_tasks SET state = 'failed', attempts = :attempt, last_error = :error, updated_at = :now WHERE id = :id",
            { attempt = attempt, error = tostring(value), now = now, id = row.id }
        )
    end
end

function Scheduler:_dispatch(row)
    self.active = self.active + 1

    async.spawn(function()
        local startedAt = os.time()
        local handler = self.handlers[row.name]

        local ok, value
        if handler then
            ok, value = pcall(handler, decode(row.payload), row)
        else
            ok, value = false, "no handler registered for task " .. row.name
        end

        local settled = pcall(function()
            self:_settle(row, ok, value, startedAt)
        end)
        if not settled then
            self:_exec("UPDATE scheduler_tasks SET state = 'failed', updated_at = :now WHERE id = :id", { now = os.time(), id = row.id })
        end

        self.active = self.active - 1
    end)
end

function Scheduler:_tick()
    local now = os.time()

    self:_exec("UPDATE scheduler_tasks SET state = 'queued', updated_at = :now WHERE state = 'scheduled' AND run_at <= :now", { now = now })

    if self.paused then
        return
    end

    local slots = self.concurrency - self.active
    if slots <= 0 then
        return
    end

    local rows = self:_query(
        "SELECT * FROM scheduler_tasks WHERE state = 'queued' AND run_at <= :now ORDER BY priority DESC, run_at ASC LIMIT :slots",
        { now = now, slots = slots }
    )

    for _, row in ipairs(rows) do
        local claimed = self:_exec(
            "UPDATE scheduler_tasks SET state = 'running', lease_epoch = :epoch, updated_at = :now WHERE id = :id AND state = 'queued'",
            { epoch = self.epoch, now = now, id = row.id }
        )
        if claimed == 1 then
            self:_dispatch(row)
        end
    end
end

-- boots the scheduler, returning any task left running by a dead process to the queue, then starts the tick loop; idempotent
function Scheduler:start()
    if self.running then
        return
    end

    self.epoch = crypto.uuidV7()
    self:_exec("UPDATE scheduler_tasks SET state = 'queued', lease_epoch = NULL, updated_at = :now WHERE state = 'running'", { now = os.time() })

    self.running = true

    async.spawn(function()
        while self.running do
            local ok, err = pcall(function()
                self:_tick()
            end)
            if not ok then
                self.lastError = tostring(err)
            end

            async.sleep(self.pollMs):await()
        end
    end)
end

function Scheduler:stop()
    self.running = false
end

function Scheduler:close()
    self.running = false
    self.db:close()
end

return scheduler
