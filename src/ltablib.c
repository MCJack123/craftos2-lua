/*
** $Id: ltablib.c,v 1.38.1.3 2008/02/14 16:46:58 roberto Exp $
** Library for Table Manipulation
** See Copyright Notice in lua.h
*/


#include <stddef.h>
#include <string.h>

#define ltablib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#define aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))
#define aux_igetn(L,n,c,k)	(luaL_checktype(L, n, LUA_TTABLE), luaL_igetn(L, n, c, k))


#define TAB_R 1
#define TAB_W 2
#define TAB_L 4


static void tablelike (lua_State *L, int idx, int req) {
  if (!lua_istable(L, idx) && lua_getmetatable(L, idx)) {
    lua_getfield(L, -1, "__len");
    if (!(req & TAB_L) || lua_isfunction(L, -1)) {
      lua_getfield(L, -1, "__index");
      if (!(req & TAB_R) || lua_isfunction(L, -1)) {
        lua_getfield(L, -1, "__newindex");
        if (!(req & TAB_W) || lua_isfunction(L, -1)) {
          lua_pop(L, 4);
          return;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 2);
  }
  luaL_checktype(L, idx, LUA_TTABLE);
}

static int luaL_igetn (lua_State *L, int t, int ictx, lua_CFunction k) {
  int n, ctx = 0;
  if (ictx != 0 && lua_getctx(L, &ctx) == LUA_YIELD && ctx == ictx) goto resume;
  t = lua_absindex(L, t);
  /*lua_pushliteral(L, "n");  * try t.n *
  lua_rawget(L, t);
  if ((n = checkint(L, 1)) >= 0) return n;*/
  /*getsizes(L);  * else try sizes[t] *
  lua_pushvalue(L, t);
  lua_rawget(L, -2);
  if ((n = checkint(L, 2)) >= 0) return n;*/
  if (lua_getmetatable(L, t) && lua_istable(L, -1)) { /* else try __len metamethod */
    lua_getfield(L, -1, "__len");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, t);
      lua_callk(L, 1, 1, ictx, k);
  resume:
      if (lua_isnumber(L, -1)) {
        n = lua_tointeger(L, -1);
        lua_pop(L, 2);
        return n;
      }
    }
    lua_pop(L, 2);
  }
  return (int)lua_rawlen(L, t);
}

static void luaL_igeti (lua_State *L, int idx, int n, int ctx, lua_CFunction k) {
  lua_rawgeti(L, idx, n);
  if (lua_isnil(L, -1) && luaL_getmetafield(L, idx, "__index")) {
    lua_remove(L, -2);
    lua_pushvalue(L, idx);
    lua_pushinteger(L, n);
    lua_callk(L, 2, 1, ctx, k);
  }
}

static void luaL_iseti (lua_State *L, int idx, int n, int ctx, lua_CFunction k) {
  lua_rawgeti(L, idx, n);
  if (lua_isnil(L, -1) && luaL_getmetafield(L, idx, "__newindex")) {
    lua_remove(L, -2);
    lua_pushvalue(L, idx);
    lua_pushinteger(L, n);
    lua_pushvalue(L, -4);
    lua_remove(L, -5);
    lua_callk(L, 3, 0, ctx, k);
  } else {
    lua_pop(L, 1);
    lua_rawseti(L, idx, n);
  }
}


static int foreachi (lua_State *L) {
  int n, i;
  lua_getctx(L, &i);
  if (i > 0) {
    n = lua_tointeger(L, 3);  /* get cached n */
    if (i % 2) lua_callk(L, 2, 1, i-1, foreachi);
    i >>= 1;
    goto resume;
  }
  n = aux_igetn(L, 1, -1, foreachi);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);
  lua_pushinteger(L, n);  /* cache n because aux_igetn may be expensive */
  for (i=1; i <= n; i++) {
    lua_pushvalue(L, 2);  /* function */
    lua_pushinteger(L, i);  /* 1st argument */
    luaL_igeti(L, 1, i, i*2+1, foreachi);  /* 2nd argument */
    lua_callk(L, 2, 1, i*2, foreachi);
resume:
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 1);  /* remove nil result */
  }
  return 0;
}


