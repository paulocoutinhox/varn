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
    async::Promise::installMetatable(L);
    async::AsyncModule::install(L);
    fs::FsModule::install(L);
    ffi::FfiModule::install(L);
    log::LogModule::install(L);
    crypto::CryptoModule::install(L);
    platform::PlatformModule::install(L);
    zip::ZipModule::install(L);
    http::HttpServerModule::install(L);
    socket::SocketModule::install(L);
}

} // namespace varn::lua
