/*
** $Id: lstring.c,v 2.26.1.1 2013/04/12 18:48:47 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>
#include <limits.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif
#define ROPE_ALLOC_MIN_SIZE 32768



/*
** equality for long strings
*/
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->tsv.len;
  lua_assert(a->tsv.tt == LUA_TLNGSTR && b->tsv.tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->tsv.len) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}


/*
** equality for strings
*/
int luaS_eqstr (TString *a, TString *b) {
  return (a->tsv.tt == b->tsv.tt) &&
         (a->tsv.tt == LUA_TSHRSTR ? eqshrstr(a, b) : luaS_eqlngstr(a, b));
}


unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t l1;
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (l1 = l; l1 >= step; l1 -= step)
    h = h ^ ((h<<5) + (h>>2) + cast_byte(str[l1 - 1]));
  return h;
}


/*
** resizes the string table
*/
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  /* cannot resize while GC is traversing strings */
  luaC_runtilstate(L, ~bitmask(GCSsweepstring));
  if (newsize > tb->size) {
    luaM_reallocvector(L, tb->hash, tb->size, newsize, GCObject *);
    for (i = tb->size; i < newsize; i++) tb->hash[i] = NULL;
  }
  /* rehash */
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      GCObject *next = gch(p)->next;  /* save next */
      unsigned int h = lmod(gco2ts(p)->hash, newsize);  /* new position */
      gch(p)->next = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      resetoldbit(p);  /* see MOVE OLD rule */
      p = next;
    }
  }
  if (newsize < tb->size) {
    /* shrinking slice must be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, GCObject *);
  }
  tb->size = newsize;
}


/*
** creates a new string object
*/
static TString *createstrobj (lua_State *L, const char *str, size_t l,
                              int tag, unsigned int h, GCObject **list) {
  TString *ts;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizeof(TString) + ((l + 1) * sizeof(char));
  ts = &luaC_newobj(L, tag, totalsize, list, 0)->ts;
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.extra = 0;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  return ts;
}


/*
** creates a new short string, inserting it into string table
*/
static TString *newshrstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  GCObject **list;  /* (pointer to) list where it will be inserted */
  stringtable *tb = &G(L)->strt;
  TString *s;
  if (tb->nuse >= cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  list = &tb->hash[lmod(h, tb->size)];
  s = createstrobj(L, str, l, LUA_TSHRSTR, h, list);
  tb->nuse++;
  return s;
}


/*
** checks whether short string exists and reuses it or creates a new one
*/
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  global_State *g = G(L);
  unsigned int h = luaS_hash(str, l, g->seed);
  for (o = g->strt.hash[lmod(h, g->strt.size)];
       o != NULL;
       o = gch(o)->next) {
    TString *ts = rawgco2ts(o);
    if (h == ts->tsv.hash &&
        l == ts->tsv.len &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      if (isdead(G(L), o))  /* string is dead (but was not collected yet)? */
        changewhite(o);  /* resurrect it */
      return ts;
    }
  }
  return newshrstr(L, str, l, h);  /* not found; create a new string */
}


/*
** new string (with explicit length)
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    if (l + 1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    return createstrobj(L, str, l, LUA_TLNGSTR, G(L)->seed, NULL);
  }
}


/*
** new zero-terminated string
*/
TString *luaS_new (lua_State *L, const char *str) {
  return luaS_newlstr(L, str, strlen(str));
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = &luaC_newobj(L, LUA_TUSERDATA, sizeof(Udata) + s, NULL, 0)->u;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  return u;
}

TString *luaS_concat (lua_State *L, TString *l, TString *r) {
  TString *rope = NULL;
  TString *cluster, *next;
  bitmap_unit *bitmap;
  int i, j;
  global_State *g;
  g = G(L);
  for (cluster = g->ropefreecluster; rope == NULL; cluster = nextropecluster(cluster)) {
    bitmap = (bitmap_unit*)cluster + BITMAP_SKIP;
    /* search for unused entry in cluster */
    for (i = 0; i < ROPE_CLUSTER_SIZE / BITMAP_UNIT_SIZE; i++) {
      if (bitmap[i] != ULONG_MAX) {  /* empty space found? */
        for (j = 0; j < BITMAP_UNIT_SIZE - 1; j++) {  /* if j reaches max long, then it must be unused */
          if (!(bitmap[i] & (1 << j))) break;
        }
        rope = cluster + i * BITMAP_UNIT_SIZE + j;
        bitmap[i] |= (1 << j);
        break;
      }
    }
    if (rope != NULL) break;
    if (nextropecluster(cluster) == NULL) {  /* need new cluster? */
      next = luaM_newvector(L, ROPE_CLUSTER_SIZE, TString);
      memset(next, 0, ROPE_CLUSTER_SIZE * sizeof(TString));
      nextropecluster(cluster) = next;  /* chain next cluster in list */
      nextropecluster(next) = NULL;  /* ensure next pointer is NULL */
      rawclusterid(next) = (clusterid(cluster) + 1) | (g->currentwhite & bitmask(WHITE0BIT) ? 0 : ~CLUSTERID_MASK); /* set cluster number */
      ((bitmap_unit*)next)[BITMAP_SKIP] = 0xFFFF;  /* always mark first entry as used by bitmap */
      nextropecluster(cluster) = next;
    }
  }
  g->ropefreecluster = cluster;
  rope->tsr.marked = luaC_white(g);
  rope->tsr.tt = LUA_TROPSTR;
  rope->tsr.next = g->allgc;
  g->allgc = cast(GCObject *, rope);
  rope->tsr.cluster = cluster;
  rope->tsr.left = l;
  rope->tsr.right = r;
  rope->tsr.len = ((l->tsr.tt == LUA_TLNGSTR || l->tsr.tt == LUA_TSHRSTR) ? cast(TString *, l)->tsv.len : (l->tsr.tt == LUA_TSUBSTR ? cast(TString *, l)->tss.len : l->tsr.len)) +
                  ((r->tsr.tt == LUA_TLNGSTR || r->tsr.tt == LUA_TSHRSTR) ? cast(TString *, r)->tsv.len : (r->tsr.tt == LUA_TSUBSTR ? cast(TString *, r)->tss.len : r->tsr.len));
  rope->tsr.res = NULL;
  return rope;
}

