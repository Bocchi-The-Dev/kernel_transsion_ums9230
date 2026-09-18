# Multi-Gen LRU (MGLRU) on Seuj09 ums9230 (tweaks)

## Status

MGLRU is **compiled in** (`CONFIG_LRU_GEN=y`) but **disabled by default**
(`CONFIG_LRU_GEN_ENABLED` is not set). The static key `lru_gen_caps[LRU_GEN_CORE]`
starts off, so boot uses the classic active/inactive LRU (+ FreeYond HybridSwap
and `CONFIG_LRU_BALANCE_BASE_THRASHING`).

Donor series: arter97 `android_kernel_oneplus_sm8350` (Yu Zhao MGLRU for 5.4).
Ported onto FreeYond/Unisoc 5.4.302 with HybridSwap `trace_android_vh_tune_scan_type`
kept immediately before `switch (scan_balance)` in `get_scan_count()`.

## Runtime enable

The real sysfs state-change path (`lru_gen_change_state()` +
`fill_evictable()`/`drain_evictable()`) has landed, so this now works without
a rebuild:

```
echo y > /sys/kernel/mm/lru_gen/enabled
cat /sys/kernel/mm/lru_gen/enabled
echo n > /sys/kernel/mm/lru_gen/enabled
```

Only the `LRU_GEN_CORE` bit does anything on this tree — the upstream
`LRU_GEN_MM_WALK`/`LRU_GEN_NONLEAF_YOUNG` bits gate page-table-walk code that
isn't ported here yet (see below), so `store_enable()` only ever flips core.

`CONFIG_LRU_GEN_ENABLED=y` still exists if you want it on at boot instead of
toggling at runtime, but that's no longer the only way to turn it on.

## Test matrix

| Case | Expectation |
|------|-------------|
| Boot with MGLRU off (default) | Device boots; HybridSwap healthy; no MGLRU lists in use |
| Idle RAM | Free memory stable; no unexpected kswapd storms |
| App switch / multitasking | Classic LRU reclaim path; LMK rate unchanged vs pre-port |
| HybridSwap under memory pressure | `trace_android_vh_tune_scan_type` still fires; zram/HybridSwap path OK |
| Enable MGLRU via `/sys/kernel/mm/lru_gen/enabled` | Pages move onto gen lists; reclaim via `lru_gen_shrink_lruvec` |
| LMK rate with MGLRU on | Compare kill counts vs off over same workload |

## Non-goals on this branch

- No maple_tree / per-VMA locks
- No `ZRAM_MULTI_COMP`
- No bpf work (other branches)

## Conflict / follow-up notes

Full kill-switch sysfs (this doc's previous blocker) landed in `04993b865` —
the runtime toggle above is real now, not compile-time only.

Still outstanding, and still needing careful adaptation to FreeYond
`LRU_BALANCE_BASE_THRASHING` and the `shrink_node_memcg` topology: rmap
locality (`exploit locality in rmap`), page-table walks (the actual scanning
mechanism — without it, aging only happens via `iterate_mm_list_nowalk()`,
not real page-table-driven aging), multi-memcg aging (per-node
`lru_gen_folio` lists), thrashing prevention (`min_ttl_ms`), and the debugfs
stats interface. These are deeply interdependent (each donor commit assumes
the previous ones' helpers exist) and are being ported one at a time,
verified via this repo's CI rather than a local kernel build.
See `/workspace/mglru-BLOCKER.md` if present for Debugger hand-off.
