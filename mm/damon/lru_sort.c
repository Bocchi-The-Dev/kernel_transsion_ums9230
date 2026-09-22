// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based LRU-lists Sorting
 *
 * Author: SeongJae Park <sj@kernel.org>
 *
 * Adapted for this tree's DAMON (reclaim-era API): no after_wmarks_check,
 * damon_start(2-arg), damon_new_target(id), region setup like reclaim.c.
 * Default disabled at runtime.
 */

#define pr_fmt(fmt) "damon-lru-sort: " fmt

#include <linux/damon.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_lru_sort."

/*
 * Enable or disable DAMON_LRU_SORT.
 *
 * Default off. echo Y > /sys/module/damon_lru_sort/parameters/enabled
 */
static bool enabled __read_mostly;
module_param(enabled, bool, 0600);

/*
 * Access frequency threshold for hot memory regions identification in permil.
 * 500 means 50% of max accesses by default.
 */
static unsigned long hot_thres_access_freq __read_mostly = 500;
module_param(hot_thres_access_freq, ulong, 0600);

/*
 * Time threshold for cold memory regions identification in microseconds.
 * 120 seconds by default.
 */
static unsigned long cold_min_age __read_mostly = 120000000;
module_param(cold_min_age, ulong, 0600);

/* Limit of time for trying the LRU sorting in milliseconds (10ms default). */
static unsigned long quota_ms __read_mostly = 10;
module_param(quota_ms, ulong, 0600);

/*
 * The time quota charge reset interval in milliseconds (1s default).
 */
static unsigned long quota_reset_interval_ms __read_mostly = 1000;
module_param(quota_reset_interval_ms, ulong, 0600);

/* Watermarks check interval in microseconds (5s default). */
static unsigned long wmarks_interval __read_mostly = 5000000;
module_param(wmarks_interval, ulong, 0600);

/* Free memory rate (per thousand) for the high watermark (200 default). */
static unsigned long wmarks_high __read_mostly = 200;
module_param(wmarks_high, ulong, 0600);

/* Free memory rate (per thousand) for the middle watermark (150 default). */
static unsigned long wmarks_mid __read_mostly = 150;
module_param(wmarks_mid, ulong, 0600);

/* Free memory rate (per thousand) for the low watermark (50 default). */
static unsigned long wmarks_low __read_mostly = 50;
module_param(wmarks_low, ulong, 0600);

/* Sampling interval for monitoring (5ms default). */
static unsigned long sample_interval __read_mostly = 5000;
module_param(sample_interval, ulong, 0600);

/* Aggregation interval for monitoring (100ms default). */
static unsigned long aggr_interval __read_mostly = 100000;
module_param(aggr_interval, ulong, 0600);

/* Minimum number of monitoring regions (10 default). */
static unsigned long min_nr_regions __read_mostly = 10;
module_param(min_nr_regions, ulong, 0600);

/* Maximum number of monitoring regions (1000 default). */
static unsigned long max_nr_regions __read_mostly = 1000;
module_param(max_nr_regions, ulong, 0600);

/* Start of physical address space region to monitor (0 = auto). */
static unsigned long monitor_region_start __read_mostly;
module_param(monitor_region_start, ulong, 0600);

/* End of physical address space region to monitor (0 = auto). */
static unsigned long monitor_region_end __read_mostly;
module_param(monitor_region_end, ulong, 0600);

/* PID of the kdamond thread (-1 means inactive). */
static int kdamond_pid __read_mostly = -1;
module_param(kdamond_pid, int, 0400);

static unsigned long nr_lru_sort_tried_hot_regions __read_mostly;
module_param(nr_lru_sort_tried_hot_regions, ulong, 0400);
static unsigned long bytes_lru_sort_tried_hot_regions __read_mostly;
module_param(bytes_lru_sort_tried_hot_regions, ulong, 0400);
static unsigned long nr_lru_sorted_hot_regions __read_mostly;
module_param(nr_lru_sorted_hot_regions, ulong, 0400);
static unsigned long bytes_lru_sorted_hot_regions __read_mostly;
module_param(bytes_lru_sorted_hot_regions, ulong, 0400);
static unsigned long nr_hot_quota_exceeds __read_mostly;
module_param(nr_hot_quota_exceeds, ulong, 0400);

static unsigned long nr_lru_sort_tried_cold_regions __read_mostly;
module_param(nr_lru_sort_tried_cold_regions, ulong, 0400);
static unsigned long bytes_lru_sort_tried_cold_regions __read_mostly;
module_param(bytes_lru_sort_tried_cold_regions, ulong, 0400);
static unsigned long nr_lru_sorted_cold_regions __read_mostly;
module_param(nr_lru_sorted_cold_regions, ulong, 0400);
static unsigned long bytes_lru_sorted_cold_regions __read_mostly;
module_param(bytes_lru_sorted_cold_regions, ulong, 0400);
static unsigned long nr_cold_quota_exceeds __read_mostly;
module_param(nr_cold_quota_exceeds, ulong, 0400);

static struct damon_ctx *ctx;
static struct damon_target *target;

struct damon_lru_sort_ram_walk_arg {
	unsigned long start;
	unsigned long end;
};

static int walk_system_ram(struct resource *res, void *arg)
{
	struct damon_lru_sort_ram_walk_arg *a = arg;

	if (a->end - a->start < res->end - res->start) {
		a->start = res->start;
		a->end = res->end;
	}
	return 0;
}

static bool get_monitoring_region(unsigned long *start, unsigned long *end)
{
	struct damon_lru_sort_ram_walk_arg arg = {};

	walk_system_ram_res(0, ULONG_MAX, &arg, walk_system_ram);
	if (arg.end <= arg.start)
		return false;

	*start = arg.start;
	*end = arg.end;
	return true;
}