TString *luaS_build (lua_State *L, TString *rope) {
  char *buffer, *cur;
  TString *s;
  TString **stack;
  TString *orig = rope;
  int freemem = 0;
  if (rope->tsr.tt == LUA_TSHRSTR || rope->tsr.tt == LUA_TLNGSTR || rope->tsr.tt == LUA_TSUBSTR) return cast(TString *, rope);
  if (rope->tsr.res || rope->tsr.left == NULL || rope->tsr.right == NULL) return rope->tsr.res;
  if (rope->tsr.len >= ROPE_ALLOC_MIN_SIZE) {
    buffer = cur = G(L)->frealloc(G(L)->ud, NULL, 0, rope->tsr.len * sizeof(char));
    freemem = 1;
  } else buffer = cur = luaZ_openspace(L, &G(L)->buff, rope->tsr.len);
  stack = G(L)->ropestack;
  do {
    int b = 0;
    while (rope->tsr.left->tsr.tt == LUA_TROPSTR && rope->tsr.left->tsr.res == NULL) {
      if (stack - G(L)->ropestack == G(L)->ropestacksize - 1) {
        TString **oldbase = G(L)->ropestack;
        luaM_reallocvector(L, G(L)->ropestack, G(L)->ropestacksize, G(L)->ropestacksize + G(L)->ropestacksize, TString *);
        stack = G(L)->ropestack + (stack - oldbase);
        G(L)->ropestacksize += G(L)->ropestacksize;
      }
      *stack++ = rope;
      rope = rope->tsr.left;
    }
    if (rope->tsr.left->tsr.tt == LUA_TSUBSTR) {
      memcpy(cur, getstr(cast(TString *, rope->tsr.left)->tss.str) + cast(TString *, rope->tsr.left)->tss.offset, cast(TString *, rope->tsr.left)->tss.len);
      cur += cast(TString *, rope->tsr.left)->tss.len;
    } else if (rope->tsr.left->tsr.tt == LUA_TSHRSTR || rope->tsr.left->tsr.tt == LUA_TLNGSTR) {
      memcpy(cur, getstr(cast(TString *, rope->tsr.left)), cast(TString *, rope->tsr.left)->tsv.len);
      cur += cast(TString *, rope->tsr.left)->tsv.len;
    } else {
      memcpy(cur, getstr(rope->tsr.left->tsr.res), rope->tsr.left->tsr.res->tsv.len);
      cur += rope->tsr.left->tsr.res->tsv.len;
    }
    while (rope->tsr.right->tsr.tt == LUA_TSHRSTR || rope->tsr.right->tsr.tt == LUA_TLNGSTR || rope->tsr.right->tsr.tt == LUA_TSUBSTR || rope->tsr.right->tsr.res) {
      if (rope->tsr.right->tsr.tt == LUA_TSUBSTR) {
        memcpy(cur, getstr(cast(TString *, rope->tsr.right)->tss.str) + cast(TString *, rope->tsr.right)->tss.offset, cast(TString *, rope->tsr.right)->tss.len);
        cur += cast(TString *, rope->tsr.right)->tss.len;
      } else if (rope->tsr.right->tsr.tt == LUA_TSHRSTR || rope->tsr.right->tsr.tt == LUA_TLNGSTR) {
        memcpy(cur, getstr(cast(TString *, rope->tsr.right)), cast(TString *, rope->tsr.right)->tsv.len);
        cur += cast(TString *, rope->tsr.right)->tsv.len;
      } else {
        memcpy(cur, getstr(rope->tsr.right->tsr.res), rope->tsr.right->tsr.res->tsv.len);
        cur += rope->tsr.right->tsr.res->tsv.len;
      }
      if (stack <= G(L)->ropestack) {b = 1; break;}
      rope = *--stack;
    }
    if (b) break;
    rope = rope->tsr.right;
  } while (stack >= G(L)->ropestack);
  s = luaS_newlstr(L, buffer, cur - buffer);
  if (freemem) {
    G(L)->frealloc(G(L)->ud, buffer, orig->tsr.len * sizeof(char), 0);
  }
  orig->tsr.res = s;
  orig->tsr.left = orig->tsr.right = NULL;  /* release left & right nodes (we don't need them anymore) */
  /* mark the string as black so it doesn't accidentally get freed */
  /* (apparently this is a problem?) */
  if (orig->tsr.marked & bitmask(BLACKBIT)) {
    resetbits(s->tsv.marked, WHITEBITS);
    setbits(s->tsv.marked, bitmask(BLACKBIT));
  }
  //luaC_step(L);  /* try to let the old rope get freed */
  return s;
}

