/*
 * Copyright © 2016 Intel Corporation
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
#include "drmtest.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

drmModeModeInfo mode_60hz = {
.name = "1920x1080@60hz",
.vrefresh = 60,
.clock = 142667,
.hdisplay = 1920,
.hsync_start = 1936,
.hsync_end = 1952,
.htotal = 2104,
.vdisplay = 1080,
.vsync_start = 1083,
.vsync_end = 1097,
.vtotal = 1128,
.flags = 0xa,
};


drmModeModeInfo mode_1024x768_90hz = {
.name = "1024x768@90hz",
.vrefresh = 90,
.clock = 100190,
.hdisplay = 1024,
.hsync_start = 1088,
.hsync_end = 1200,
.htotal = 1376,
.vdisplay = 768,
.vsync_start = 769,
.vsync_end = 772,
.vtotal = 809,
.flags = 0xa,
};


struct data {
	int drm_fd;
	igt_display_t display;
	int num_planes;
	uint32_t format;
	uint64_t modifier;
};

static void
test(struct data *data, igt_output_t *output, enum pipe pipe)
{
	struct igt_fb fb, argb_fb;
	drmModeModeInfo *mode;
	igt_plane_t *plane;
	uint64_t cursor_width, cursor_height;
	int i;

	//igt_output_override_mode(output, &mode_60hz);
	//igt_output_override_mode(output, &mode_1024x768_90hz);
	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);

	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay, mode->vdisplay,
			    data->format, data->modifier,
			    1.0, 1.0, 0.0,
			    &fb);

	do_or_die(drmGetCap(data->drm_fd, DRM_CAP_CURSOR_WIDTH, &cursor_width));
	do_or_die(drmGetCap(data->drm_fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height));

	igt_create_color_fb(data->drm_fd, cursor_width, cursor_height,
			    DRM_FORMAT_ARGB8888,
			    LOCAL_DRM_FORMAT_MOD_NONE,
			    1.0,0.0,0.0,
			    &argb_fb);

	i = 0;
	for_each_plane_on_pipe(&data->display, pipe, plane) {
		if (plane->type == DRM_PLANE_TYPE_CURSOR) {
			igt_plane_set_fb(plane, &argb_fb);
			igt_fb_set_size(&argb_fb, plane, cursor_width, cursor_height);
			igt_plane_set_size(plane, cursor_width, cursor_height);
		} else {
			igt_plane_set_fb(plane, &fb);
		}
		if (++i == data->num_planes)
			break;
	}

	igt_display_commit2(&data->display, data->display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	igt_debug_wait_for_keypress("icl");

	igt_remove_fb(data->drm_fd, &fb);
	igt_remove_fb(data->drm_fd, &argb_fb);

	igt_output_set_pipe(output, PIPE_ANY);
}

static void
run_test(struct data *data)
{
	igt_output_t *output;
	enum pipe pipe;

	for_each_pipe_with_single_output(&data->display, pipe, output)
		test(data, output, pipe);
}

static int opt_handler(int opt, int opt_index, void *_data)
{
	struct data *data = _data;

	switch (opt) {
	case 'n':
		data->num_planes = atoi(optarg);
		break;
	case 'b':
		switch (atoi(optarg)) {
		case 32:
		case 24:
			data->format = DRM_FORMAT_XRGB8888;
			break;
		case 16:
			data->format = DRM_FORMAT_RGB565;
			break;
		case 8:
			data->format = DRM_FORMAT_RGB565;
			break;
		}
		break;
	case 't':
		switch (optarg[0]) {
		case 'L':
		case 'l':
			data->modifier = DRM_FORMAT_MOD_LINEAR;
			break;
		case 'X':
		case 'x':
			data->modifier = I915_FORMAT_MOD_X_TILED;
			break;
		case 'Y':
		case 'y':
			data->modifier = I915_FORMAT_MOD_Y_TILED;
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct data data = {
		.num_planes = 7,
		.format = DRM_FORMAT_XRGB8888,
		.modifier = DRM_FORMAT_MOD_LINEAR,
	};
	static const struct option long_opts[] = {
		{ .name = "num-planes", .val = 'n', .has_arg = true, },
		{ .name = "bpp", .val = 'b', .has_arg = true, },
		{ .name = "tiling", .val = 't', .has_arg = true, },
		{}
	};
	static const char *help_str =
		"  --num-planes\t\tEnable this many planes\n"
		"  --bpp\t\tUse specified bpp framebuffer\n"
		"  --tiling\t\tUse an x/y-tilied framebuffer\n";

	igt_simple_init_parse_opts(&argc, argv, "", long_opts, help_str,
				   opt_handler, &data);

	igt_skip_on_simulation();

	data.drm_fd = drm_open_driver_master(DRIVER_ANY);

	kmstest_set_vt_graphics_mode();

	igt_display_require(&data.display, data.drm_fd);
	igt_display_require_output(&data.display);

	run_test(&data);

	igt_display_fini(&data.display);

	igt_exit();
}