static int foreach (lua_State *L) {
  if (lua_getctx(L, NULL) != LUA_OK) goto resume;
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushnil(L);  /* first key */
  while (lua_next(L, 1)) {
    lua_pushvalue(L, 2);  /* function */
    lua_pushvalue(L, -3);  /* key */
    lua_pushvalue(L, -3);  /* value */
    lua_callk(L, 2, 1, 1, foreach);
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
  lua_pushinteger(L, aux_igetn(L, 1, 1, getn));
  return 1;
}


static int setn (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
#ifndef luaL_setn
  //luaL_setn(L, 1, luaL_checkint(L, 2));
#else
  luaL_error(L, LUA_QL("setn") " is obsolete");
#endif
  lua_pushvalue(L, 1);
  return 1;
}


static int tinsert (lua_State *L) {
  int e;  /* first empty element */
  int pos;  /* where to insert new element */
  int ctx = 0;
  if (lua_getctx(L, &ctx) == LUA_YIELD) {
    if (ctx > 0) e = lua_tointeger(L, 4);
    else if (ctx == -2) return 0;
  } else e = aux_igetn(L, 1, -1, tinsert) + 1;
  tablelike(L, 1, TAB_L | TAB_R | TAB_W);
  switch (lua_gettop(L)) {
    case 2: {  /* called with only 2 arguments */
      pos = e;  /* insert new element at the end */
      break;
    }
    case 3: {
      int i;
      pos = luaL_checkint(L, 2);  /* 2nd argument is the position */
      if (pos > e) e = pos;  /* `grow' array if necessary */
      if (ctx) {
        i = ctx >> 1;
        if (ctx % 2) {
          luaL_iseti(L, 1, i, i * 2 - 2, tinsert);  /* t[i] = t[i-1] */
          i--;
        }
      } else {
        i = e;
        lua_pushinteger(L, e);
      }
      for (; i > pos; i--) {  /* move up elements */
        luaL_igeti(L, 1, i-1, i * 2 + 1, tinsert);
        luaL_iseti(L, 1, i, i * 2 - 2, tinsert);  /* t[i] = t[i-1] */
      }
      lua_pop(L, 1);
      break;
    }
    default: {
      return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
    }
  }
  //luaL_setn(L, 1, e);  /* new size */
  luaL_iseti(L, 1, pos, -2, tinsert);  /* t[pos] = v */
  return 0;
}


static int tremove (lua_State *L) {
  int e, pos, ctx = 0;
  if (lua_getctx(L, &ctx) == LUA_YIELD) {
    if (ctx == -2) return 1;
    else if (ctx == -3) {
      e = lua_tointeger(L, 3);
      pos = luaL_optint(L, 2, e);
      goto resume;
    }
    else if (ctx > 0) {
      e = lua_tointeger(L, 3);
      pos = ctx >> 1;
      if (ctx % 2) {
        luaL_iseti(L, 1, pos, pos * 2 + 2, tremove);  /* t[pos] = t[pos+1] */
        pos++;
      }
      goto resume;
    }
  }
  e = aux_igetn(L, 1, -1, tremove);
  pos = luaL_optint(L, 2, e);
  lua_settop(L, 2);
  lua_pushinteger(L, e);
  if (!(1 <= pos && pos <= e))  /* position is outside bounds? */
   return 0;  /* nothing to remove */
  //luaL_setn(L, 1, e - 1);  /* t.n = n-1 */
  luaL_igeti(L, 1, pos, -3, tremove);  /* result = t[pos] */
resume:
  for (; pos<e; pos++) {
    luaL_igeti(L, 1, pos+1, pos * 2 + 1, tremove);
    luaL_iseti(L, 1, pos, pos * 2 + 2, tremove);  /* t[pos] = t[pos+1] */
  }
  lua_pushnil(L);
  luaL_iseti(L, 1, e, -2, tremove);  /* t[e] = nil */
  return 1;
}


/*
** Copy elements (1[f], ..., 1[e]) into (tt[t], tt[t+1], ...). Whenever
** possible, copy in increasing order, which is better for rehashing.
** "possible" means destination after original range, or smaller
** than origin, or copying to another table.
*/
static int tmove (lua_State *L) {
  lua_Integer f = luaL_checkinteger(L, 2);
  lua_Integer e = luaL_checkinteger(L, 3);
  lua_Integer t = luaL_checkinteger(L, 4);
  int tt = !lua_isnoneornil(L, 5) ? 5 : 1;  /* destination table */
  int ctx = 0;
  lua_getctx(L, &ctx);
  tablelike(L, 1, TAB_R);
  tablelike(L, tt, TAB_W);
  if (e >= f) {  /* otherwise, nothing to move */
    lua_Integer n, i;
    luaL_argcheck(L, f > 0 || e < 0x7FFFFFFF + f, 3,
                  "too many elements to move");
    n = e - f + 1;  /* number of elements to move */
    luaL_argcheck(L, t <= 0x7FFFFFFF - n + 1, 4,
                  "destination wrap around");
    if (t > e || t <= f || (tt != 1 && !lua_rawequal(L, 1, tt))) {
      i = ctx >> 1;
      if (ctx % 2) {
        luaL_iseti(L, tt, t + i, i * 2 + 2, tmove);
        i++;
      }
      for (; i < n; i++) {
        luaL_igeti(L, 1, f + i, i * 2 + 1, tmove);
        luaL_iseti(L, tt, t + i, i * 2 + 2, tmove);
      }
    }
    else {
      i = ctx >> 1;
      if (ctx == 0) i = n - 1;
      else if (ctx == INT_MAX) i = 0;
      else if (ctx % 2) {
        luaL_iseti(L, tt, t + i, i == 1 ? INT_MAX : i * 2 - 2, tmove);
        i--;
      }
      for (; i >= 0; i--) {
        luaL_igeti(L, 1, f + i, i * 2 + 1, tmove);
        luaL_iseti(L, tt, t + i, i == 1 ? INT_MAX : i * 2 - 2, tmove);
      }
    }
  }
  lua_pushvalue(L, tt);  /* return destination table */
  return 1;
}


static int tconcat (lua_State *L);


static void addfield (lua_State *L, luaL_Buffer *b, int i) {
  luaL_igeti(L, 1, i, i + 1, tconcat);
  if (!lua_isstring(L, -1))
    luaL_error(L, "invalid value (%s) at index %d in table for "
                  LUA_QL("concat"), luaL_typename(L, -1), i);
  luaL_addvalue(b);
}


struct concat_state {
  luaL_Buffer b;
  int i, last;
};


int tconcat (lua_State *L) {
  struct concat_state * state = NULL;
  int i, last, ctx = 0;
  size_t lsep;
  const char *sep = luaL_optlstring(L, 2, "", &lsep);
  if (lua_getctx(L, &ctx) != LUA_OK && ctx != 1) {
    state = lua_touserdata(L, 5);
    if (!lua_isstring(L, -1))
      luaL_error(L, "invalid value (%s) at index %d in table for "
                  LUA_QL("concat"), luaL_typename(L, -1), state->i);
    luaL_addvalue(&state->b);
    if (state->i == state->last) {
      luaL_pushresult(&state->b);
      return 1;
    } else goto resume;
  }
  tablelike(L, 1, TAB_R | TAB_L);
  i = luaL_optint(L, 3, 1);
  if (!ctx && !state) lua_settop(L, 4);
  last = luaL_opt(L, luaL_checkint, 4, luaL_igetn(L, 1, 1, tconcat));
  state = lua_newuserdata(L, sizeof(struct concat_state));
  state->i = i; state->last = last;
  luaL_buffinit(L, &state->b);
  for (; state->i < state->last; state->i++) {
    addfield(L, &state->b, state->i);
resume:
    luaL_addlstring(&state->b, sep, lsep);
  }
  if (state->i == state->last)  /* add last value (if interval was not empty) */
    addfield(L, &state->b, state->i);
  luaL_pushresult(&state->b);
  return 1;
}



/*
** {======================================================
** Quicksort
** (based on `Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
*/


static int sort (lua_State *L);


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
    int id;
    int s;
    int d;
    int i;
    int j;
    struct table_sort_args * args;
};

