/*
A minimal "reject-all" external kernel checker used to smoke-test the
`--external-checker-lib` integration. It rejects every declaration with a
`Kernel.Exception.other` message, exercising: library load, the versioned
populate handshake, dispatch from `lean_add_decl`, and error-table reporting.

The ABI structs below mirror `src/kernel/external_checker.h`. A real external
checker (e.g. nanoda) mirrors them the same way and is kept in sync via the
`abi_version` handshake. Core `lean.h` runtime functions (lean_dec,
lean_mk_string) are resolved against the host via RTLD_GLOBAL.
*/
#include <stddef.h>
#include <stdint.h>
#include <lean/lean.h>

#define LEAN_EXTERNAL_CHECKER_ABI_VERSION 1u

typedef struct {
    uint32_t abi_version;
    int32_t (*tick)(uint64_t n, int32_t * reason);
    lean_object * (*mk_ok)(lean_object * value);
    lean_object * (*mk_error)(lean_object * kernel_exc);
    lean_object * (*mk_kernel_exception)(uint32_t code, lean_object * env, lean_object * lctx,
        lean_object * name, lean_object * decl, lean_object * e0, lean_object * e1,
        lean_object * e2, lean_object * msg);
} lean_external_checker_host;

typedef struct {
    uint32_t abi_version;
    void * self;
    lean_object * (*add_decl)(void * self, lean_object * env, size_t max_heartbeat,
                              lean_object * decl, lean_object * opt_cancel_tk);
    lean_object * (*whnf)(void * self, lean_object * env, lean_object * lctx, lean_object * e);
    lean_object * (*check)(void * self, lean_object * env, lean_object * lctx, lean_object * e);
    lean_object * (*is_def_eq)(void * self, lean_object * env, lean_object * lctx,
                               lean_object * a, lean_object * b);
    void (*release)(void * self);
} lean_external_checker_callbacks;

static lean_external_checker_host const * g_host = NULL;

/* `env` is owned (consume it); `decl` and `opt_cancel_tk` are borrowed. */
static lean_object * reject_all_add_decl(void * self, lean_object * env, size_t max_heartbeat,
                                         lean_object * decl, lean_object * opt_cancel_tk) {
    (void) self; (void) max_heartbeat; (void) decl; (void) opt_cancel_tk;
    lean_dec(env);
    lean_object * msg = lean_mk_string("rejected by reject-all external checker");
    lean_object * exc = g_host->mk_kernel_exception(12, NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, msg);
    return g_host->mk_error(exc);
}

/* Default visibility so the host can `dlsym` it: leanc compiles with
   -fvisibility=hidden. (Rust's `#[no_mangle] pub extern "C"` exports by default.) */
__attribute__((visibility("default")))
uint32_t lean_external_check_populate_callbacks(lean_external_checker_host const * host,
                                                lean_external_checker_callbacks * out) {
    if (host == NULL || host->abi_version != LEAN_EXTERNAL_CHECKER_ABI_VERSION)
        return 1;  /* reject incompatible host */
    g_host = host;
    out->abi_version = LEAN_EXTERNAL_CHECKER_ABI_VERSION;
    out->self      = NULL;
    out->add_decl  = reject_all_add_decl;
    out->whnf      = NULL;
    out->check     = NULL;
    out->is_def_eq = NULL;
    out->release   = NULL;
    return 0;
}
