// C++ locks for Lua
extern "C" {
#include "lua.h"
#include "luaconf.h"
#include "lstate.h"
}
#include <mutex>

extern "C" {
    void _lua_lock(lua_State *L) {
        ((std::mutex*)G(L)->lock)->lock();
    }

    void _lua_unlock(lua_State *L) {
        ((std::mutex*)G(L)->lock)->unlock();
    }

    void * _lua_newlock() {
        return new std::mutex;
    }

    void _lua_freelock(void * l) {
        delete (std::mutex*)l;
    }
}