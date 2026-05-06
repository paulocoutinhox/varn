#include "varn/lua/NativeModules.h"

#include "varn/async/AsyncModule.h"
#include "varn/async/Promise.h"
#include "varn/crypto/CryptoModule.h"
#include "varn/ffi/FfiModule.h"
#include "varn/platform/PlatformModule.h"
#include "varn/zip/ZipModule.h"
#include "varn/fs/FsModule.h"
#include "varn/http/HttpServerModule.h"
#include "varn/log/LogModule.h"
#include "varn/socket/SocketModule.h"

namespace varn::lua {

void NativeModuleRegistry::installAll(lua_State* L) {
    Promise::installMetatable(L);
    AsyncModule::install(L);
    FsModule::install(L);
    FfiModule::install(L);
    LogModule::install(L);
    CryptoModule::install(L);
    PlatformModule::install(L);
    ZipModule::install(L);
    HttpServerModule::install(L);
    SocketModule::install(L);
}

} // namespace varn::lua