static int l_strcmp (lua_State *L, int ls, int rs) {
  size_t ll, lr;
  const char *l = lua_tolstring(L, ls, &ll);
  const char *r = lua_tolstring(L, rs, &lr);
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* strings are equal up to a `\0' */
      size_t len = strlen(l);  /* index of first `\0' in both strings */
      if (len == lr)  /* r is finished? */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l is finished? */
        return -1;  /* l is smaller than r (because r is not finished) */
      /* both strings longer than `len'; go on comparing (after the `\0') */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}

static int sort_comp (lua_State *L, int a, int b, struct table_sort_state * s, int ss) {
  if (!lua_isnil(L, 2)) {  /* function? */
    int res;
    if (s->s) goto resume;
    s->s = ss;
    lua_pushvalue(L, 2);
    lua_pushvalue(L, a-1);  /* -1 to compensate function */
    lua_pushvalue(L, b-2);  /* -2 to compensate function and `a' */
    lua_callk(L, 2, 1, s->id, sort);
resume:
    res = lua_toboolean(L, -1);
    lua_pop(L, 1);
    s->s = 0;
    return res;
  }
  else { /* a < b? */
    /* return lua_lessthan(L, a, b); */
    int res;
    int t1 = lua_type(L, a);
    int t2 = lua_type(L, b);
    if (s->s) goto resume2;
    if (t1 != t2)
      return luaL_error(L, "attempt to compare %s with %s", lua_typename(L, t1), lua_typename(L, t2));
    else if (t1 == LUA_TNUMBER) 
      return lua_tonumber(L, a) < lua_tonumber(L, b);
    else if (t1 == LUA_TSTRING)
      return l_strcmp(L, a, b) < 0;
    else if (luaL_getmetafield(L, a, "__lt")) {
      if (luaL_getmetafield(L, b-1, "__lt")) {
        if (lua_rawequal(L, -2, -1)) {
          s->s = ss;
          lua_pop(L, 1);
          lua_pushvalue(L, a-1);  /* -1 to compensate function */
          lua_pushvalue(L, b-2);  /* -2 to compensate function and `a' */
          lua_callk(L, 2, 1, s->id, sort);
resume2:
          res = lua_toboolean(L, -1);
          lua_pop(L, 1);
          s->s = 0;
          return res;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    return luaL_error(L, "attempt to compare two %s values", lua_typename(L, t1));
  }
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
      case 0: break;
      case 1: goto resume1;
      case 2: goto resume2;
      case 3: goto resume3;
      case 4: goto resume4;
      case 5: goto resume5;
      case 6: goto resume6;
      case 7: goto resume7;
      case 8: goto resume8;
      case 9: goto resume9;
      case 10: goto resume10;
      case 11: goto resume11;
    }
    /* sort elements a[l], a[(l+u)/2] and a[u] */
    luaL_igeti(L, 1, a->l, 6, sort);
resume6:
    luaL_igeti(L, 1, a->u, 1, sort);
resume1:
    if (sort_comp(L, -1, -2, s, 1))  /* a[u] < a[l]? */
      set2(L, a->l, a->u);  /* swap a[l] - a[u] */
    else
      lua_pop(L, 2);
    if (a->u-a->l == 1) break;  /* only 2 elements */
    s->i = (a->l+a->u)/2;
    luaL_igeti(L, 1, s->i, 7, sort);
resume7:
    luaL_igeti(L, 1, a->l, 2, sort);
resume2:
    if (sort_comp(L, -2, -1, s, 2))  /* a[i]<a[l]? */
      set2(L, s->i, a->l);
    else {
      lua_pop(L, 1);  /* remove a[l] */
      luaL_igeti(L, 1, a->u, 3, sort);
resume3:
      if (sort_comp(L, -1, -2, s, 3))  /* a[u]<a[i]? */
        set2(L, s->i, a->u);
      else
        lua_pop(L, 2);
    }
    if (a->u-a->l == 2) break;  /* only 3 elements */
    luaL_igeti(L, 1, s->i, 8, sort);  /* Pivot */
resume8:
    lua_pushvalue(L, -1);
    luaL_igeti(L, 1, a->u-1, 9, sort);
resume9:
    set2(L, s->i, a->u-1);
    /* a[l] <= P == a[u-1] <= a[u], only need to sort from l+1 to u-2 */
    s->i = a->l; s->j = a->u-1;
    for (;;) {  /* invariant: a[l..i] <= P <= a[j..u] */
      /* repeat ++i until a[i] >= P */
      if (!s->s) luaL_igeti(L, 1, ++s->i, 4, sort);
resume4:
      while (sort_comp(L, -1, -2, s, 4)) {
        if (s->i>a->u) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[i] */
        if (!s->s) luaL_igeti(L, 1, ++s->i, 4, sort);
      }
      /* repeat --j until a[j] <= P */
      if (!s->s) luaL_igeti(L, 1, --s->j, 5, sort);
resume5:
      while (sort_comp(L, -3, -1, s, 5)) {
        if (s->j<a->l) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[j] */
        if (!s->s) luaL_igeti(L, 1, --s->j, 5, sort);
      }
      if (s->j<s->i) {
        lua_pop(L, 3);  /* pop pivot, a[i], a[j] */
        break;
      }
      set2(L, s->i, s->j);
    }
    luaL_igeti(L, 1, a->u-1, 10, sort);
resume10:
    luaL_igeti(L, 1, s->i, 11, sort);
resume11:
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
    a->next->l = s->j; a->next->u = s->i;
    a->next->next = NULL;
    auxsort(L, s, a->next, m + 1);  /* call recursively the smaller one */
    alloc(ud, a->next, sizeof(struct table_sort_args), 0);
    a->next = NULL;
  }  /* repeat the routine for the larger one */
  s->d--;
}

int sort (lua_State *L) {
  struct table_sort_state * s;
  struct table_sort_args *a, *aa;
  int n;
  void * ud = NULL;
  lua_Alloc alloc = lua_getallocf(L, &ud);
  if (lua_getctx(L, &n) != LUA_OK) {
    s = (struct table_sort_state*)lua_touserdata(L, 3);
    goto resume;
  }
  tablelike(L, 1, TAB_R | TAB_W | TAB_L);
  n = aux_igetn(L, 1, -1, sort);
  luaL_checkstack(L, 40, "");  /* assume array is smaller than 2^40 */
  if (!lua_isnoneornil(L, 2))  /* is there a 2nd argument? */
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);  /* make sure there is two arguments */
  s = (struct table_sort_state*)lua_newuserdata(L, sizeof(struct table_sort_state));
  s->args = (struct table_sort_args*)alloc(ud, NULL, 0, sizeof(struct table_sort_args));
  s->args->l = 1;
  s->args->u = n;
  s->args->next = NULL;
  s->s = s->d = s->i = s->j = 0;
resume:
  auxsort(L, s, s->args, 1);
  a = s->args;
  while (a) {
    aa = a;
    a = a->next;
    alloc(ud, aa, sizeof(struct table_sort_args), 0);
  }
  return 0;
}

