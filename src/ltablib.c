/*
** $Id: ltablib.c,v 1.38.1.3 2008/02/14 16:46:58 roberto Exp $
** Library for Table Manipulation
** See Copyright Notice in lua.h
*/


#include <stddef.h>

#define ltablib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#define aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))
#define aux_igetn(L,n,c)	(luaL_checktype(L, n, LUA_TTABLE), luaL_igetn(L, n, c))


static int foreachi (lua_State *L) {
  int n;
  int i = lua_icontext(L);
  if (i > 0) {
    n = lua_tointeger(L, 3);  /* get cached n */
    goto resume;
  }
  n = aux_igetn(L, 1, -1);
   luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);
  lua_pushinteger(L, n);  /* cache n because aux_igetn may be expensive */
   for (i=1; i <= n; i++) {
     lua_pushvalue(L, 2);  /* function */
     lua_pushinteger(L, i);  /* 1st argument */
     lua_rawgeti(L, 1, i);  /* 2nd argument */
    lua_icall(L, 2, 1, i);
resume:
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 1);  /* remove nil result */
  }
  return 0;
}


static int foreach (lua_State *L) {
  if (lua_vcontext(L)) goto resume;
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushnil(L);  /* first key */
  while (lua_next(L, 1)) {
    lua_pushvalue(L, 2);  /* function */
    lua_pushvalue(L, -3);  /* key */
    lua_pushvalue(L, -3);  /* value */
    lua_icall(L, 2, 1, 1);
resume:
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 2);  /* remove value and result */
  }
  return 0;
}


static int maxn (lua_State *L) {
  lua_Number max = 0;
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);  /* first key */
  while (lua_next(L, 1)) {
    lua_pop(L, 1);  /* remove value */
    if (lua_type(L, -1) == LUA_TNUMBER) {
      lua_Number v = lua_tonumber(L, -1);
      if (v > max) max = v;
    }
  }
  lua_pushnumber(L, max);
  return 1;
}


static int getn (lua_State *L) {
  lua_pushinteger(L, aux_igetn(L, 1, 1));
  return 1;
}


static int setn (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
#ifndef luaL_setn
  luaL_setn(L, 1, luaL_checkint(L, 2));
#else
  luaL_error(L, LUA_QL("setn") " is obsolete");
#endif
  lua_pushvalue(L, 1);
  return 1;
}


static int tinsert (lua_State *L) {
  int e = aux_igetn(L, 1, 1) + 1;  /* first empty element */
  int pos;  /* where to insert new element */
  switch (lua_gettop(L)) {
    case 2: {  /* called with only 2 arguments */
      pos = e;  /* insert new element at the end */
      break;
    }
    case 3: {
      int i;
      pos = luaL_checkint(L, 2);  /* 2nd argument is the position */
      if (pos > e) e = pos;  /* `grow' array if necessary */
      for (i = e; i > pos; i--) {  /* move up elements */
        lua_rawgeti(L, 1, i-1);
        lua_rawseti(L, 1, i);  /* t[i] = t[i-1] */
      }
      break;
    }
    default: {
      return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
    }
  }
  luaL_setn(L, 1, e);  /* new size */
  lua_rawseti(L, 1, pos);  /* t[pos] = v */
  return 0;
}


static int tremove (lua_State *L) {
  int e = aux_igetn(L, 1, 1);
  int pos = luaL_optint(L, 2, e);
  if (!(1 <= pos && pos <= e))  /* position is outside bounds? */
   return 0;  /* nothing to remove */
  luaL_setn(L, 1, e - 1);  /* t.n = n-1 */
  lua_rawgeti(L, 1, pos);  /* result = t[pos] */
  for ( ;pos<e; pos++) {
    lua_rawgeti(L, 1, pos+1);
    lua_rawseti(L, 1, pos);  /* t[pos] = t[pos+1] */
  }
  lua_pushnil(L);
  lua_rawseti(L, 1, e);  /* t[e] = nil */
  return 1;
}


static void addfield (lua_State *L, luaL_Buffer *b, int i) {
  lua_rawgeti(L, 1, i);
  if (!lua_isstring(L, -1))
    luaL_error(L, "invalid value (%s) at index %d in table for "
                  LUA_QL("concat"), luaL_typename(L, -1), i);
    luaL_addvalue(b);
}


