/* Userspace check that this 5.4 kernel accepted BPF_MAP_TYPE_RINGBUF (27).
 *
 * Termux (as root):
 *   clang -O2 -o test_ringbuf test_ringbuf.c
 *   ./test_ringbuf
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifndef __NR_bpf
#define __NR_bpf 280	/* aarch64 */
#endif

#define BPF_MAP_CREATE		0
#define BPF_MAP_TYPE_RINGBUF	27

int main(void)
{
	struct {
		uint32_t map_type;
		uint32_t key_size;
		uint32_t value_size;
		uint32_t max_entries;
		uint32_t map_flags;
	} attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_type = BPF_MAP_TYPE_RINGBUF;
	attr.max_entries = 4096; /* power of two and page-aligned */

	int fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
	if (fd < 0) {
		fprintf(stderr, "RINGBUF create failed: %s (%d)\n",
			strerror(errno), errno);
		return 1;
	}

	printf("RINGBUF map ok, fd=%d\n", fd);
	close(fd);
	return 0;
}
