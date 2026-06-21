-- test: a tiny test runner. describe groups cases, it registers one case, and expect builds fluent assertions; the registered cases run on require-return run() (or at process exit) and print a pass/fail summary, exiting non-zero when anything failed.
local test = {}

-- every registered case, in declaration order, each tagged with the describe block it lives in.
local cases = {}
-- the describe block currently being defined, so nested it() calls inherit its name.
local currentGroup = nil
-- guards against running the suite twice when the caller invokes run() explicitly and the at-exit fallback also fires.
local ran = false

-- renders a value for failure messages; tables show their contents one level deep so a mismatch is readable.
local function show(value)
    if type(value) ~= "table" then
        return tostring(value)
    end
    local parts = {}
    for key, item in pairs(value) do
        parts[#parts + 1] = tostring(key) .. "=" .. tostring(item)
    end
    table.sort(parts)
    return "{ " .. table.concat(parts, ", ") .. " }"
end

-- deep structural equality for the table comparison in toEqual; falls back to == for non-tables.
local function deepEqual(a, b)
    if a == b then
        return true
    end
    if type(a) ~= "table" or type(b) ~= "table" then
        return false
    end
    for key, value in pairs(a) do
        if not deepEqual(value, b[key]) then
            return false
        end
    end
    for key in pairs(b) do
        if a[key] == nil then
            return false
        end
    end
    return true
end

-- expect(value) -> a matcher object; each matcher raises on failure so the case fails with a clear message.
function test.expect(value)
    local matcher = {}

    -- matchers are called with the colon syntax (expect(x):toBe(y)), so each takes self and ignores it, reading the captured value from the closure.

    -- exact identity/primitive equality.
    function matcher:toBe(expected)
        if value ~= expected then
            error("expected " .. show(expected) .. " but got " .. show(value), 2)
        end
    end

    -- deep equality, for comparing tables by content.
    function matcher:toEqual(expected)
        if not deepEqual(value, expected) then
            error("expected " .. show(expected) .. " but got " .. show(value), 2)
        end
    end

    -- passes when the value is truthy (anything but nil/false).
    function matcher:toBeTruthy()
        if not value then
            error("expected a truthy value but got " .. show(value), 2)
        end
    end

    -- passes when the value is falsy.
    function matcher:toBeFalsy()
        if value then
            error("expected a falsy value but got " .. show(value), 2)
        end
    end

    -- value must be a function; passes when calling it raises.
    function matcher:toThrow()
        if type(value) ~= "function" then
            error("toThrow expects a function, got " .. type(value), 2)
        end
        local ok = pcall(value)
        if ok then
            error("expected the function to throw, but it did not", 2)
        end
    end

    return matcher
end

-- describe(name, fn) groups the cases registered inside fn under name.
function test.describe(name, fn)
    local previous = currentGroup
    currentGroup = name
    fn()
    currentGroup = previous
end

-- it(name, fn) registers one test case; fn runs later when the suite runs.
function test.it(name, fn)
    cases[#cases + 1] = { group = currentGroup, name = name, fn = fn }
end

-- runs every registered case, prints a per-case line and a summary, and exits non-zero if any failed. safe to call once; a second call is a no-op so the at-exit fallback does not double-run.
function test.run()
    if ran then
        return
    end
    ran = true

    local passed, failed = 0, 0
    for _, case in ipairs(cases) do
        local label = case.group and (case.group .. " > " .. case.name) or case.name
        local ok, err = pcall(case.fn)
        if ok then
            passed = passed + 1
            print("  ok   - " .. label)
        else
            failed = failed + 1
            print("  FAIL - " .. label)
            print("         " .. tostring(err))
        end
    end

    print(string.format("\n%d passed, %d failed (%d total)", passed, failed, passed + failed))

    if failed > 0 then
        os.exit(1)
    end
end

return test
