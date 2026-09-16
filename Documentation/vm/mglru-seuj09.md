# Multi-Gen LRU (MGLRU) on Seuj09 ums9230 (tweaks)

## Status

MGLRU is **compiled in** (`CONFIG_LRU_GEN=y`) but **disabled by default**
(`CONFIG_LRU_GEN_ENABLED` is not set). The static key `lru_gen_caps[LRU_GEN_CORE]`
starts off, so boot uses the classic active/inactive LRU (+ FreeYond HybridSwap
and `CONFIG_LRU_BALANCE_BASE_THRASHING`).

Donor series: arter97 `android_kernel_oneplus_sm8350` (Yu Zhao MGLRU for 5.4).
Ported onto FreeYond/Unisoc 5.4.302 with HybridSwap `trace_android_vh_tune_scan_type`
kept immediately before `switch (scan_balance)` in `get_scan_count()`.

## Runtime enable (after full kill-switch/sysfs lands)

On mainline/arter97, enable via:

```
echo y > /sys/kernel/mm/lru_gen/enabled
```

Until the sysfs state-change path is fully seated on this tree, rebuild with
`CONFIG_LRU_GEN_ENABLED=y` to turn MGLRU on at boot (not recommended until the
remaining rmap / page-table-walk commits are ported and CI is green).

## Test matrix

| Case | Expectation |
|------|-------------|
| Boot with MGLRU off (default) | Device boots; HybridSwap healthy; no MGLRU lists in use |
| Idle RAM | Free memory stable; no unexpected kswapd storms |
| App switch / multitasking | Classic LRU reclaim path; LMK rate unchanged vs pre-port |
| HybridSwap under memory pressure | `trace_android_vh_tune_scan_type` still fires; zram/HybridSwap path OK |
| Enable MGLRU (when sysfs ready) | Pages move onto gen lists; reclaim via `lru_gen_shrink_lruvec` |
| LMK rate with MGLRU on | Compare kill counts vs off over same workload |

## Non-goals on this branch

- No maple_tree / per-VMA locks
- No `ZRAM_MULTI_COMP`
- No bpf work (other branches)

## Conflict / follow-up notes

Several later donor commits (rmap locality, page-table walks, multi-memcg,
thrashing prevention, debugfs, full kill-switch sysfs) need careful adaptation
to FreeYond `LRU_BALANCE_BASE_THRASHING` and the `shrink_node_memcg` topology.
See `/workspace/mglru-BLOCKER.md` if present for Debugger hand-off.
