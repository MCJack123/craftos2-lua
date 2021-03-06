/*
** $Id: lualib.h,v 1.36.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef lualib_h
#define lualib_h

#include <stdio.h>

#include "lua.h"


/* Key to file-handle type */
#define LUA_FILEHANDLE		"FILE*"


#define LUA_COLIBNAME	"coroutine"
LUALIB_API int (luaopen_base) (lua_State *L);

#define LUA_TABLIBNAME	"table"
LUALIB_API int (luaopen_table) (lua_State *L);

#define LUA_IOLIBNAME	"io"
LUALIB_API int (luaopen_io) (lua_State *L);

#define LUA_OSLIBNAME	"os"
LUALIB_API int (luaopen_os) (lua_State *L);

#define LUA_STRLIBNAME	"string"
LUALIB_API int (luaopen_string) (lua_State *L);

#define LUA_MATHLIBNAME	"math"
LUALIB_API int (luaopen_math) (lua_State *L);

#define LUA_DBLIBNAME	"debug"
LUALIB_API int (luaopen_debug) (lua_State *L);

#define LUA_LOADLIBNAME	"package"
LUALIB_API int (luaopen_package) (lua_State *L);

#define LUA_UTF8LIBNAME	"utf8"
LUALIB_API int (luaopen_utf8)(lua_State *L);

#define LUA_BITLIBNAME	"bit32"
LUALIB_API int (luaopen_bit32)(lua_State *L);


/* open all previous libraries */
LUALIB_API void (luaL_openlibs) (lua_State *L); 

LUALIB_API void lualib_debug_ccpc_functions(void(*scm)(lua_State *L, int), lua_CFunction debug, lua_CFunction breakpoint, lua_CFunction unsetbreakpoint);
LUALIB_API void lualib_io_ccpc_functions(FILE* (*open)(lua_State *L, const char * filename, const char * mode), int (*close)(lua_State *L, FILE * stream));

#ifndef lua_assert
#define lua_assert(x)	((void)0)
#endif


#endif
