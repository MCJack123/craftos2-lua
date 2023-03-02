/*
** $Id: lstring.h,v 1.43.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h


#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define ROPE_CLUSTER_SIZE ((sizeof(TRope) * 16 - sizeof(void*) * 2) * 8)
#define SUBSTR_CLUSTER_SIZE ((sizeof(TSubString) * 16 - sizeof(void*) * 2) * 8)
#define BITMAP_UNIT_SIZE (sizeof(bitmap_unit) * 8)
#define BITMAP_SKIP (sizeof(void*) / sizeof(bitmap_unit) * 2)

#define sizestring(s)	(sizeof(union TString)+((s)->len+1)*sizeof(char))

#define sizeudata(u)	(sizeof(union Udata)+(u)->len)

#define clusterid(l) (*(bitmap_unit*)((void**)(l) + 1))
#define nextropecluster(l) (*(TRope**)(l))
#define nextsscluster(l) (*(TSubString**)(l))

#define luaS_new(L, s)	(luaS_newlstr(L, s, strlen(s)))
#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))

#define luaS_fix(s)	l_setbit((s)->tsv.marked, FIXEDBIT)

typedef unsigned long bitmap_unit;

LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s, Table *e);
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TRope *luaS_concat (lua_State *L, TRope *l, TRope *r);
LUAI_FUNC TString *luaS_build (lua_State *L, TRope *rope);
LUAI_FUNC void luaS_freerope (lua_State *L, TRope *rope);
LUAI_FUNC void luaS_freesubstr (lua_State *L, TSubString *ss);
LUAI_FUNC void luaS_freeclusters (lua_State *L);


#endif
