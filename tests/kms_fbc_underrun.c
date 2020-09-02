/*
 * Copyright © 2020 Intel Corporation
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
 */

#include "igt.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_output_t *output;
	igt_plane_t *plane;
	enum pipe pipe;
} data_t;

static void prepare_crtc(data_t *data)
{
	drmModeModeInfo *mode;

	igt_output_set_pipe(data->output, data->pipe);
	igt_display_commit(&data->display);

	mode = igt_output_get_mode(data->output);
	data->plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);

	igt_create_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888, I915_FORMAT_MOD_X_TILED, &data->fb);

	igt_plane_set_fb(data->plane, &data->fb);
	igt_display_commit(&data->display);
}

static void test(data_t *data)
{
	uint32_t handle = data->fb.gem_handle;

	for (int i = 0; i < 100000; i++) {
		for (int j = 0; j < 5; j++) {
			usleep(2000);
			gem_set_domain(data->drm_fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
			gem_sw_finish(data->drm_fd, handle);
			gem_set_domain(data->drm_fd, handle, I915_GEM_DOMAIN_GTT, 0);
		}
		igt_wait_for_vblank(data->drm_fd, data->pipe);
	}
}

static void cleanup_crtc(data_t *data)
{
	igt_output_set_pipe(data->output, PIPE_NONE);
	igt_plane_set_fb(data->plane, NULL);
	igt_display_commit(&data->display);
	igt_remove_fb(data->drm_fd, &data->fb);
}

static void run_test(data_t *data)
{
	igt_display_t *display = &data->display;

	for_each_pipe_with_valid_output(display, data->pipe, data->output) {
		prepare_crtc(data);
		test(data);
		cleanup_crtc(data);
		break;
	}

	igt_skip("no valid crtc/connector combinations found\n");
}

static data_t data;

igt_simple_main
{
	data.drm_fd = drm_open_driver_master(DRIVER_INTEL);
	kmstest_set_vt_graphics_mode();
	igt_display_require(&data.display, data.drm_fd);

	run_test(&data);

	igt_display_fini(&data.display);
}
