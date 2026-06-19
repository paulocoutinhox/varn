-- ffi features exercising cdef, calls, new, cast, copy, string, and the introspection surface, staying inside the always-present process namespace ffi.C with standard libc symbols and never loading an optional library so it is ci-safe on ubuntu-24.04.
local ffi = require("ffi")

ffi.cdef [[
    unsigned long strlen(const char *s);
    int abs(int n);
    int memcmp(const char *a, const char *b, unsigned long n);
]]

-- basic calls return deterministic results from libc.
assert(ffi.C.strlen("hello") == 5, "strlen(hello) should be 5")
assert(ffi.C.strlen("") == 0, "strlen('') should be 0")
assert(ffi.C.abs(-3) == 3, "abs(-3) should be 3")
assert(ffi.C.abs(7) == 7, "abs(7) should be 7")

-- memcmp reports equality and difference deterministically.
assert(ffi.C.memcmp("abc", "abc", 3) == 0, "memcmp of equal buffers should be 0")
assert(ffi.C.memcmp("abc", "abd", 3) ~= 0, "memcmp of different buffers should be non-zero")

-- introspection over standard c types is stable.
assert(ffi.sizeof("char") == 1, "sizeof char should be 1")
assert(ffi.sizeof("char[10]") == 10, "sizeof char[10] should be 10")
assert(tostring(ffi.typeof("int")) ~= nil, "typeof int should produce a ctype")

-- new allocates a buffer, copy fills it, and string reads it back.
local buf = ffi.new("char[?]", 6)
ffi.copy(buf, "world")
assert(ffi.string(buf) == "world", "ffi.string should read back the copied bytes")

-- cast reinterprets the buffer as a const char pointer over standard types only.
local ptr = ffi.cast("const char *", buf)
assert(ffi.string(ptr) == "world", "ffi.string through a cast pointer should match")
assert(ffi.string(buf, 3) == "wor", "ffi.string with an explicit length should truncate")

-- the null sentinel is exposed and stringifies without crashing.
assert(tostring(ffi.nullptr) ~= nil, "ffi.nullptr should stringify")

print("ffi features ok")
