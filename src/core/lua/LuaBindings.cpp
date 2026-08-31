#include "core/lua/LuaBindings.h"

#include <LuaWrapper.h>

#include "core/lua/DisplayApi.h"
#include "core/lua/FsApi.h"
#include "core/lua/InputApi.h"
#include "core/lua/SettingsApi.h"
#include "core/lua/SysApi.h"

namespace LuaBindings {

void install(LuaWrapper& wrapper) {
    lua_State* L = wrapper.state();
    if (L == nullptr) {
        return;
    }

    DisplayApi::install(L);
    InputApi::install(L);
    FsApi::install(L);
    SysApi::install(L);
    SettingsApi::install(L);
}

}  // namespace LuaBindings
