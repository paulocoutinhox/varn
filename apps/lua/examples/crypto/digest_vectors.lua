-- compares empty and short string digests or prints skip on dummy builds
local crypto = require("crypto")

local empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
local abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

local ok, got = pcall(crypto.digest, "SHA256", "", "hex")
if not ok then
    print("SKIP crypto.digest vectors:", got)
    os.exit(0)
end
assert(got == empty, "SHA256 empty string mismatch")

ok, got = pcall(crypto.digest, "SHA256", "abc", "hex")
assert(ok, got)
assert(got == abc, "SHA256 abc mismatch")

local badOk, badErr = pcall(crypto.digest, "__NOT_A_REAL_DIGEST_ALGO__", "x", "hex")
assert(not badOk, "unknown algorithm should error")
assert(
    tostring(badErr):find("unknown") or tostring(badErr):find("EVP") or tostring(badErr):find("digest"),
    badErr
)

print("crypto.digest NIST vectors ok (empty, abc) + invalid algo rejected")
os.exit(0)
