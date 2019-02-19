/*
 * Copyright © 2014 Intel Corporation
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
 *   Damien Lespiau <damien.lespiau@intel.com>
 */

#include "igt.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	int drm_fd;
	igt_display_t *display;
	igt_output_t *output;
	igt_plane_t *plane;
	struct igt_fb fb;
	enum pipe pipe;
} data_t;

drmModeModeInfo mode_640_480 = {
	.name		= "640x480",
	.vrefresh	= 60,
	.clock		= 25200,

	.hdisplay	= 640,
	.hsync_start	= 656,
	.hsync_end	= 752,
	.htotal		= 800,

	.vdisplay	= 480,
	.vsync_start	= 490,
	.vsync_end	= 492,
	.vtotal		= 525,

	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static void set_atomic_lut(data_t *data, int lut_size,
			   const struct drm_color_lut *lut)
{
	igt_pipe_t *pipe_obj = &data->display->pipes[data->pipe];

	igt_pipe_obj_replace_prop_blob(pipe_obj, IGT_CRTC_GAMMA_LUT, lut, lut_size * sizeof(lut[0]));
}

static void test_lut(data_t *data, data_t *data2)
{
	igt_pipe_t *pipe_obj = &data->display->pipes[data->pipe];
	struct drm_color_lut *one, *zero;
	drmModeCrtc *crtc;
	int i, lut_size;

	crtc = drmModeGetCrtc(data->drm_fd, pipe_obj->crtc_id);
	lut_size = crtc->gamma_size;
	drmModeFreeCrtc(crtc);

	one = calloc(sizeof(one[0]), lut_size);
	memset(one, 0x40, sizeof(one[0]) * lut_size);

	zero = calloc(sizeof(zero[0]), lut_size);

	for (i = 0; i < 10*60; i++) {
	  set_atomic_lut(data, lut_size, one);
	  set_atomic_lut(data2, lut_size, one);
	  igt_display_commit2(data->display, data->display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
	  usleep(8*1000);
	  set_atomic_lut(data, lut_size, zero);
	  set_atomic_lut(data2, lut_size, zero);
	  igt_display_commit2(data->display, data->display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
	}

	free(one);
	free(zero);
}

static void prep_output(data_t *data)
{
	drmModeModeInfo *mode;

	//igt_output_override_mode(data->output, &mode_640_480);

	mode = igt_output_get_mode(data->output);

	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			    1.0, 1.0, 1.0,
			    &data->fb);

	igt_output_set_pipe(data->output, data->pipe);
	data->plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(data->plane, &data->fb);
	igt_display_commit2(data->display, data->display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
}

static void clean_pipe(data_t *data)
{
	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_plane_set_fb(data->plane, NULL);
	igt_display_commit2(data->display, data->display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	igt_remove_fb(data->drm_fd, &data->fb);
}

static void
prep_pipe(data_t *data, igt_output_t *exclude)
{
	igt_skip_on(data->pipe >= data->display->n_pipes);
	igt_require(data->display->pipes[data->pipe].n_planes > 0);
	igt_display_require_output_on_pipe(data->display, data->pipe);

	for_each_valid_output_on_pipe(data->display, data->pipe, data->output) {
	  if (data->output == exclude)
	    continue;
	  prep_output(data);
	  break;
	}
}

static igt_display_t display;
static data_t data;
static data_t data2;

igt_simple_main
{
	igt_skip_on_simulation();

	data.display = &display;
	data.drm_fd = drm_open_driver_master(DRIVER_INTEL);

	kmstest_set_vt_graphics_mode();

	igt_display_require(data.display, data.drm_fd);

	for_each_pipe_static(data.pipe) {
	  data2 = data;
	  data2.pipe = data.pipe + 1;

	  prep_pipe(&data, NULL);
	  prep_pipe(&data2, data.output);

	  test_lut(&data, &data2);

	  clean_pipe(&data2);
	  clean_pipe(&data);
	}

	igt_display_fini(data.display);
}
