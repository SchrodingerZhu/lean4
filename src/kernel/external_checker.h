/*
Copyright (c) 2026 Schrodinger ZHU Yifan. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Schrodinger ZHU Yifan
*/
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lean/lean.h"

/* ABI contract for a pluggable external kernel type-checker loaded via
   `--external-checker-lib=PATH`. The external library exports exactly one
   symbol, `lean_external_check_populate_callbacks`, which receives the host
   callback table and fills the checker callback table.

   Both tables carry an `abi_version`, so a single version constant governs both
   directions of the contract: the checker rejects a host whose version it does
   not support, and the host rejects a checker whose version it does not support.
   Bump the version on ANY change to either table below. */
#define LEAN_EXTERNAL_CHECKER_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* Host -> checker callbacks. Built by the host and passed to `populate`. These
   replace symbol-resolved helpers so the version handshake covers them too.
   (Core `lean.h` object operations remain symbol-resolved: they are the
   universal Lean runtime ABI, not part of this checker contract.) */
typedef struct {
    uint32_t abi_version;     /* = LEAN_EXTERNAL_CHECKER_ABI_VERSION */

    /* Bump the thread-local heartbeat by `n`; return 0 to continue, else an abort
       reason matching a Kernel.Exception tag (13 = deterministicTimeout,
       16 = interrupted). Writes the reason to `*reason` if non-NULL. Never throws. */
    int32_t (*tick)(uint64_t n, int32_t * reason);

    /* Wrap a checked value as `Except.ok value` (consumes `value`). */
    lean_object * (*mk_ok)(lean_object * value);
    /* Wrap an exception as `Except.error kernel_exc` (consumes `kernel_exc`). */
    lean_object * (*mk_error)(lean_object * kernel_exc);
    /* Build a `Kernel.Exception` (constructor index `code`, 0..16). Consumes the
       non-NULL payload slots required by `code`; pass NULL for unused slots. The
       code<->payload mapping mirrors `catch_kernel_exceptions` in kernel_exception.h. */
    lean_object * (*mk_kernel_exception)(uint32_t code, lean_object * env, lean_object * lctx,
        lean_object * name, lean_object * decl, lean_object * e0, lean_object * e1,
        lean_object * e2, lean_object * msg);

    /* Look up a constant in the kernel environment for lazy import. `env` and
       `name` are borrowed; returns an owned `Option ConstantInfo` (a boxed
       `none`, or `some` wrapping the ConstantInfo). Lets the external checker
       pull in constants it has not yet imported (e.g. from `.olean` imports). */
    lean_object * (*find_const)(lean_object * env, lean_object * name);

    /* Add a declaration using the BUILTIN kernel, bypassing the external
       checker. Same contract as `lean_add_decl` (consumes `env`, borrows `decl`/
       `opt_cancel_tk`, returns owned `Except Kernel.Exception Environment`). The
       external checker delegates declaration kinds it does not handle (inductive,
       quotient, mutual) so the builtin kernel generates their recursors. */
    lean_object * (*builtin_add_decl)(lean_object * env, size_t max_heartbeat,
                                      lean_object * decl, lean_object * opt_cancel_tk);
} lean_external_checker_host;

/* Checker -> host callbacks. Filled by `populate`.

   All `lean_object *` follow standard Lean FFI ownership, matching the host
   functions they mirror (`lean_add_decl`, `lean_kernel_*`): `add_decl` takes
   ownership of `env` (consumes it) and borrows `decl`/`opt_cancel_tk`; it returns
   an owned `Except Kernel.Exception Environment`. Callbacks must NOT throw or
   unwind across the FFI boundary. */
typedef struct {
    uint32_t abi_version;     /* checker sets = LEAN_EXTERNAL_CHECKER_ABI_VERSION */
    void *   self;            /* checker instance; host treats as opaque */

    lean_object * (*add_decl)(void * self, lean_object * env, size_t max_heartbeat,
                              lean_object * decl, lean_object * opt_cancel_tk);

    /* Optional fast-path primitives (any may be NULL -> host keeps builtin).
       Signatures mirror lean_kernel_whnf / lean_kernel_check / lean_kernel_is_def_eq. */
    lean_object * (*whnf)     (void * self, lean_object * env, lean_object * lctx, lean_object * e);
    lean_object * (*check)    (void * self, lean_object * env, lean_object * lctx, lean_object * e);
    lean_object * (*is_def_eq)(void * self, lean_object * env, lean_object * lctx,
                               lean_object * a, lean_object * b);

    void (*release)(void * self);  /* teardown at shutdown; may be NULL */
} lean_external_checker_callbacks;

/* The one symbol the external `.so` must export. The host passes its callback
   table; the checker validates `host->abi_version`, stores `host`, and fills
   `*out` (including `out->abi_version`). Returns 0 to accept, nonzero to reject. */
typedef uint32_t (*lean_external_check_populate_fn)(
    lean_external_checker_host const * host, lean_external_checker_callbacks * out);

/* Host-internal: invoke the populate symbol and register the checker.
   Returns 1 on success, 0 if the checker rejected the handshake or is
   incompatible. Called by the dynlib loader, not by the external checker. */
uint8_t lean_register_external_checker(void * populate_sym);

#ifdef __cplusplus
}

namespace lean {
/* True iff an external checker is registered and provides `add_decl`. */
bool external_checker_active();
/* The registered callback table (valid only when `external_checker_active()`). */
lean_external_checker_callbacks const & get_external_checker();
}
#endif
