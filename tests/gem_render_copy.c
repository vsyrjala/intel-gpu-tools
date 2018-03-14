/*
 * Copyright © 2013 Intel Corporation
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
 *    Damien Lespiau <damien.lespiau@intel.com>
 */

/*
 * This file is a basic test for the render_copy() function, a very simple
 * workload for the 3D engine.
 */

#include "igt.h"
#include <stdbool.h>
#include <unistd.h>
#include <cairo.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Basic test for the render_copy() function.");

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)

#define SRC_COLOR	0xffff00ff
#define DST_COLOR	0xfff0ff00

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	uint32_t linear[WIDTH * HEIGHT];
} data_t;
static int opt_dump_png = false;
static int check_all_pixels = false;

static void scratch_buf_write_to_png(struct igt_buf *buf, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t ret;

	drm_intel_bo_map(buf->bo, 0);
	surface = cairo_image_surface_create_for_data(buf->bo->virtual,
						      CAIRO_FORMAT_RGB24,
						      igt_buf_width(buf),
						      igt_buf_height(buf),
						      buf->stride);
	ret = cairo_surface_write_to_png(surface, filename);
	igt_assert(ret == CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(surface);
	drm_intel_bo_unmap(buf->bo);
}

static void scratch_buf_init(data_t *data, struct igt_buf *buf,
			     int width, int height, int stride, uint32_t color)
{
	drm_intel_bo *bo;
	int i;

	bo = drm_intel_bo_alloc(data->bufmgr, "", SIZE, 4096);
	for (i = 0; i < width * height; i++)
		data->linear[i] = color;
	gem_write(data->drm_fd, bo->handle, 0, data->linear,
		  sizeof(data->linear));

	buf->bo = bo;
	buf->stride = stride;
	buf->tiling = I915_TILING_NONE;
	buf->size = SIZE;
}

static void
scratch_buf_check(data_t *data, struct igt_buf *buf, int x, int y,
		  uint32_t color)
{
	uint32_t val;

	gem_read(data->drm_fd, buf->bo->handle, 0,
		 data->linear, sizeof(data->linear));
	val = data->linear[y * WIDTH + x];
	igt_assert_f(val == color,
		     "Expected 0x%08x, found 0x%08x at (%d,%d)\n",
		     color, val, x, y);
}

static void
scratch_buf_check_all(data_t *data, struct igt_buf *buf)
{
	uint32_t val;
	int i, j;

	gem_read(data->drm_fd, buf->bo->handle, 0,
		 data->linear, sizeof(data->linear));

	for (i = 0; i < WIDTH; i++) {
		for (j = 0; j < HEIGHT; j++) {
			uint32_t color = DST_COLOR;
			val = data->linear[j * WIDTH + i];
			if (j >= HEIGHT/2 && i >= WIDTH/2)
				color = SRC_COLOR;

			igt_assert_f(val == color,
				     "Expected 0x%08x, found 0x%08x at (%d,%d)\n",
				     color, val, i, j);
		}
	}
}

static int opt_handler(int opt, int opt_index, void *data)
{
	if (opt == 'd') {
		opt_dump_png = true;
	}

	if (opt == 'a') {
		check_all_pixels = true;
	}

	return 0;
}

int main(int argc, char **argv)
{
	data_t data = {0, };
	struct intel_batchbuffer *batch = NULL;
	struct igt_buf src, dst, dst2;
	igt_render_copyfunc_t render_copy = NULL;
	int opt_dump_aub = igt_aub_dump_enabled();

	igt_simple_init_parse_opts(&argc, argv, "da", NULL, NULL,
				   opt_handler, NULL);

	igt_fixture {
		data.drm_fd = drm_open_driver_render(DRIVER_INTEL);
		data.devid = intel_get_drm_devid(data.drm_fd);

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);

		render_copy = igt_get_render_copyfunc(data.devid);
		igt_require_f(render_copy,
			      "no render-copy function\n");

		batch = intel_batchbuffer_alloc(data.bufmgr, data.devid);
		igt_assert(batch);
	}

	scratch_buf_init(&data, &src, WIDTH, HEIGHT, STRIDE, SRC_COLOR);
	scratch_buf_init(&data, &dst, WIDTH, HEIGHT, STRIDE, DST_COLOR);
	scratch_buf_init(&data, &dst2, WIDTH, HEIGHT, STRIDE, DST_COLOR);

	scratch_buf_check(&data, &src, WIDTH / 2, HEIGHT / 2, SRC_COLOR);
	scratch_buf_check(&data, &dst, WIDTH / 2, HEIGHT / 2, DST_COLOR);
	scratch_buf_check(&data, &dst2, WIDTH / 2, HEIGHT / 2, DST_COLOR);

	if (opt_dump_png) {
		scratch_buf_write_to_png(&src, "source.png");
		scratch_buf_write_to_png(&dst, "destination.png");
		scratch_buf_write_to_png(&dst2, "destination2.png");
	}

	if (opt_dump_aub) {
		drm_intel_bufmgr_gem_set_aub_filename(data.bufmgr,
						      "rendercopy.aub");
		drm_intel_bufmgr_gem_set_aub_dump(data.bufmgr, true);
	}

	/* This will copy the src to the mid point of the dst buffer. Presumably
	 * the out of bounds accesses will get clipped.
	 * Resulting buffer should look like:
	 *	  _______
	 *	 |dst|dst|
	 *	 |dst|src|
	 *	  -------
	 */
	render_copy(batch, NULL,
		    &src, 0, 0, WIDTH, HEIGHT,
		    &dst, WIDTH / 2, HEIGHT / 2);
	render_copy(batch, NULL,
		    &src, 0, 0, WIDTH, HEIGHT,
		    &dst2, WIDTH / 2, HEIGHT / 2);

	if (opt_dump_png) {
		scratch_buf_write_to_png(&dst, "result.png");
		scratch_buf_write_to_png(&dst2, "result2.png");
	}

	if (opt_dump_aub) {
		drm_intel_gem_bo_aub_dump_bmp(dst.bo,
			0, 0, WIDTH, HEIGHT,
			AUB_DUMP_BMP_FORMAT_ARGB_8888,
			STRIDE, 0);
		drm_intel_gem_bo_aub_dump_bmp(dst2.bo,
			0, 0, WIDTH, HEIGHT,
			AUB_DUMP_BMP_FORMAT_ARGB_8888,
			STRIDE, 0);
		drm_intel_bufmgr_gem_set_aub_dump(data.bufmgr, false);
	} else if (check_all_pixels) {
		scratch_buf_check_all(&data, &dst);
		scratch_buf_check_all(&data, &dst2);
	} else {
		scratch_buf_check(&data, &dst, 10, 10, DST_COLOR);
		scratch_buf_check(&data, &dst, WIDTH - 10, HEIGHT - 10, SRC_COLOR);
		scratch_buf_check(&data, &dst2, 10, 10, DST_COLOR);
		scratch_buf_check(&data, &dst2, WIDTH - 10, HEIGHT - 10, SRC_COLOR);
	}

	igt_exit();
}
