# 🔐 crypto

Hashing, keyed hashing, and cryptographically secure random bytes (OpenSSL-backed).

- `crypto.digest(algorithm, data, format?)` → a digest of `data`. `algorithm` is a name like `"SHA256"` or `"SHA512"`. `format` is `"hex"` (default) or `"raw"`.
- `crypto.hmac(algorithm, key, data, format?)` → an HMAC, with the same `format` options.
- `crypto.randomBytes(n)` → `n` random bytes as a string.
- `crypto.equals(a, b)` → `true` if the two strings are equal, compared in constant time. Use this to verify a MAC, token, or any secret-derived value instead of `==`, which leaks bytes through timing.

There is no artificial input-size limit — Varn runs your own code on your own machine, so hashing or generating large data is allowed (bounded only by memory and the platform's integer limits). Not available in the browser build.

## Examples

### `digest_raw.lua`

```lua
-- checks the raw digest length for a fixed input string
local crypto = require("crypto")

local raw = crypto.digest("SHA256", "raw-test", "raw")
assert(#raw == 32, "raw sha256 length")
print("crypto.digest raw ok, len=", #raw)
```

### `digest_sha256.lua`

```lua
-- checks a short string against a known hex fingerprint
local crypto = require("crypto")

local h = crypto.digest("SHA256", "varn", "hex")
assert(#h == 64, "sha256 hex length")
assert(h:match("^[0-9a-f]+$"), "expected lowercase hex")
print("crypto.digest SHA256 hex ok:", h:sub(1, 16), "...")
```

### `digest_sha512.lua`

```lua
-- hashes a string with SHA-512 and prints the start of the hex digest.
local crypto = require("crypto")

local hex = crypto.digest("SHA512", "test", "hex")
assert(#hex == 128, "sha512 hex length")

print("crypto.digest SHA512 ok:", hex:sub(1, 16) .. "...")
```

### `digest_vectors.lua`

```lua
-- verifies SHA-256 against known vectors for the empty and "abc" inputs.
local crypto = require("crypto")

assert(crypto.digest("SHA256", "", "hex")
    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 empty string")
assert(crypto.digest("SHA256", "abc", "hex")
    == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 abc")

print("crypto.digest SHA256 vectors ok (empty, abc)")
```

### `hmac_vectors.lua`

```lua
-- verifies HMAC-SHA256 against RFC 4231 test case 1.
local crypto = require("crypto")

local key = string.rep("\x0b", 20)
local mac = crypto.hmac("SHA256", key, "Hi There", "hex")
assert(mac == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "HMAC-SHA256 RFC 4231 case 1")

print("crypto.hmac RFC 4231 vector ok")
```

### `random_bytes.lua`

```lua
-- asserts only on the length fields returned for random byte requests
local crypto = require("crypto")

local b16 = crypto.randomBytes(16)
assert(#b16 == 16, "length")
local b32 = crypto.randomBytes(32)
assert(#b32 == 32, "length")
print("crypto.randomBytes ok")
```
