-- sanity check that the stub module still exposes a process namespace table
local ffi = require("ffi")

assert(type(ffi.cdef) == "function", "ffi.cdef")
assert(type(ffi.load) == "function", "ffi.load")
assert(ffi.C ~= nil, "ffi.C (default process symbols)")

ffi.cdef [[ int puts(const char *s); ]]

local ok, err = pcall(function()
    ffi.C.puts("ffi dummy_smoke: ffi.C puts ok")
end)

if ok then
    print("ffi dummy_smoke ok")
else
    print("ffi table ok; ffi.C.puts failed:", err)
end
os.exit(0)