static int tconcat (lua_State *L) {
  luaL_Buffer b;
  size_t lsep;
  int i, last;
  const char *sep = luaL_optlstring(L, 2, "", &lsep);
  luaL_checktype(L, 1, LUA_TTABLE);
  i = luaL_optint(L, 3, 1);
  if (!lua_icontext(L)) lua_settop(L, 4);
  last = luaL_opt(L, luaL_checkint, 4, luaL_igetn(L, 1, 1));
  luaL_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    luaL_addlstring(&b, sep, lsep);
  }
  if (i == last)  /* add last value (if interval was not empty) */
    addfield(L, &b, i);
  luaL_pushresult(&b);
  return 1;
}



/*
** {======================================================
** Quicksort
** (based on `Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
*/


static void set2 (lua_State *L, int i, int j) {
  lua_rawseti(L, 1, i);
  lua_rawseti(L, 1, j);
}

struct table_sort_args {
    int l;
    int u;
    struct table_sort_args * next;
};

struct table_sort_state {
    int s;
    int d;
    int i;
    int j;
    struct table_sort_args * args;
};

static int sort_comp (lua_State *L, int a, int b, struct table_sort_state * s, int ss) {
  if (!lua_isnil(L, 2)) {  /* function? */
    int res;
    if (s->s) goto resume;
    s->s = ss;
    lua_pushvalue(L, 2);
    lua_pushvalue(L, a-1);  /* -1 to compensate function */
    lua_pushvalue(L, b-2);  /* -2 to compensate function and `a' */
    lua_vcall(L, 2, 1, s);
resume:
    res = lua_toboolean(L, -1);
    lua_pop(L, 1);
    s->s = 0;
    return res;
  }
  else  /* a < b? */
    return lua_lessthan(L, a, b);
}

static void auxsort (lua_State *L, struct table_sort_state * s, struct table_sort_args * a, int m) {
  void * ud = NULL;
  lua_Alloc alloc = lua_getallocf(L, &ud);
  if (!s->s) s->d++;
  if (s->d > m) {
    auxsort(L, s, a->next, m + 1);
    alloc(ud, a->next, sizeof(struct table_sort_args), 0);
    a->next = NULL;
  }
  while (a->l < a->u) {  /* for tail recursion */
    switch (s->s) {
      case 1: goto resume1;
      case 2: goto resume2;
      case 3: goto resume3;
      case 4: goto resume4;
      case 5: goto resume5;
    }
    /* sort elements a[l], a[(l+u)/2] and a[u] */
    lua_rawgeti(L, 1, a->l);
    lua_rawgeti(L, 1, a->u);
resume1:
    if (sort_comp(L, -1, -2, s, 1))  /* a[u] < a[l]? */
      set2(L, a->l, a->u);  /* swap a[l] - a[u] */
    else
      lua_pop(L, 2);
    if (a->u-a->l == 1) break;  /* only 2 elements */
    s->i = (a->l+a->u)/2;
    lua_rawgeti(L, 1, s->i);
    lua_rawgeti(L, 1, a->l);
resume2:
    if (sort_comp(L, -2, -1, s, 2))  /* a[i]<a[l]? */
      set2(L, s->i, a->l);
    else {
      lua_pop(L, 1);  /* remove a[l] */
      lua_rawgeti(L, 1, a->u);
resume3:
      if (sort_comp(L, -1, -2, s, 3))  /* a[u]<a[i]? */
        set2(L, s->i, a->u);
      else
        lua_pop(L, 2);
    }
    if (a->u-a->l == 2) break;  /* only 3 elements */
    lua_rawgeti(L, 1, s->i);  /* Pivot */
    lua_pushvalue(L, -1);
    lua_rawgeti(L, 1, a->u-1);
    set2(L, s->i, a->u-1);
    /* a[l] <= P == a[u-1] <= a[u], only need to sort from l+1 to u-2 */
    s->i = a->l; s->j = a->u-1;
    for (;;) {  /* invariant: a[l..i] <= P <= a[j..u] */
      /* repeat ++i until a[i] >= P */
resume4:
      while ((!s->s ? lua_rawgeti(L, 1, ++s->i) : (void)0), sort_comp(L, -1, -2, s, 4)) {
        if (s->i>a->u) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[i] */
      }
      /* repeat --j until a[j] <= P */
resume5:
      while ((!s->s ? lua_rawgeti(L, 1, --s->j) : (void)0), sort_comp(L, -3, -1, s, 5)) {
        if (s->j<a->l) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[j] */
      }
      if (s->j<s->i) {
        lua_pop(L, 3);  /* pop pivot, a[i], a[j] */
        break;
      }
      set2(L, s->i, s->j);
    }
    lua_rawgeti(L, 1, a->u-1);
    lua_rawgeti(L, 1, s->i);
    set2(L, a->u-1, s->i);  /* swap pivot (a[u-1]) with a[i] */
    /* a[l..i-1] <= a[i] == P <= a[i+1..u] */
    /* adjust so that smaller half is in [j..i] and larger one in [l..u] */
    if (s->i-a->l < a->u-s->i) {
      s->j=a->l; s->i=s->i-1; a->l=s->i+2;
    }
    else {
      s->j=s->i+1; s->i=a->u; a->u=s->j-2;
    }
    a->next = (struct table_sort_args*)alloc(ud, NULL, 0, sizeof(struct table_sort_args));
    a->next->l = s->j, a->next->u = s->i;
    a->next->next = NULL;
    auxsort(L, s, a->next, m + 1);  /* call recursively the smaller one */
    alloc(ud, a->next, sizeof(struct table_sort_args), 0);
    a->next = NULL;
  }  /* repeat the routine for the larger one */
  s->d--;
}