static struct damos *damon_lru_sort_new_hot_scheme(unsigned int hot_thres)
{
	struct damos_watermarks wmarks = {
		.metric = DAMOS_WMARK_FREE_MEM_RATE,
		.interval = wmarks_interval,
		.high = wmarks_high,
		.mid = wmarks_mid,
		.low = wmarks_low,
	};
	struct damos_quota quota = {
		.ms = quota_ms / 2,
		.sz = 0,
		.reset_interval = quota_reset_interval_ms,
		.weight_sz = 0,
		.weight_nr_accesses = 1,
		.weight_age = 0,
	};

	return damon_new_scheme(
			PAGE_SIZE, ULONG_MAX,
			hot_thres, UINT_MAX,
			0, UINT_MAX,
			DAMOS_LRU_PRIO,
			&quota,
			&wmarks);
}

static struct damos *damon_lru_sort_new_cold_scheme(unsigned int cold_thres)
{
	struct damos_watermarks wmarks = {
		.metric = DAMOS_WMARK_FREE_MEM_RATE,
		.interval = wmarks_interval,
		.high = wmarks_high,
		.mid = wmarks_mid,
		.low = wmarks_low,
	};
	struct damos_quota quota = {
		.ms = quota_ms / 2,
		.sz = 0,
		.reset_interval = quota_reset_interval_ms,
		.weight_sz = 0,
		.weight_nr_accesses = 0,
		.weight_age = 1,
	};

	return damon_new_scheme(
			PAGE_SIZE, ULONG_MAX,
			0, 0,
			cold_thres, UINT_MAX,
			DAMOS_LRU_DEPRIO,
			&quota,
			&wmarks);
}

static int damon_lru_sort_turn(bool on)
{
	struct damon_region *region;
	struct damos *hot_scheme, *cold_scheme;
	struct damos *scheme, *next_scheme;
	unsigned int hot_thres, cold_thres;
	int err;

	if (!on) {
		err = damon_stop(&ctx, 1);
		if (!err)
			kdamond_pid = -1;
		return err;
	}

	err = damon_set_attrs(ctx, sample_interval, aggr_interval, 0,
			min_nr_regions, max_nr_regions);
	if (err)
		return err;

	if (monitor_region_start > monitor_region_end)
		return -EINVAL;
	if (!monitor_region_start && !monitor_region_end &&
			!get_monitoring_region(&monitor_region_start,
				&monitor_region_end))
		return -EINVAL;

	/* Drop schemes from a previous run */
	damon_for_each_scheme_safe(scheme, next_scheme, ctx)
		damon_destroy_scheme(scheme);

	hot_thres = aggr_interval / sample_interval * hot_thres_access_freq /
		1000;
	hot_scheme = damon_lru_sort_new_hot_scheme(hot_thres);
	if (!hot_scheme)
		return -ENOMEM;

	cold_thres = cold_min_age / aggr_interval;
	cold_scheme = damon_lru_sort_new_cold_scheme(cold_thres);
	if (!cold_scheme) {
		damon_destroy_scheme(hot_scheme);
		return -ENOMEM;
	}

	damon_add_scheme(ctx, hot_scheme);
	damon_add_scheme(ctx, cold_scheme);

	region = damon_new_region(monitor_region_start, monitor_region_end);
	if (!region)
		return -ENOMEM;
	damon_add_region(region, target);

	err = damon_start(&ctx, 1);
	if (!err) {
		kdamond_pid = ctx->kdamond->pid;
		return 0;
	}

	damon_destroy_region(region, target);
	return err;
}

#define ENABLE_CHECK_INTERVAL_MS	1000
static struct delayed_work damon_lru_sort_timer;
static void damon_lru_sort_timer_fn(struct work_struct *work)
{
	static bool last_enabled;
	bool now_enabled;

	now_enabled = enabled;
	if (last_enabled != now_enabled) {
		if (!damon_lru_sort_turn(now_enabled))
			last_enabled = now_enabled;
		else
			enabled = last_enabled;
	}

	schedule_delayed_work(&damon_lru_sort_timer,
			msecs_to_jiffies(ENABLE_CHECK_INTERVAL_MS));
}
static DECLARE_DELAYED_WORK(damon_lru_sort_timer, damon_lru_sort_timer_fn);

static int damon_lru_sort_after_aggregation(struct damon_ctx *c)
{
	struct damos *s;

	damon_for_each_scheme(s, c) {
		if (s->action == DAMOS_LRU_PRIO) {
			nr_lru_sort_tried_hot_regions = s->stat.nr_tried;
			bytes_lru_sort_tried_hot_regions = s->stat.sz_tried;
			nr_lru_sorted_hot_regions = s->stat.nr_applied;
			bytes_lru_sorted_hot_regions = s->stat.sz_applied;
			nr_hot_quota_exceeds = s->stat.qt_exceeds;
		} else if (s->action == DAMOS_LRU_DEPRIO) {
			nr_lru_sort_tried_cold_regions = s->stat.nr_tried;
			bytes_lru_sort_tried_cold_regions = s->stat.sz_tried;
			nr_lru_sorted_cold_regions = s->stat.nr_applied;
			bytes_lru_sorted_cold_regions = s->stat.sz_applied;
			nr_cold_quota_exceeds = s->stat.qt_exceeds;
		}
	}
	return 0;
}

static int __init damon_lru_sort_init(void)
{
	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;

	damon_pa_set_primitives(ctx);
	ctx->callback.after_aggregation = damon_lru_sort_after_aggregation;

	target = damon_new_target(4243);
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	schedule_delayed_work(&damon_lru_sort_timer, 0);
	return 0;
}

module_init(damon_lru_sort_init);
