/*
 * Copyright © 2019 Intel Corporation
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

struct data {
	int drm_fd;
	igt_display_t display;
	enum pipe pipe;
	igt_output_t *output;
	igt_plane_t *plane;
};

static void test(struct data *data)
{
	struct igt_fb fb = {};
	drmModeModeInfoPtr mode;

	data->output = igt_get_single_output_for_pipe(&data->display, data->pipe);
	igt_require(data->output);

	igt_output_set_pipe(data->output, data->pipe);
	data->plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);

	mode = igt_output_get_mode(data->output);

	igt_create_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			      DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_NONE, &fb);

	igt_plane_set_fb(data->plane, &fb);

	for (;;) {
		igt_plane_set_size(data->plane,
				   mode->hdisplay * 18 / 19,
				   mode->vdisplay * 18 / 19);
		igt_display_commit2(&data->display, COMMIT_ATOMIC);

		igt_plane_set_size(data->plane,
				   mode->hdisplay * 8 / 9,
				   mode->vdisplay * 8 / 9);
		igt_display_commit2(&data->display, COMMIT_ATOMIC);
	}

	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_plane_set_fb(data->plane, NULL);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	igt_remove_fb(data->drm_fd, &fb);
}

static struct data data;

igt_simple_main
{
	drmModeRes *res;

	igt_skip_on_simulation();

	data.drm_fd = drm_open_driver_master(DRIVER_ANY);

	kmstest_set_vt_graphics_mode();

	igt_require_pipe_crc(data.drm_fd);
	igt_display_require(&data.display, data.drm_fd);
	igt_require(&data.display.is_atomic);

	res = drmModeGetResources(data.drm_fd);
	kmstest_unset_all_crtcs(data.drm_fd, res);
	drmModeFreeResources(res);

	for_each_pipe_static(data.pipe)
		test(&data);

	igt_display_fini(&data.display);
}