/* }====================================================== */

static int tpack (lua_State *L) {
  int i, n = lua_gettop(L);
  lua_createtable(L, n, 1);
  lua_pushinteger(L, n);
  lua_setfield(L, -2, "n");
  for (i = 1; i <= n; i++) {
    lua_pushvalue(L, i);
    lua_rawseti(L, -2, i);
  }
  return 1;
}

int tunpack (lua_State *L) {
  int i, e, n, ctx = 0;
  luaL_checktype(L, 1, LUA_TTABLE);
  i = luaL_optint(L, 2, 1);
  lua_getctx(L, &ctx);
  if (ctx == 0) lua_settop(L, 3);
  else if (ctx != 1) {
    i = ctx;
    goto resume;
  }
  e = luaL_opt(L, luaL_checkint, 3, luaL_igetn(L, 1, 1, tunpack));
  if (i > e) return 0;  /* empty range */
  n = e - i + 1;  /* number of elements */
  if (n <= 0 || !lua_checkstack(L, n))  /* n <= 0 means arith. overflow */
    return luaL_error(L, "too many results to unpack");
  if (luaL_getmetafield(L, 1, "__index")) {  /* handle __index tables specially */
    lua_rawgeti(L, 1, i);  /* push arg[i] (avoiding overflow problems) */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushvalue(L, 4);
      lua_pushvalue(L, 1);
      lua_pushnumber(L, i);
      lua_callk(L, 2, 1, i + 1, tunpack);
    }
    while (i++ < e) {  /* push arg[i + 1...e] */
resume:
      lua_rawgeti(L, 1, i);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, 4);
        lua_pushvalue(L, 1);
        lua_pushnumber(L, i);
        lua_callk(L, 2, 1, i + 1, tunpack);
      }
    }
    lua_remove(L, 4);
  } else {  /* if no __index, we can go faster by not checking nils */
    lua_rawgeti(L, 1, i);  /* push arg[i] (avoiding overflow problems) */
    while (i++ < e)  /* push arg[i + 1...e] */
      lua_rawgeti(L, 1, i);
  }
  return n;
}


static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"foreach", foreach},
  {"foreachi", foreachi},
  {"getn", getn},
  {"maxn", maxn},
  {"move", tmove},
  {"insert", tinsert},
  {"remove", tremove},
  {"setn", setn},
  {"sort", sort},
  {"pack", tpack},
  {"unpack", tunpack},
  {NULL, NULL}
};


LUALIB_API int luaopen_table (lua_State *L) {
  luaL_newlib(L, tab_funcs);
  return 1;
}

