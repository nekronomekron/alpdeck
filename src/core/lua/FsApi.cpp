#include "core/lua/FsApi.h"

#include <SD.h>

#include "core/Vfs.h"
#include "core/lua/LuaApi.h"
#include "core/lua/LuaContext.h"
#include "utils/Logger.h"

namespace FsApi {
namespace {

// Rejects anything that could climb out of the sandbox. Checked before any
// prefix comparison, because "/sd/apps/x/../../boot.lua" would otherwise pass
// a naive startsWith test.
bool hasTraversal(const String& path) { return path.indexOf("..") >= 0; }

// Resolves the path an app gave us into an absolute one.
//
// A path that does not start with '/' is relative to the app's own directory,
// which is how an app reads its assets without knowing where it was installed.
// Returns an empty string when that cannot be done -- the caller turns it into
// a Lua error naming the reason.
String resolveInternal(const String& path, const char** reason) {
    if (hasTraversal(path)) {
        *reason = "'..' is not allowed";
        return "";
    }
    if (path.startsWith("/")) {
        return path;
    }

    const String& root = LuaContext::sandboxRoot();
    if (root.isEmpty()) {
        *reason = "relative path, but this script has no app directory";
        return "";
    }
    return root + "/" + path;
}

// Writes stay inside the running app's own directory. The launcher runs with
// an empty root and so may write nowhere -- it only ever reads.
bool writeAllowed(const String& absolute) {
    const String& root = LuaContext::sandboxRoot();
    if (root.isEmpty()) {
        return false;
    }
    return absolute == root || absolute.startsWith(root + "/");
}

int list(lua_State* L) {
    const char* reason = "";
    const String path = resolveInternal(luaL_checkstring(L, 1), &reason);
    if (path.isEmpty()) {
        return luaL_error(L, "fs.list denied: %s", reason);
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    const bool onSd = &fs == &SD;

    File dir = fs.open(localPath);
    if (!dir || !dir.isDirectory()) {
        LOGW(LuaApi::kLogTag, "fs.list('%s') -> %s:'%s' not a directory",
             path.c_str(), onSd ? "SD" : "LittleFS", localPath.c_str());
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    // Absolute index of the result table, so storing into it never depends on
    // the fluctuating relative top during entry construction.
    const int resultIndex = lua_gettop(L);
    lua_Integer index = 1;

    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        // entry.name() is a bare name on some cores and a full path on others;
        // normalise to the last segment so apps see one shape.
        String name = entry.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        const bool isDir = entry.isDirectory();
        const size_t size = entry.size();
        entry.close();

        lua_newtable(L);
        LuaApi::setField(L, "name", name.c_str());
        LuaApi::setField(L, "dir", isDir);
        LuaApi::setField(L, "size", static_cast<lua_Integer>(size));
        lua_seti(L, resultIndex, index++);
    }
    dir.close();

    LOGD(LuaApi::kLogTag, "fs.list('%s') -> %d entries", path.c_str(),
         static_cast<int>(index - 1));
    return 1;
}

int read(lua_State* L) {
    const char* reason = "";
    const String path = resolveInternal(luaL_checkstring(L, 1), &reason);
    if (path.isEmpty()) {
        return luaL_error(L, "fs.read denied: %s", reason);
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);

    File file = fs.open(localPath, "r");
    if (!file || file.isDirectory()) {
        lua_pushnil(L);
        return 1;
    }

    const size_t expected = file.size();
    String content = file.readString();
    file.close();

    // A short read is an I/O or allocation failure, not a shorter file. Say so
    // rather than handing back a silently truncated asset.
    if (content.length() != expected) {
        return luaL_error(L, "fs.read('%s') got %d of %d bytes", path.c_str(),
                          static_cast<int>(content.length()),
                          static_cast<int>(expected));
    }

    lua_pushlstring(L, content.c_str(), content.length());
    return 1;
}

int exists(lua_State* L) {
    const char* reason = "";
    const String path = resolveInternal(luaL_checkstring(L, 1), &reason);
    if (path.isEmpty()) {
        lua_pushboolean(L, false);
        return 1;
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);
    lua_pushboolean(L, fs.exists(localPath));
    return 1;
}

int write(lua_State* L) {
    const char* reason = "";
    const String path = resolveInternal(luaL_checkstring(L, 1), &reason);
    if (path.isEmpty()) {
        return luaL_error(L, "fs.write denied: %s", reason);
    }

    size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);

    if (!writeAllowed(path)) {
        return luaL_error(L, "fs.write denied for '%s' (outside '%s')",
                          path.c_str(), LuaContext::sandboxRoot().c_str());
    }

    String localPath;
    fs::FS& fs = Vfs::resolve(path, localPath);

    File file = fs.open(localPath, "w");
    if (!file) {
        lua_pushboolean(L, false);
        return 1;
    }

    const size_t written =
        file.write(reinterpret_cast<const uint8_t*>(data), length);
    file.close();

    lua_pushboolean(L, written == length);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"list", list},     {"read", read}, {"exists", exists},
    {"write", write},   {nullptr, nullptr},
};

}  // namespace

String resolvePath(const String& path, const char** reason) {
    return resolveInternal(path, reason);
}

void install(lua_State* L) { LuaApi::installTable(L, "fs", kFunctions); }

}  // namespace FsApi
