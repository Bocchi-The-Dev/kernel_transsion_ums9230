// SPDX-License-Identifier: GPL-2.0
/* Stub: 5.4 struct net has no netns_bpf; SK_LOOKUP/netns links are unused by A17 netd.o. */

#include <linux/bpf.h>
#include <linux/bpf-netns.h>
#include <linux/errno.h>
#include <linux/mutex.h>

DEFINE_MUTEX(netns_bpf_mutex);

int netns_bpf_prog_query(const union bpf_attr *attr,
			 union bpf_attr __user *uattr)
{
	return -EOPNOTSUPP;
}

int netns_bpf_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}

int netns_bpf_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype)
{
	return -EOPNOTSUPP;
}

int netns_bpf_link_create(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
