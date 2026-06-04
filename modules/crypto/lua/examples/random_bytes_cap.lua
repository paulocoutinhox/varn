-- shows the randomBytes count cap by drawing at the limit and rejecting one byte over it.
local crypto = require("crypto")

local atCap = crypto.randomBytes(1024 * 1024)
assert(#atCap == 1024 * 1024, "draw at the cap")

local ok = pcall(crypto.randomBytes, 1024 * 1024 + 1)
assert(not ok, "a count above the cap is rejected")

print("crypto.randomBytes cap ok: at-cap len=", #atCap, "over-cap rejected")
os.exit(0)
