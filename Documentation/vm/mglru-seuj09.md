# Multi-Gen LRU (MGLRU) on Seuj09 ums9230 (tweaks)

## Status

MGLRU is **compiled in** (`CONFIG_LRU_GEN=y`) but **disabled by default**
(`CONFIG_LRU_GEN_ENABLED` is not set). The static key `lru_gen_caps[LRU_GEN_CORE]`
starts off, so boot uses the classic active/inactive LRU (+ FreeYond HybridSwap
and `CONFIG_LRU_BALANCE_BASE_THRASHING`).

Donor series: arter97 `android_kernel_oneplus_sm8350` (Yu Zhao MGLRU for 5.4).
Ported onto FreeYond/Unisoc 5.4.302 with HybridSwap `trace_android_vh_tune_scan_type`
kept immediately before `switch (scan_balance)` in `get_scan_count()`.

### Included on this tip

| Piece | Notes |
|-------|-------|
| Groundwork + minimal | Gen lists, refault path, kill-switch scaffolding |
| Unisoc 5.4 API adaptations | Build fixes for this tree |
| Kill-switch (real sysfs) | `lru_gen_change_state()` + fill/drain |
| Rmap locality | `lru_gen_look_around()` |
| Page table walks | Heap `lru_gen_mm` (KABI, no `mm_struct` growth); `arch_has_hw_pte_young()` is the generic `false` fallback on this tree (no HW AF), so normal aging uses `iterate_mm_list_nowalk()` + rmap look-around; full walks run when `full_scan` (debugfs) |
| Multi-memcg optimization | kswapd prefers memcgs that can drop clean file pages before aging/swapping |
| Thrashing prevention | `min_ttl_ms` (default **5000**); OOM if working set cannot be kept |
| Debugfs | `/sys/kernel/debug/lru_gen{,_full}` |
| Enabled caps wired | CORE + MM_WALK + NONLEAF_YOUNG static keys are real |

### Intentionally skipped

- arter97 "call simple LMK if enabled" — no `CONFIG_ANDROID_SIMPLE_LMK` here
- arter97 mega "merge v11..v15" commits — too coarse; port fixes individually
- Maple tree / per-VMA locks / `ZRAM_MULTI_COMP`

## Runtime enable

```
# turn MGLRU on (all supported caps) / off
echo y > /sys/kernel/mm/lru_gen/enabled
echo n > /sys/kernel/mm/lru_gen/enabled

# or a hex mask: bit0=CORE, bit1=MM_WALK, bit2=NONLEAF_YOUNG
echo 0x0001 > /sys/kernel/mm/lru_gen/enabled
cat /sys/kernel/mm/lru_gen/enabled
```

Thrashing prevention (on by default at 5000ms once MGLRU is enabled; set 0 to disable):

```
cat /sys/kernel/mm/lru_gen/min_ttl_ms
echo 1000 > /sys/kernel/mm/lru_gen/min_ttl_ms
cat /sys/kernel/mm/lru_gen/min_ttl_unsatisfied
```

## Debugfs

Requires `CONFIG_DEBUG_FS=y` (set in `unisoc_defconfig`).

```
# working-set estimate (anon/file sizes per gen + age in ms)
cat /sys/kernel/debug/lru_gen

# + tier / mm_stats detail
cat /sys/kernel/debug/lru_gen_full

# proactive aging / eviction (memcg_id from the dump above; nid=0 on !NUMA)
#   + <memcg_id> <nid> <seq> [swappiness] [full_scan]
#   - <memcg_id> <nid> <seq> [swappiness] [nr_to_reclaim]
echo '+ 0 0 <max_seq> 0 1' > /sys/kernel/debug/lru_gen
```

## Test matrix

| Case | Expectation |
|------|-------------|
| Boot with MGLRU off (default) | Device boots; HybridSwap healthy; classic LRU |
| `echo y > .../enabled` | Pages move onto gen lists; reclaim via `lru_gen_shrink_lruvec` |
| Idle RAM with MGLRU on | Free memory stable; no kswapd storms |
| App switch / multitasking | Compare LMK / swap vs off |
| HybridSwap under pressure | `trace_android_vh_tune_scan_type` still fires |
| `min_ttl_ms=5000` under force reclaim | Prefer OOM over thrashing; counter may bump |
| debugfs dump | Gens / ages / sizes look sane after enable |

## Non-goals on this branch

- No maple_tree / per-VMA locks
- No `ZRAM_MULTI_COMP`
- No bpf work (other branches)
- Do **not** enlarge `mm_struct` (keep heap `lru_gen_mm` / KABI pattern)

## Remaining gaps / known limits

- No HW Access Flag (`arch_has_hw_pte_young() == false`): aging relies on rmap
  `lru_gen_look_around()` unless debugfs forces `full_scan`.
- Direct-reclaim multi-memcg optimization is still kswapd-only (same as donor).
- Later arter97 improve/fix commits (cgroup migration crash fix, look_around
  simplify, walk_pmd improve, etc.) not yet evaluated — pick up if CI or
  runtime shows the matching bug.

## KABI / API notes for porters

- `mem_cgroup_lruvec(pgdat, memcg)` — this tree's argument order
- `scan_control` carries Unisoc thrashing bits (`anon_below_min`,
  `clean_below_{low,min}`) plus MGLRU `memcgs_*` flags
- HybridSwap hook in `get_scan_count()` must stay put
