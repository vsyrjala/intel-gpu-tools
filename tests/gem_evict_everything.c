/*
 * Copyright © 2011,2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: run a couple of big batches to force the eviction code.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define HEIGHT 256
#define WIDTH 1024

static void
copy(int fd, uint32_t dst, uint32_t src, uint32_t *all_bo, int n_bo, int error)
{
	uint32_t batch[10];
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 *obj;
	struct drm_i915_gem_execbuffer2 exec;
	uint32_t handle;
	int n, ret;

	batch[0] = (XY_SRC_COPY_BLT_CMD |
		    XY_SRC_COPY_BLT_WRITE_ALPHA |
		    XY_SRC_COPY_BLT_WRITE_RGB);
	batch[1] = (3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  WIDTH*4;
	batch[2] = 0; /* dst x1,y1 */
	batch[3] = (HEIGHT << 16) | WIDTH; /* dst x2,y2 */
	batch[4] = 0; /* dst reloc */
	batch[5] = 0; /* src x1,y1 */
	batch[6] = WIDTH*4;
	batch[7] = 0; /* src reloc */
	batch[8] = MI_BATCH_BUFFER_END;
	batch[9] = MI_NOOP;

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	reloc[0].target_handle = dst;
	reloc[0].delta = 0;
	reloc[0].offset = 4 * sizeof(batch[0]);
	reloc[0].presumed_offset = 0;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;

	reloc[1].target_handle = src;
	reloc[1].delta = 0;
	reloc[1].offset = 7 * sizeof(batch[0]);
	reloc[1].presumed_offset = 0;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[1].write_domain = 0;

	obj = calloc(n_bo + 1, sizeof(*obj));
	for (n = 0; n < n_bo; n++)
		obj[n].handle = all_bo[n];
	obj[n].handle = handle;
	obj[n].relocation_count = 2;
	obj[n].relocs_ptr = (uintptr_t)reloc;

	exec.buffers_ptr = (uintptr_t)obj;
	exec.buffer_count = n_bo + 1;
	exec.batch_start_offset = 0;
	exec.batch_len = sizeof(batch);
	exec.DR1 = exec.DR4 = 0;
	exec.num_cliprects = 0;
	exec.cliprects_ptr = 0;
	exec.flags = HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0;
	i915_execbuffer2_set_context_id(exec, 0);
	exec.rsvd2 = 0;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
	if (ret)
		ret = errno;
	igt_assert(ret == error);

	gem_close(fd, handle);
	free(obj);
}

