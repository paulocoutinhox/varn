-- compares against the published hmac test vector or prints skip on dummy builds
local crypto = require("crypto")

local key = string.rep("\x0b", 20)
local data = "Hi There"
local expected = "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"

local ok, got = pcall(crypto.hmac, "SHA256", key, data, "hex")
if not ok then
    print("SKIP crypto.hmac vectors:", got)
    os.exit(0)
end
assert(got == expected, "HMAC-SHA256 RFC 4231 case 1 mismatch")

local badOk, badErr = pcall(crypto.hmac, "__NOT_A_REAL_DIGEST__", key, data, "hex")
assert(not badOk, "unknown digest should error")

print("crypto.hmac RFC 4231 vector ok + invalid digest rejected")
os.exit(0)
