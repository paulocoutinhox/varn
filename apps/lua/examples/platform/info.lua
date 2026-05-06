-- prints host identifiers and shared library naming hints
local p = require("platform")

print("os()        ", p.os())
print("arch()      ", p.arch())
print("libPrefix() ", p.libPrefix())
print("shlibSuffix()", p.shlibSuffix())
assert(type(p.os()) == "string" and #p.os() > 0, "os")
assert(type(p.arch()) == "string", "arch")
print("platform info ok")
os.exit(0)
