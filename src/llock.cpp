// C++ locks for Lua
extern "C" {
#include "lua.h"
#include "luaconf.h"
#include "lstate.h"
}
#include <mutex>

extern "C" {
    void _lua_lock(lua_State *L) {
        ((std::recursive_mutex*)G(L)->lock)->lock();
        G(L)->lockstate = 1;
    }

    void _lua_unlock(lua_State *L) {
        ((std::recursive_mutex*)G(L)->lock)->unlock();
        G(L)->lockstate = 0;
    }

    void * _lua_newlock() {
        return new std::recursive_mutex;
    }

    void _lua_freelock(void * l) {
        delete (std::recursive_mutex*)l;
    }
}