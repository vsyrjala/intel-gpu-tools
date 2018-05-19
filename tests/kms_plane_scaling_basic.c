/*
 * Copyright © 2018 Intel Corporation
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
 */

#include "igt.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

IGT_TEST_DESCRIPTION("Test plane scaling correctness");

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb_8x8;
	igt_output_t *output;
	enum pipe pipe;
} data_t;

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;

	igt_display_reset(display);
}

struct color {
	float r, g, b;
};

static const struct color colors[] = {
	{ 1.0f, 0.0f, 0.0f, },
	{ 0.0f, 1.0f, 0.0f, },
	{ 0.0f, 0.0f, 1.0f, },
	{ 1.0f, 1.0f, 1.0f, },
	{ 0.0f, 0.0f, 0.0f, },
	{ 0.0f, 1.0f, 1.0f, },
	{ 1.0f, 0.0f, 1.0f, },
	{ 1.0f, 1.0f, 0.0f, },
};

static void create_fb(data_t *data, int width, int height,
		      uint32_t format, uint64_t tiling,
		      struct igt_fb *fb)
{
	cairo_t *cr;
	int i = 0;

	igt_create_fb(data->drm_fd, width, height, format, tiling, fb);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	for (int y = 0; y < fb->height; y++) {
		for (int x = 0; x < fb->width; x++) {
			const struct color *c =
				&colors[i++ % ARRAY_SIZE(colors)];

			igt_paint_color(cr, x, y, 1, 1, c->r, c->g, c->b);
		}

		i += 3;
	}

	igt_put_cairo_ctx(data->drm_fd, fb, cr);
}

static void prepare_crtc(data_t *data)
{
	igt_display_t *display = &data->display;

	igt_display_require_output_on_pipe(display, data->pipe);

	data->output = igt_get_single_output_for_pipe(display, data->pipe);

	cleanup_crtc(data);

	/* select the pipe we want to use */
	igt_output_set_pipe(data->output, data->pipe);

	igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
}

static void test_plane(data_t *data, igt_plane_t *plane,
		       int dw, int dh, struct igt_fb *fb)
{
	igt_display_t *display = &data->display;
	drmModeModeInfo *mode;

	mode = igt_output_get_mode(data->output);

	plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_OVERLAY);
	igt_plane_set_fb(plane, fb);
	igt_plane_set_size(plane, dw, dh);
	igt_plane_set_position(plane,
			       (mode->hdisplay - dw) / 2,
			       (mode->vdisplay - dh) / 2);

	igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_UNIVERSAL);

	igt_debug_wait_for_keypress("plane");

	igt_plane_set_fb(plane, NULL);
	igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_UNIVERSAL);
}

static void test_upscaling(data_t *data)
{
	igt_display_t *display = &data->display;
	drmModeModeInfo *mode;
	igt_plane_t *plane;

	prepare_crtc(data);

	mode = igt_output_get_mode(data->output);

	for_each_plane_on_pipe(display, data->pipe, plane) {
		test_plane(data, plane,
			   mode->hdisplay, mode->vdisplay,
			   &data->fb_8x8);
	}

	cleanup_crtc(data);
}

static data_t data;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);

		kmstest_set_vt_graphics_mode();

		igt_display_init(&data.display, data.drm_fd);

		create_fb(&data, 8, 8,
			  DRM_FORMAT_XRGB8888,
			  LOCAL_DRM_FORMAT_MOD_NONE,
			  &data.fb_8x8);

	}

	for_each_pipe_static(data.pipe)
		igt_subtest_f("pipe-%s-upscaling", kmstest_pipe_name(data.pipe))
			test_upscaling(&data);

	igt_fixture {
		igt_remove_fb(data.drm_fd, &data.fb_8x8);
		igt_display_fini(&data.display);
	}
}