static void exchange_uint32_t(void *array, unsigned i, unsigned j)
{
	uint32_t *i_arr = array;
	uint32_t i_tmp;

	i_tmp = i_arr[i];
	i_arr[i] = i_arr[j];
	i_arr[j] = i_tmp;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

#define INTERRUPTIBLE	(1 << 0)
#define SWAPPING	(1 << 1)
#define DUP_DRMFD	(1 << 2)
#define MEMORY_PRESSURE	(1 << 3)
#define ALL_FLAGS	(INTERRUPTIBLE | SWAPPING | DUP_DRMFD | MEMORY_PRESSURE)

static void forked_evictions(int fd, int size, int count,
			     unsigned flags)
{
	uint32_t *bo;
	int n, pass, l;
	int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
	int bo_count;

	igt_require((uint64_t)count * size / (1024 * 1024) < intel_get_total_ram_mb() * 9 / 10);

	if (flags & SWAPPING) {
		igt_require(intel_get_total_ram_mb() / 4 < intel_get_total_swap_mb());
		bo_count = intel_get_total_ram_mb() * 11 / 10;

		if (bo_count < count)
			bo_count = count;
	} else
		bo_count = count;

	bo = malloc(bo_count*sizeof(*bo));
	igt_assert(bo);

	for (n = 0; n < bo_count; n++)
		bo[n] = gem_create(fd, size);

	igt_fork(i, min(count, num_threads * 4)) {
		int realfd = fd;
		int num_passes = flags & SWAPPING ? 10 : 100;

		/* Every fork should have a different permutation! */
		srand(i * 63);

		if (flags & INTERRUPTIBLE)
			igt_fork_signal_helper();

		igt_permute_array(bo, bo_count, exchange_uint32_t);

		if (flags & DUP_DRMFD) {
			realfd = drm_open_any();

			/* We can overwrite the bo array since we're forked. */
			for (l = 0; l < count; l++) {
				uint32_t flink;

				flink = gem_flink(fd, bo[l]);
				bo[l] = gem_open(realfd, flink);
			}

		}

		for (pass = 0; pass < num_passes; pass++) {
			copy(realfd, bo[0], bo[1], bo, count, 0);

			for (l = 0; l < count && (flags & MEMORY_PRESSURE); l++) {
				uint32_t *base = gem_mmap__cpu(realfd, bo[l],
							       size,
							       PROT_READ | PROT_WRITE);
				memset(base, 0, size);
				munmap(base, size);
			}
		}

		if (flags & INTERRUPTIBLE)
			igt_stop_signal_helper();

		/* drmfd closing will take care of additional bo refs */
		if (flags & DUP_DRMFD)
			close(realfd);
	}

	igt_waitchildren();

	for (n = 0; n < bo_count; n++)
		gem_close(fd, bo[n]);
	free(bo);
}

static void swapping_evictions(int fd, int size, int count)
{
	uint32_t *bo;
	int i, n, pass;
	int bo_count;

	igt_require((uint64_t)count * size / (1024 * 1024) < intel_get_total_ram_mb() * 9 / 10);

	igt_require(intel_get_total_ram_mb() / 4 < intel_get_total_swap_mb());
	bo_count = intel_get_total_ram_mb() * 11 / 10;

	if (bo_count < count)
		bo_count = count;

	bo = malloc(bo_count*sizeof(*bo));
	igt_assert(bo);

	for (n = 0; n < bo_count; n++)
		bo[n] = gem_create(fd, size);

	for (i = 0; i < bo_count/32; i++) {
		igt_permute_array(bo, bo_count, exchange_uint32_t);

		for (pass = 0; pass < 100; pass++) {
			copy(fd, bo[0], bo[1], bo, count, 0);
		}
	}

	for (n = 0; n < bo_count; n++)
		gem_close(fd, bo[n]);
	free(bo);
}

static void minor_evictions(int fd, int size, int count)
{
	uint32_t *bo, *sel;
	int n, m, pass, fail;

	igt_require((uint64_t)count * size / (1024 * 1024) < intel_get_total_ram_mb() * 9 / 10);

	bo = malloc(3*count*sizeof(*bo));
	igt_assert(bo);

	for (n = 0; n < 2*count; n++)
		bo[n] = gem_create(fd, size);

	sel = bo + n;
	for (fail = 0, m = 0; fail < 10; fail++) {
		for (pass = 0; pass < 100; pass++) {
			for (n = 0; n < count; n++, m += 7)
				sel[n] = bo[m%(2*count)];
			copy(fd, sel[0], sel[1], sel, count, 0);
		}
		copy(fd, bo[0], bo[0], bo, 2*count, ENOSPC);
	}

	for (n = 0; n < 2*count; n++)
		gem_close(fd, bo[n]);
	free(bo);
}

static void major_evictions(int fd, int size, int count)
{
	int n, m, loop;
	uint32_t *bo;

	igt_require((uint64_t)count * size / (1024 * 1024) < intel_get_total_ram_mb() * 9 / 10);

	bo = malloc(count*sizeof(*bo));
	igt_assert(bo);

	for (n = 0; n < count; n++)
		bo[n] = gem_create(fd, size);

	for (loop = 0, m = 0; loop < 100; loop++, m += 17) {
		n = m % count;
		copy(fd, bo[n], bo[n], &bo[n], 1, 0);
	}

	for (n = 0; n < count; n++)
		gem_close(fd, bo[n]);
	free(bo);
}

int main(int argc, char **argv)
{
	int size, count, fd;
	size = count = 0;
	fd = -1;

	igt_subtest_init(argc, argv);

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();

		size = 1024 * 1024;
		count = 3*gem_aperture_size(fd) / size / 4;
	}

	for (unsigned flags = 0; flags < ALL_FLAGS + 1; flags++) {
		igt_subtest_f("forked%s%s%s-%s",
			      flags & SWAPPING ? "-swapping" : "",
			      flags & DUP_DRMFD ? "-multifd" : "",
			      flags & MEMORY_PRESSURE ? "-mempressure" : "",
			      flags & INTERRUPTIBLE ? "interruptible" : "normal") {
			forked_evictions(fd, size, count, flags);
		}
	}

	igt_subtest("swapping-normal")
		swapping_evictions(fd, size, count);

	igt_subtest("minor-normal")
		minor_evictions(fd, size, count);

	igt_subtest("major-normal") {
		size = 3*gem_aperture_size(fd) / 4;
		count = 4;
		major_evictions(fd, size, count);
	}

	igt_fixture {
		size = 1024 * 1024;
		count = 3*gem_aperture_size(fd) / size / 4;
	}

	igt_fork_signal_helper();

	igt_subtest("swapping-interruptible")
		swapping_evictions(fd, size, count);

	igt_subtest("minor-interruptible")
		minor_evictions(fd, size, count);

	igt_subtest("major-interruptible") {
		size = 3*gem_aperture_size(fd) / 4;
		count = 4;
		major_evictions(fd, size, count);
	}

	igt_stop_signal_helper();

	igt_fixture {
		close(fd);
	}

	igt_exit();
}
