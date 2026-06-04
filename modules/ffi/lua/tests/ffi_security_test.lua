-- ffi security: covered classes are the lua-reachable subset of the argument marshalling cases.
-- most ffi security ids (library loading, callbacks, abi, arbitrary memory, rce) are pure
-- native or c++ concerns and are not reachable from this lua surface, so they are not driven here.
-- the lua-reachable cases are argument type validation, which the binding checks before the call.
-- covered: FFI-023 type confusion string vs pointer, FFI-028 float/int coercion to a wrong c type.
-- note: FFI-021 null pointer arg (strlen(nil)) is a native deref by design and is not exercised,
-- because passing a null pointer to a dereferencing libc function crashes in libc, not in the binding.
-- this stays inside the always-present process namespace ffi.C with standard libc symbols.
local ffi = require("ffi")

ffi.cdef [[
    unsigned long strlen(const char *s);
    int abs(int n);
]]

-- FFI-023: passing a table where a const char pointer is expected must be rejected, not crash.
local ok_table, err_table = pcall(function()
    return ffi.C.strlen({})
end)
assert(ok_table == false, "strlen with a table arg should be rejected")
assert(type(err_table) == "string", "the rejection should carry an error message")

-- FFI-028: passing a string where an int is expected must be rejected, not coerced silently.
local ok_str, err_str = pcall(function()
    return ffi.C.abs("not a number")
end)
assert(ok_str == false, "abs with a string arg should be rejected")
assert(type(err_str) == "string", "the rejection should carry an error message")

-- after the rejected calls the binding stack stays healthy and normal calls still work.
assert(ffi.C.strlen("ok") == 2, "valid calls should still succeed after a rejected one")
assert(ffi.C.abs(-9) == 9, "valid calls should still succeed after a rejected one")

print("ffi security ok")
os.exit(0)
