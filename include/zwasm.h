/* zwasm-specific C extensions, subordinate to the standard wasm.h.
 *
 * STABILITY: zwasm is pre-1.0. These extension symbols and the wider
 * C ABI may change between releases until 1.0. The standard wasm.h
 * surface follows the upstream wasm-c-api interface.
 *
 * Instance-level sandboxing setters: per-instance budgets are set
 * post-instantiate and are mutable mid-workload (fuel, memory/table
 * ceilings, interrupt). The C API creates interpreter-backed instances
 * (the hardened default engine); JIT budgets are driven via the CLI.
 * All functions are null-tolerant (a null instance is a no-op).
 *
 * The WASI config family (zwasm_wasi_config_*, zwasm_store_set_wasi) is
 * declared in wasi.h; zwasm_instance_get_func is declared below.
 */
#ifndef ZWASM_H
#define ZWASM_H

#include <stdbool.h>
#include <stdint.h>

#include "wasm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Fuel (deterministic budget; interp units = instructions) ────────── */

/* Arm (or re-arm) the fuel budget. Exhaustion traps with kind
 * ZWASM_TRAP_OUT_OF_FUEL ("all fuel consumed"). */
WASM_API_EXTERN void zwasm_instance_set_fuel(wasm_instance_t*, uint64_t fuel);

/* Remove the budget (unmetered). */
WASM_API_EXTERN void zwasm_instance_disable_fuel(wasm_instance_t*);

/* Read the remaining fuel into *out; returns false when unmetered. */
WASM_API_EXTERN bool zwasm_instance_fuel_remaining(const wasm_instance_t*, uint64_t* out);

/* ── Memory cap (host ceiling below the declared/spec max) ───────────── */

/* memory.grow past `max_pages` (pages of memory 0's page size, 64 KiB by
 * default) returns the spec grow-failure (-1) — not a trap. */
WASM_API_EXTERN void zwasm_instance_set_memory_pages_limit(wasm_instance_t*, uint64_t max_pages);
WASM_API_EXTERN void zwasm_instance_clear_memory_pages_limit(wasm_instance_t*);

/* ── Cooperative interruption (cancel / host-driven timeout) ─────────── */

/* Callable from any thread; the running guest traps with kind
 * ZWASM_TRAP_INTERRUPTED at its next poll (function entry / loop
 * back-edge). Idempotent; clear before re-invoking. */
WASM_API_EXTERN void zwasm_instance_interrupt(wasm_instance_t*);
WASM_API_EXTERN void zwasm_instance_clear_interrupt(wasm_instance_t*);

/* ── Trap kind introspection ─────────────────────────────────────────── */

/* Machine-readable trap kind beside wasm.h's message-only surface; -1 on
 * NULL. Values mirror the `TrapKind` enum (src/api/trap_surface.zig), which is
 * append-only stable; a C host can switch on these without string-matching. */
#define ZWASM_TRAP_BINDING_ERROR 0
#define ZWASM_TRAP_UNREACHABLE 1
#define ZWASM_TRAP_DIV_BY_ZERO 2
#define ZWASM_TRAP_INT_OVERFLOW 3
#define ZWASM_TRAP_INVALID_CONVERSION 4
#define ZWASM_TRAP_OOB_MEMORY 5
#define ZWASM_TRAP_OOB_TABLE 6
#define ZWASM_TRAP_UNINITIALIZED_ELEM 7
#define ZWASM_TRAP_INDIRECT_CALL_MISMATCH 8
#define ZWASM_TRAP_STACK_OVERFLOW 9
#define ZWASM_TRAP_OUT_OF_MEMORY 10
#define ZWASM_TRAP_NULL_REFERENCE 11
#define ZWASM_TRAP_CAST_FAILURE 12
#define ZWASM_TRAP_UNCAUGHT_EXCEPTION 13
#define ZWASM_TRAP_UNALIGNED_ATOMIC 14
#define ZWASM_TRAP_EXPECTED_SHARED_MEMORY 15
#define ZWASM_TRAP_INTERRUPTED 16
#define ZWASM_TRAP_OUT_OF_FUEL 17
WASM_API_EXTERN int32_t zwasm_trap_kind(const wasm_trap_t*);

/* ── Instance helpers ────────────────────────────────────────────────── */

/* Resolve an instance + defined-function index into a fresh, owned func
 * handle — a convenience over wasm_instance_exports + wasm_extern_vec_t
 * indexing. Returns NULL on a null instance or an out-of-range index. The
 * caller owns the result and must release it with wasm_func_delete. */
WASM_API_EXTERN wasm_func_t* zwasm_instance_get_func(wasm_instance_t*, uint32_t idx);

/* ── Instruction trace (interpreter mode only) ──────────────────────── */

/* Per-instruction trace event. Emitted after each instruction when a trace
 * callback is set. operand_top is the stack top value AFTER the instruction
 * (valid when has_operand_top is non-zero). frame_depth is the call stack depth.
 * NOTE: single-instance only in current version. */
typedef struct {
    uint32_t pc;
    uint16_t op;
    uint8_t has_operand_top;
    uint8_t pad;
    int64_t operand_top_i64;
    uint32_t frame_depth;
    uint32_t current_func_idx;    // 当前执行函数全局索引，无法获取时为0xFFFFFFFF
    uint32_t call_target_func_idx; // call指令的目标函数索引，非call时为0xFFFFFFFF
    uint8_t has_mem;              // 是否有内存访问信息（load/store）
    uint8_t mem_op;               // 内存操作类型：0=无, 1=load, 2=store
    uint8_t mem_pad[2];
    uint32_t mem_addr;            // 内存访问地址
    uint64_t mem_val;             // 内存访问值（load=加载值，store=存储值）
} zwasm_trace_event_t;

/* Trace callback type. Invoked after each instruction with the event struct. */
typedef void (*zwasm_trace_callback_t)(void* ctx, const zwasm_trace_event_t* event);

/* Set per-instruction trace callback. Pass NULL to disable.
 * Only works with interpreter-mode instances (no-op on JIT). */
WASM_API_EXTERN void zwasm_instance_set_trace_cb(
    wasm_instance_t*,
    zwasm_trace_callback_t callback,
    void* ctx);

/* ── Engine selection ────────────────────────────────────────────────── */

/* Per-instance engine kind for zwasm_instance_new_ex. AUTO resolves to the
 * runtime's choice (currently the interpreter until the JIT host-import/WASI
 * bridge lands; documented to change without an API break). JIT forces the
 * native JIT (an explicit JIT on a JIT-less arch fails instantiation, returning
 * NULL — no silent downgrade). INTERP forces the interpreter. */
#define ZWASM_ENGINE_AUTO 0
#define ZWASM_ENGINE_JIT 1
#define ZWASM_ENGINE_INTERP 2

/* wasm_instance_new with a trailing per-instance engine selector (the stock
 * wasm_instance_new is AUTO). Same ownership/trap contract as wasm_instance_new:
 * NULL on null input / instantiation failure / OOM; a start-function trap is
 * written through trap_out (when non-NULL) with a NULL return. */
WASM_API_EXTERN wasm_instance_t* zwasm_instance_new_ex(
    wasm_store_t*, const wasm_module_t*, const wasm_extern_vec_t*,
    wasm_trap_t**, uint8_t engine_kind);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ZWASM_H */
