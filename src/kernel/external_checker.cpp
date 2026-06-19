/*
Copyright (c) 2026 Schrodinger ZHU Yifan. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Schrodinger ZHU Yifan
*/
#include <cstring>
#include "lean/lean.h"
#include "kernel/external_checker.h"

namespace lean {
/* The single registered external checker. Zero-initialized: `add_decl == nullptr`
   means "no external checker", so the builtin kernel path is used. */
static lean_external_checker_callbacks g_external_checker = {};

bool external_checker_active() {
    return g_external_checker.add_decl != nullptr;
}

lean_external_checker_callbacks const & get_external_checker() {
    return g_external_checker;
}
}

/* `tick` lives in runtime/interrupt.cpp (it needs the thread-local heartbeat and
   cancel token). The other host helpers are defined here. All are passed to the
   external checker through the host table below, never resolved by symbol. */
extern "C" int32_t lean_kernel_tick(uint64_t n, int32_t * reason);

extern "C" lean_object * lean_extern_mk_ok(lean_object * value) {
    lean_object * c = lean_alloc_ctor(1, 1, 0);  /* Except.ok */
    lean_ctor_set(c, 0, value);
    return c;
}

extern "C" lean_object * lean_extern_mk_error(lean_object * kernel_exc) {
    lean_object * c = lean_alloc_ctor(0, 1, 0);  /* Except.error */
    lean_ctor_set(c, 0, kernel_exc);
    return c;
}

extern "C" lean_object * lean_extern_mk_kernel_exception(
        uint32_t code, lean_object * env, lean_object * lctx, lean_object * name,
        lean_object * decl, lean_object * e0, lean_object * e1, lean_object * e2,
        lean_object * msg) {
    lean_object * c;
    switch (code) {
    case 0:  /* unknownConstant env name */
        c = lean_alloc_ctor(0, 2, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, name);
        return c;
    case 1:  /* alreadyDeclared env name */
        c = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, name);
        return c;
    case 2:  /* declTypeMismatch env decl givenType */
        c = lean_alloc_ctor(2, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, decl); lean_ctor_set(c, 2, e0);
        return c;
    case 3:  /* declHasMVars env name expr */
        c = lean_alloc_ctor(3, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, name); lean_ctor_set(c, 2, e0);
        return c;
    case 4:  /* declHasFVars env name expr */
        c = lean_alloc_ctor(4, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, name); lean_ctor_set(c, 2, e0);
        return c;
    case 5:  /* funExpected env lctx expr */
        c = lean_alloc_ctor(5, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, e0);
        return c;
    case 6:  /* typeExpected env lctx expr */
        c = lean_alloc_ctor(6, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, e0);
        return c;
    case 7:  /* letTypeMismatch env lctx name givenType expectedType */
        c = lean_alloc_ctor(7, 5, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, name);
        lean_ctor_set(c, 3, e0); lean_ctor_set(c, 4, e1);
        return c;
    case 8:  /* exprTypeMismatch env lctx expr expectedType */
        c = lean_alloc_ctor(8, 4, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, e0);
        lean_ctor_set(c, 3, e1);
        return c;
    case 9:  /* appTypeMismatch env lctx app funType argType */
        c = lean_alloc_ctor(9, 5, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, e0);
        lean_ctor_set(c, 3, e1); lean_ctor_set(c, 4, e2);
        return c;
    case 10: /* invalidProj env lctx proj */
        c = lean_alloc_ctor(10, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, lctx); lean_ctor_set(c, 2, e0);
        return c;
    case 11: /* thmTypeIsNotProp env name type */
        c = lean_alloc_ctor(11, 3, 0);
        lean_ctor_set(c, 0, env); lean_ctor_set(c, 1, name); lean_ctor_set(c, 2, e0);
        return c;
    case 12: /* other msg */
        c = lean_alloc_ctor(12, 1, 0);
        lean_ctor_set(c, 0, msg);
        return c;
    case 13: case 14: case 15: case 16:
        /* deterministicTimeout / excessiveMemory / deepRecursion / interrupted: nullary */
        return lean_box(code);
    default:
        return lean_box(16);  /* defensive: treat unknown as interrupted */
    }
}

/* The host callback table handed to the external checker at registration. */
static lean_external_checker_host const g_host = {
    LEAN_EXTERNAL_CHECKER_ABI_VERSION,
    &lean_kernel_tick,
    &lean_extern_mk_ok,
    &lean_extern_mk_error,
    &lean_extern_mk_kernel_exception,
};

extern "C" LEAN_EXPORT uint8_t lean_register_external_checker(void * populate_sym) {
    if (populate_sym == nullptr)
        return 0;
    auto populate = reinterpret_cast<lean_external_check_populate_fn>(populate_sym);
    lean_external_checker_callbacks cbs;
    std::memset(&cbs, 0, sizeof(cbs));
    uint32_t rc = populate(&g_host, &cbs);
    if (rc != 0 || cbs.abi_version != LEAN_EXTERNAL_CHECKER_ABI_VERSION || cbs.add_decl == nullptr)
        return 0;
    lean::g_external_checker = cbs;
    return 1;
}
