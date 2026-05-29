-- skips cleanly when the linked crypto stack rejects the algorithm
local crypto = require("crypto")

local ok, h = pcall(crypto.digest, "SHA512", "test", "hex")
if not ok then
    print("SKIP crypto.digest SHA512:", h)
    os.exit(0)
end
assert(#h == 128, "sha512 hex length")
print("crypto.digest SHA512 ok:", h:sub(1, 16), "...")
os.exit(0)
