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
 *
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "drm_fourcc.h"

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_output_t *output;
	enum pipe pipe;
	uint32_t devid;
	int xoff, yoff;
	int x, y;
	double angle, radius;
} data_t;

static void render_fb(data_t *data, struct igt_fb *fb)
{
	cairo_t *cr;

	igt_assert(fb->fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color(cr, 0, 0, fb->width, fb->height, 1, 1, 0);
	igt_paint_test_pattern(cr, fb->width, fb->height);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);
}

static uint8_t *ccs_ptr(uint8_t *ptr,
			unsigned int x, unsigned int y,
			unsigned int stride)
{
	return ptr +
		((y & ~0x3f) * stride) +
		((x & ~0x7) * 64) +
		((y & 0x3f) * 8) +
		(x & 7);
}

static void render_ccs(data_t *data, uint32_t gem_handle,
		       uint32_t offset, uint32_t size,
		       int w, int h, unsigned int stride)
{
	uint8_t *ptr;
	int x;

	ptr = gem_mmap__cpu(data->drm_fd, gem_handle,
			    offset, size,
			    PROT_READ | PROT_WRITE);

	for (x = 0 ; x < w; x++) {
		int y = x * h / w;
		*ccs_ptr(ptr, x, y, stride) = 0x3c;
		*ccs_ptr(ptr, x, h - y, stride) = 0xc3;
	}

	munmap(ptr, size);
}

static void create_fb(data_t *data,
		      drmModeModeInfo *mode,
		      struct igt_fb *fb)
{
	struct local_drm_mode_fb_cmd2 f = {};
	unsigned int width, height;
	unsigned int size[2];

	f.flags = LOCAL_DRM_MODE_FB_MODIFIERS;
	f.width = ALIGN(mode->hdisplay*2, 16);
	f.height = ALIGN(mode->vdisplay*2, 8);
	f.pixel_format = DRM_FORMAT_XRGB8888;

	width = f.width;
	height = f.height;
	f.pitches[0] = ALIGN(width * 4, 128);
	f.modifier[0] = LOCAL_I915_FORMAT_MOD_Y_TILED_CCS;
	f.offsets[0] = 0;
	size[0] = f.pitches[0] * ALIGN(height, 32);

	width = ALIGN(f.width, 16) / 16;
	height = ALIGN(f.height, 8) / 8;
	f.pitches[1] = ALIGN(width * 1, 64);
	f.modifier[1] = LOCAL_I915_FORMAT_MOD_Y_TILED_CCS;
	f.offsets[1] = size[0];
	size[1] = f.pitches[1] * ALIGN(height, 64);

	f.handles[0] = f.handles[1] =
		gem_create(data->drm_fd, size[0] + size[1]);

	igt_assert(drmIoctl(data->drm_fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == 0);

	fb->fb_id = f.fb_id;
	fb->fd = data->drm_fd;
	fb->gem_handle = f.handles[0];
	fb->is_dumb = false;
	fb->drm_format = f.pixel_format;
	fb->width = f.width;
	fb->height = f.height;
	fb->stride = f.pitches[0];
	fb->tiling = f.modifier[0];
	fb->size = size[0];
	fb->cairo_surface = NULL;
	fb->domain = 0;

	render_fb(data, fb);

	render_ccs(data, f.handles[0], f.offsets[1], size[1],
		   f.width/16, f.height/8, f.pitches[1]);
}

static bool prepare_crtc(data_t *data)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;

	/* select the pipe we want to use */
	igt_output_set_pipe(data->output, data->pipe);
	igt_display_commit(display);

	if (!data->output->valid) {
		igt_output_set_pipe(data->output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	mode = igt_output_get_mode(data->output);

	create_fb(data, mode, &data->fb);

	data->xoff = (data->fb.width  - mode->hdisplay) / 2;
	data->yoff = (data->fb.height  - mode->vdisplay) / 2;
	data->radius = min(mode->hdisplay, mode->vdisplay) / 2;
	data->angle = 0.0;

	return true;
}

static void pan_around(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	data->angle += M_PI / 500.0;
	data->x = data->xoff + sin(data->angle) * data->radius;
	data->y = data->yoff + cos(data->angle) * data->radius;

	primary = igt_output_get_plane(data->output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, &data->fb);
	igt_fb_set_position(&data->fb, primary, data->x, data->y);
	igt_display_commit(display);

	igt_debug_wait_for_keypress("ccs");
}

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	primary = igt_output_get_plane(data->output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_display_commit(display);

	igt_remove_fb(data->drm_fd, &data->fb);
}

static void test(data_t *data)
{
	igt_display_t *display = &data->display;
	int valid_tests = 0;
	int i;

	for_each_connected_output(display, data->output) {
		if (!prepare_crtc(data))
			continue;

		valid_tests++;

		igt_info("Beginning %s on pipe %s, connector %s\n",
			 igt_subtest_name(),
			 kmstest_pipe_name(data->pipe),
			 igt_output_name(data->output));

		for (i = 0; i < 1000; i++)
			pan_around(data);

		igt_info("\n%s on pipe %s, connector %s: PASSED\n\n",
			 igt_subtest_name(),
			 kmstest_pipe_name(data->pipe),
			 igt_output_name(data->output));

		cleanup_crtc(data);
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static data_t data;

igt_main
{
	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);
		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_display_init(&data.display, data.drm_fd);
	}

	for (data.pipe = PIPE_A; data.pipe <= PIPE_C; data.pipe++) {
		igt_subtest_f("pipe-%s", kmstest_pipe_name(data.pipe))
			test(&data);
	}

	igt_fixture
		igt_display_fini(&data.display);
}
