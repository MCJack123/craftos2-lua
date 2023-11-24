/*
** $Id: lstring.h,v 1.49.1.1 2013/04/12 18:48:47 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define ROPE_CLUSTER_SIZE ((sizeof(TString) * 16 - sizeof(void*) * 2) * 8)
#define SUBSTR_CLUSTER_SIZE ((sizeof(TString) * 16 - sizeof(void*) * 2) * 8)
#define BITMAP_UNIT_SIZE (sizeof(bitmap_unit) * 8)
#define BITMAP_SKIP (sizeof(void*) / sizeof(bitmap_unit) * 2)

#define sizestring(s)	(sizeof(union TString)+((s)->len+1)*sizeof(char))

#define sizeudata(u)	(sizeof(union Udata)+(u)->len)

#define clusterid(l) (*(bitmap_unit*)((void**)(l) + 1))
#define nextropecluster(l) (*(TString**)(l))
#define nextsscluster(l) (*(TString**)(l))

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))

#define luaS_fix(s)	l_setbit((s)->tsv.marked, FIXEDBIT)


typedef unsigned long bitmap_unit;

/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tsv.tt == LUA_TSHRSTR && (s)->tsv.extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tsv.tt == LUA_TSHRSTR, (a) == (b))


LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC int luaS_eqstr (TString *a, TString *b);
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s, Table *e);
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);
LUAI_FUNC TString *luaS_concat (lua_State *L, TString *l, TString *r);
LUAI_FUNC TString *luaS_build (lua_State *L, TString *rope);
LUAI_FUNC void luaS_freerope (lua_State *L, TString *rope);
LUAI_FUNC void luaS_freesubstr (lua_State *L, TString *ss);
LUAI_FUNC void luaS_freeclusters (lua_State *L);


#endif
