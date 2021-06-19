/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"



void luaS_resize (lua_State *L, int newsize) {
  GCObject **newhash;
  stringtable *tb;
  int i;
  if (G(L)->gcstate == GCSsweepstring)
    return;  /* cannot resize during GC traverse */
  newhash = luaM_newvector(L, newsize, GCObject *);
  tb = &G(L)->strt;
  for (i=0; i<newsize; i++) newhash[i] = NULL;
  /* rehash */
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
    while (p > (GCObject*)256) {  /* for each node in the list */
      GCObject *next = p->gch.next;  /* save next */
      unsigned int h = gco2ts(p)->hash;
      int h1 = lmod(h, newsize);  /* new position */
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->gch.next = newhash[h1];  /* chain it */
      newhash[h1] = p;
      p = next;
    }
  }
  luaM_freearray(L, tb->hash, tb->size, TString *);
  tb->size = newsize;
  tb->hash = newhash;
}


static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  TString *ts;
  stringtable *tb;
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  ts = cast(TString *, luaM_malloc(L, (l+1)*sizeof(char)+sizeof(TString)));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.marked = luaC_white(G(L));
  ts->tsv.tt = LUA_TSTRING;
  ts->tsv.reserved = 0;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  tb = &G(L)->strt;
  h = lmod(h, tb->size);
  ts->tsv.next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = obj2gco(ts);
  tb->nuse++;
  if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  return ts;
}


TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
    TString *ts = rawgco2ts(o);
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      /* string may be dead */
      if (isdead(G(L), o)) changewhite(o);
      return ts;
    }
  }
  return newlstr(L, str, l, h);  /* not found */
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
  u->uv.marked = luaC_white(G(L));  /* is not finalized */
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  /* chain it on udata list (after main thread) */
  u->uv.next = G(L)->mainthread->next;
  G(L)->mainthread->next = obj2gco(u);
  return u;
}

TRope *luaS_concat (lua_State *L, TRope *l, TRope *r) {
  TRope *rope;
  rope = cast(TRope *, luaM_malloc(L, sizeof(TRope)));
  luaC_link(L, obj2gco(rope), LUA_TROPE);
  rope->tsr.left = l;
  rope->tsr.right = r;
  rope->tsr.parent = NULL;
  rope->tsr.len = (l->tsr.tt == LUA_TSTRING ? cast(TString *, l)->tsv.len : l->tsr.len) + (r->tsr.tt == LUA_TSTRING ? cast(TString *, r)->tsv.len : r->tsr.len);
  return rope;
}

TString *luaS_build (lua_State *L, TRope *rope) {
  char *buffer, *cur;
  TString *s;
  TRope **stack, **base;
  size_t stacksize = 8;
  if (rope->tsr.tt == LUA_TSTRING) return cast(TString *, rope);
  buffer = cur = luaZ_openspace(L, &G(L)->buff, rope->tsr.len);
  base = stack = luaM_newvector(L, stacksize, TRope *);
  do {
    int b = 0;
    while (rope->tsr.left->tsr.tt == LUA_TROPE) {
      if (stack - base == stacksize - 1) {
        TRope **oldbase = base;
        luaM_reallocvector(L, base, stacksize, stacksize + stacksize, TRope *);
        stack = base + (stack - oldbase);
        stacksize += stacksize;
      }
      *stack++ = rope;
      rope = rope->tsr.left;
    }
    memcpy(cur, getstr(cast(TString *, rope->tsr.left)), cast(TString *, rope->tsr.left)->tsv.len);
    cur += cast(TString *, rope->tsr.left)->tsv.len;
    while (rope->tsr.right->tsr.tt == LUA_TSTRING) {
      memcpy(cur, getstr(cast(TString *, rope->tsr.right)), cast(TString *, rope->tsr.right)->tsv.len);
      cur += cast(TString *, rope->tsr.right)->tsv.len;
      if (stack <= base) {b = 1; break;}
      rope = *--stack;
    }
    if (b) break;
    rope = rope->tsr.right;
  } while (stack >= base);
  luaM_freearray(L, base, stacksize, TRope *);
  s = luaS_newlstr(L, buffer, cur - buffer);
  return s;
}

void luaS_freerope (lua_State *L, TRope *rope) {
  if (rope->tsr.tt == LUA_TSTRING) {
    resetbit(cast(TString *, rope)->tsv.marked, FIXEDBIT);  /* let the GC handle the string value next cycle */
  } else {
    luaM_free(L, rope);
  }
}