static int sort (lua_State *L) {
  struct table_sort_state * s;
  int n;
  void * ud = NULL;
  lua_Alloc alloc = lua_getallocf(L, &ud);
  if (lua_icontext(L) > 0) {
    s = (struct table_sort_state*)lua_vcontext(L);
    goto resume;
  }
  n = aux_igetn(L, 1, -1);
  luaL_checkstack(L, 40, "");  /* assume array is smaller than 2^40 */
  if (!lua_isnoneornil(L, 2))  /* is there a 2nd argument? */
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);  /* make sure there is two arguments */
  s = (struct table_sort_state*)alloc(ud, NULL, 0, sizeof(struct table_sort_state));
  s->args = (struct table_sort_args*)alloc(ud, NULL, 0, sizeof(struct table_sort_args));
  s->args->l = 1;
  s->args->u = n;
  s->args->next = NULL;
  s->s = s->d = s->i = s->j = 0;
resume:
  auxsort(L, s, s->args, 1);
  for (struct table_sort_args* a = s->args, *aa = a; a != NULL; aa = a, a = a->next, alloc(ud, aa, sizeof(struct table_sort_args), 0)) ;
  alloc(ud, s, sizeof(struct table_sort_state), 0);
  return 0;
}

/* }====================================================== */

static int tpack (lua_State *L) {
  int n = lua_gettop(L);
  lua_createtable(L, n, 1);
  lua_pushinteger(L, n);
  lua_setfield(L, -2, "n");
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, i);
    lua_rawseti(L, -2, i);
  }
  return 1;
}

static int tunpack (lua_State *L) {
  int i, e, n;
  luaL_checktype(L, 1, LUA_TTABLE);
  i = luaL_optint(L, 2, 1);
  if (lua_icontext(L) == 0) lua_settop(L, 3);
  e = luaL_opt(L, luaL_checkint, 3, luaL_igetn(L, 1, 1));
  if (i > e) return 0;  /* empty range */
  n = e - i + 1;  /* number of elements */
  if (n <= 0 || !lua_checkstack(L, n))  /* n <= 0 means arith. overflow */
    return luaL_error(L, "too many results to unpack");
  lua_rawgeti(L, 1, i);  /* push arg[i] (avoiding overflow problems) */
  while (i++ < e)  /* push arg[i + 1...e] */
    lua_rawgeti(L, 1, i);
  return n;
}


static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"foreach", foreach},
  {"foreachi", foreachi},
  {"getn", getn},
  {"maxn", maxn},
  {"insert", tinsert},
  {"remove", tremove},
  {"setn", setn},
  {"sort", sort},
  {"pack", tpack},
  {"unpack", tunpack},
  {NULL, NULL}
};


LUALIB_API int luaopen_table (lua_State *L) {
  luaL_register(L, LUA_TABLIBNAME, tab_funcs);
  return 1;
}