void luaS_freerope (lua_State *L, TString *rope) {
  int idx = rope - rope->tsr.cluster;
  ((bitmap_unit*)rope->tsr.cluster)[BITMAP_SKIP + idx/BITMAP_UNIT_SIZE] &= ~(1 << (idx % BITMAP_UNIT_SIZE));  /* mark entry as freed */
  if (clusterid(rope->tsr.cluster) < clusterid(G(L)->ropefreecluster))
    G(L)->ropefreecluster = rope->tsr.cluster;
}

void luaS_freesubstr (lua_State *L, TString *ss) {
  int idx = ss - ss->tss.cluster;
  ((bitmap_unit*)ss->tss.cluster)[BITMAP_SKIP + idx/BITMAP_UNIT_SIZE] &= ~(1 << (idx % BITMAP_UNIT_SIZE));  /* mark entry as freed */
  if (clusterid(ss->tss.cluster) < clusterid(G(L)->ssfreecluster))
    G(L)->ssfreecluster = ss->tss.cluster;
}

static void freeropeclusters (lua_State *L) {
  TString *cluster, *last = NULL;
  bitmap_unit *bitmap;
  int i, empty, full, kept = 1, setfree = 0;
  for (cluster = G(L)->ropeclusters; cluster != NULL; last = cluster, cluster = nextropecluster(cluster)) {
    bitmap = (bitmap_unit*)cluster + BITMAP_SKIP;
    empty = (bitmap[0] & ~(bitmap_unit)(0xFFFF)) == 0;  /* ignore first entry use bit */
    full = bitmap[0] == ULONG_MAX;  /* ignore first entry use bit */
    for (i = 1; i < ROPE_CLUSTER_SIZE / BITMAP_UNIT_SIZE && (empty || full); i++) {
      if (bitmap[i]) {  /* any entry in use? */
        empty = 0;
      }
      if (bitmap[i] != ULONG_MAX) {  /* any entry *not* in use? */
        full = 0;
      }
    }
    if (empty) {  /* entire cluster unused? */
      if (kept) {
        kept--;  /* leave one empty cluster allocated */
        setfree = 1;
        G(L)->ropefreecluster = cluster;
      } else {
        /* unlink and free cluster */
        nextropecluster(last) = nextropecluster(cluster);
        if (G(L)->ropefreecluster == cluster)
          G(L)->ropefreecluster = nextropecluster(cluster);
        luaM_freemem(L, cluster, ROPE_CLUSTER_SIZE * sizeof(TString));
        cluster = last;
      }
    } else if (full && !setfree) {
      setfree = 1;
      G(L)->ropefreecluster = cluster;
    }
  }
}

static void freessclusters (lua_State *L) {
  TString *cluster, *last = NULL;
  bitmap_unit *bitmap;
  int i, empty, full, kept = 1, setfree = 0;
  for (cluster = G(L)->ssclusters; cluster != NULL; last = cluster, cluster = nextsscluster(cluster)) {
    bitmap = (bitmap_unit*)cluster + BITMAP_SKIP;
    empty = (bitmap[0] & ~(bitmap_unit)(0xFFFF)) == 0;  /* ignore first entry use bit */
    full = bitmap[0] == ULONG_MAX;  /* ignore first entry use bit */
    for (i = 1; i < SUBSTR_CLUSTER_SIZE / BITMAP_UNIT_SIZE && (empty || full); i++) {
      if (bitmap[i]) {  /* any entry in use? */
        empty = 0;
      }
      if (bitmap[i] != ULONG_MAX) {  /* any entry *not* in use? */
        full = 0;
      }
    }
    if (empty) {  /* entire cluster unused? */
      if (kept) {
        kept--;  /* leave one empty cluster allocated */
        setfree = 1;
        G(L)->ssfreecluster = cluster;
      } else {
        /* unlink and free cluster */
        nextsscluster(last) = nextsscluster(cluster);
        if (G(L)->ssfreecluster == cluster)
          G(L)->ssfreecluster = nextsscluster(cluster);
        luaM_freemem(L, cluster, SUBSTR_CLUSTER_SIZE * sizeof(TString));
        cluster = last;
      }
    } else if (full && !setfree) {
      setfree = 1;
      G(L)->ssfreecluster = cluster;
    }
  }
}

void luaS_freeclusters (lua_State *L) {
  freeropeclusters(L);
  freessclusters(L);
}

