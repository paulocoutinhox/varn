-- async: spawn runs as a coroutine, sleep yields, and promises report completion.
local async = require("async")

async.spawn(function()
    assert(coroutine.isyieldable(), "spawn body should run as a coroutine")

    local promise = async.sleep(5)
    assert(promise:isDone() == false, "promise should start pending")
    promise:await()
    assert(promise:isDone() == true, "promise should be done after await")

    print("async ok")
    os.exit(0)
end)
