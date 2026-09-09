/* Userspace check that this 5.4 kernel accepted BPF_MAP_TYPE_RINGBUF (27).
 *
 * EPERM as root is usually memlock, missing CAP_SYS_ADMIN, or SELinux
 * still untrusted_app (typical Termux su). EINVAL means type 27 is absent.
 *
 * Termux / adb (as root):
 *   clang -O2 -o test_ringbuf test_ringbuf.c
 *   ulimit -l unlimited
 *   ./test_ringbuf
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#ifndef __NR_bpf
#define __NR_bpf 280	/* aarch64 */
#endif

#define BPF_MAP_CREATE		0
#define BPF_MAP_TYPE_HASH	1
#define BPF_MAP_TYPE_RINGBUF	27

static int bpf_map_create(uint32_t type, uint32_t key_size,
			  uint32_t value_size, uint32_t max_entries)
{
	struct {
		uint32_t map_type;
		uint32_t key_size;
		uint32_t value_size;
		uint32_t max_entries;
		uint32_t map_flags;
	} attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_type = type;
	attr.key_size = key_size;
	attr.value_size = value_size;
	attr.max_entries = max_entries;
	return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

static void dump_status(void)
{
	FILE *f;
	char buf[256];

	fprintf(stderr, "uid=%d euid=%d\n", getuid(), geteuid());

	f = fopen("/proc/self/attr/current", "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f))
			fprintf(stderr, "selinux=%s", buf);
		fclose(f);
	}

	f = fopen("/proc/self/status", "r");
	if (f) {
		while (fgets(buf, sizeof(buf), f)) {
			if (!strncmp(buf, "CapEff:", 7) ||
			    !strncmp(buf, "CapBnd:", 7) ||
			    !strncmp(buf, "CapPrm:", 7))
				fputs(buf, stderr);
		}
		fclose(f);
	}
}

int main(void)
{
	struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };
	int fd;

	if (setrlimit(RLIMIT_MEMLOCK, &rl) &&
	    getrlimit(RLIMIT_MEMLOCK, &rl) == 0)
		fprintf(stderr, "memlock rlimit=%llu (setrlimit failed: %s)\n",
			(unsigned long long)rl.rlim_cur, strerror(errno));

	fd = bpf_map_create(BPF_MAP_TYPE_HASH, 4, 4, 1);
	if (fd < 0) {
		fprintf(stderr, "HASH create failed: %s (%d) — not a ringbuf bug\n",
			strerror(errno), errno);
		dump_status();
		return 1;
	}
	close(fd);
	printf("HASH map ok\n");

	fd = bpf_map_create(BPF_MAP_TYPE_RINGBUF, 0, 0, 4096);
	if (fd < 0) {
		fprintf(stderr, "RINGBUF create failed: %s (%d)\n",
			strerror(errno), errno);
		dump_status();
		return 1;
	}

	printf("RINGBUF map ok, fd=%d\n", fd);
	close(fd);
	return 0;
}
