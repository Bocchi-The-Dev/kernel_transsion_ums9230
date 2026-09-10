/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_PRELOAD_H
#define _BPF_PRELOAD_H

#include <linux/types.h>
#include <linux/module.h>

struct bpf_preload_info {
	char link_name[16];
	int link_id;
};

struct bpf_preload_ops {
	struct {
		pid_t tgid;
	} info;
	int (*preload)(struct bpf_preload_info *);
	int (*finish)(void);
	struct module *owner;
};

extern struct bpf_preload_ops *bpf_preload_ops;
#define BPF_PRELOAD_LINKS 2

#endif
