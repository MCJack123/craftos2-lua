// C++ locks for Lua
extern "C" {
#include "lua.h"
#include "luaconf.h"
#include "lstate.h"
}
#include <mutex>
#include <stdio.h>

extern "C" {
    void _lua_lock(lua_State *L) {
        if (G(L)->lockstate) {
            //fprintf(stderr, "Attempted to lock a thread twice!\n");
            return;
        }
        ((std::mutex*)G(L)->lock)->lock();
        G(L)->lockstate = 1;
    }

    void _lua_unlock(lua_State *L) {
        if (!G(L)->lockstate) {
            //fprintf(stderr, "Attempted to unlock a thread twice!\n");
            return;
        }
        ((std::mutex*)G(L)->lock)->unlock();
        G(L)->lockstate = 0;
    }

    void * _lua_newlock() {
        return new std::mutex;
    }

    void _lua_freelock(void * l) {
        delete (std::mutex*)l;
    }
}