/*
 * error_info.c
 * craftos2-lua
 * 
 * This file implements the methods for the cc.internal.error_info module.
 * 
 * This code is licensed under the Mozilla Public License version 2.0.
 * Copyright (c) 2019-2024 JackMacWindows.
 * Copyright (c) 2025 The CC: Tweaked Developers
 */

#define error_info_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"

#include "ldebug.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"

/* This code is adapted from https://github.com/cc-tweaked/CC-Tweaked/blob/mc-1.20.x/projects/core/src/main/java/dan200/computercraft/core/lua/errorinfo/ErrorInfoLib.java */

#define MAX_DEPTH 8
/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100


#define resolverope(L, o) {if (ttisrope(o)) setsvalue(L, o, luaS_build(L, rawtrvalue(o)));}
#define resolvesubstr(L, o) {if (ttissubstr(o)) setsvalue(L, o, luaS_newlstr(L, getstr(ssvalue(o)->str) + ssvalue(o)->offset, ssvalue(o)->len));}

static int gettableSafe (lua_State *L, const TValue *t, TValue *key, StkId val) {
    int loop;
    resolverope(L, key);
    resolvesubstr(L, key);
    for (loop = 0; loop < MAXTAGLOOP; loop++) {
        const TValue *tm;
        if (ttistable(t)) {  /* `t' is a table? */
            Table *h = hvalue(t);
            const TValue *res = luaH_get(L, h, key); /* do a primitive get */
            if (!ttisnil(res) ||  /* result is not nil? */
                (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) { /* or no TM? */
                setobj2s(L, val, res);
                return 0;
            }
            /* else will try the tag method */
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
            return -1;
        if (ttisfunction(t))
            return -1;
        t = tm;  /* else repeat with 'tm' */
    }
    return -1;
}

static int evaluate(lua_State *L, CallInfo *ci, Proto *p, int pc, int reg, int depth, TValue *res) {
    if (depth > MAX_DEPTH) return -1;
    
    if (luaF_getlocalname(p, reg + 1, pc) != NULL) {
        setobj2s(L, res, ci->top + reg);
        return 0;
    }

    pc = luaG_findsetreg(p, pc, reg);
    if (pc == -1) return -1;

    Instruction inst = p->code[pc];
    int opcode = GET_OPCODE(inst);
    switch (opcode) {
        case OP_MOVE: {
            int a = GETARG_A(inst);
            int b = GETARG_B(inst);
            if (b < a) return evaluate(L, ci, p, pc, reg, depth + 1, res);
            else return -1;
        } case OP_LOADK: *res = p->k[GETARG_Bx(inst)]; return 0;
        case OP_LOADKX: *res = p->k[GETARG_Ax(p->code[pc + 1])]; return 0;
        case OP_LOADBOOL: setbvalue(res, GETARG_B(inst) != 0); return 0;
        case OP_LOADNIL: setnilvalue(res); return 0;
        case OP_GETUPVAL: setobj2s(L, res, ci_func(ci)->upvals[GETARG_B(inst)]->v); return 0;
        case OP_GETTABLE: case OP_GETTABUP: {
            TValue table;
            if (opcode == OP_GETTABUP) setobj2n(L, &table, ci_func(ci)->upvals[GETARG_B(inst)]->v)
            else if (evaluate(L, ci, p, pc, GETARG_B(inst), depth + 1, &table)) return -1;
            lua_assert(ttistable(&table));

            TValue key;
            if (ISK(GETARG_C(inst))) setobj2n(L, &key, p->k + INDEXK(GETARG_C(inst)))
            else if (evaluate(L, ci, p, pc, GETARG_C(inst), depth + 1, &key)) return -1;

            return gettableSafe(L, &table, &key, res);
        } default: return -1;
    }
}

static int resolveValueSource(lua_State *L, CallInfo *ci, Proto *p, int pc, int reg, int depth) {
    if (depth > MAX_DEPTH) return -1;
    if (luaF_getlocalname(p, reg + 1, pc) != NULL) return -1;

    pc = luaG_findsetreg(p, pc, reg);
    if (pc == -1) return -1;

    Instruction inst = p->code[pc];
    switch (GET_OPCODE(inst)) {
        case OP_MOVE: {
            int a = GETARG_A(inst);
            int b = GETARG_B(inst);
            if (b < a) return resolveValueSource(L, ci, p, pc, reg, depth + 1);
            else return -1;
        } case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
            int tableIndex = GETARG_B(inst);
            int keyIndex = GETARG_C(inst);
            if (!ISK(keyIndex)) return -1;

            TValue *key = p->k + INDEXK(keyIndex);
            if (!ttisstring(key)) return -1;

            if (GET_OPCODE(inst) == OP_GETTABUP) {
                setbvalue(L->top, luaS_eqstr(p->upvalues[tableIndex].name, luaS_new(L, LUA_ENV)));
                setobj2s(L, L->top + 1, ci_func(ci)->upvals[tableIndex]->v);
            } else {
                setbvalue(L->top, 0);
                if (evaluate(L, ci, p, pc, tableIndex, depth, L->top + 1)) return -1;
            }
            setobjs2s(L, L->top + 2, key);
            L->top += 3;
            return 0;
        } default: return -1;
    }
}

static int error_info_info_for_nil(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTHREAD);
    lua_State *thread = lua_tothread(L, 1);
    int level = luaL_checkinteger(L, 2);
    lua_Debug ar;
    if (!lua_getstack(thread, level, &ar) || !isLfunction(ar.i_ci->func) || (ar.i_ci->callstatus & CIST_HOOKED)) return 0;
    luaL_checkstack(L, 4, "stack overflow");
    
    lua_lock(L);
    Proto *p = ci_func(ar.i_ci)->p;
    const Instruction *pc = ar.i_ci->u.l.savedpc - 1;
    Instruction inst = *pc;

    switch (GET_OPCODE(inst)) {
        case OP_CALL: case OP_TAILCALL: {
            luaC_checkGC(L);
            TString *str = luaS_new(L, "call");
            setsvalue2s(L, L->top, str);
            L->top++;
            if (resolveValueSource(L, ar.i_ci, p, pcRel(pc, p) + 1, GETARG_A(inst), 0)) {
                lua_unlock(L);
                return 0;
            }
            lua_unlock(L);
            return 4;
        } case OP_GETTABLE: case OP_SETTABLE: case OP_SELF: {
            luaC_checkGC(L);
            TString *str = luaS_new(L, "index");
            setsvalue2s(L, L->top, str);
            L->top++;
            if (resolveValueSource(L, ar.i_ci, p, pcRel(pc, p) + 1, GETARG_A(inst), 0)) {
                lua_unlock(L);
                return 0;
            }
            lua_unlock(L);
            return 4;
        } default:
            lua_unlock(L);
            return 0;
    }
}

static luaL_Reg error_info_reg[] = {
    {"info_for_nil", error_info_info_for_nil},
    {NULL, NULL}
};

LUAMOD_API int luaopen_error_info(lua_State *L) {
    luaL_newlib(L, error_info_reg);
    return 1;
}
