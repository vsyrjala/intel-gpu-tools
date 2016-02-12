/*
 * Copyright © 2013,2014 Intel Corporation
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
#include <math.h>

typedef struct {
	int drm_fd;
	uint32_t devid;
	igt_display_t display;
	igt_output_t *output;
	enum pipe pipe;
	igt_plane_t *plane;
	igt_plane_t *primary;
	struct igt_fb fb[4];
	struct igt_fb ref_fb;
	igt_crc_t crc[4];
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
	uint64_t tiling;
	unsigned int w, h;
} data_t;

struct color {
	float r,g,b;
};

#define RED   (struct color) {   o, 0.0, 0.0, }
#define GREEN (struct color) { 0.0,   o, 0.0, }
#define BLUE  (struct color) { 0.0, 0.0,   o, }
#define WHITE (struct color) {   o,   o,   o, }

static void
fill_color(data_t *data,
	   double r, double g, double b,
	   int w, int h,
	   struct igt_fb *fb)
{
	cairo_t *cr;

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	igt_paint_color(cr, 0, 0, w, h, r, g, b);
	igt_assert(!cairo_status(cr));

	cairo_destroy(cr);
}

static void
paint_rects(data_t *data,
	    int x, int y, int w, int h,
	    int hborder, int vborder,
	    igt_rotation_t rotation,
	    struct igt_fb *fb, float o)
{
	cairo_t *cr;
	struct color color[4];

	w = (w - 2 * hborder - 2) / 2;
	h = (h - 2 * vborder - 2) / 2;

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	/*
	 * Draw the following pattern
	 *
	 * +-------+
	 * | R | G |
	 * +---+---+
	 * | B | W |
	 * +-------+
	 *
	 * leaving out the specified amount of border
	 * around the sides, and a 2 pixel gap between
	 * the colored rectangles. The patters is drawn
	 * rotated by the specified amount.
	 */
	switch (rotation) {
	case IGT_ROTATION_0:
		color[0] = RED;
		color[1] = GREEN;
		color[2] = BLUE;
		color[3] = WHITE;
		break;
	case IGT_ROTATION_90:
		color[2] = RED;
		color[0] = GREEN;
		color[3] = BLUE;
		color[1] = WHITE;
		break;
	case IGT_ROTATION_180:
		color[3] = RED;
		color[2] = GREEN;
		color[1] = BLUE;
		color[0] = WHITE;
		break;
	case IGT_ROTATION_270:
		color[1] = RED;
		color[3] = GREEN;
		color[0] = BLUE;
		color[2] = WHITE;
		break;
	}

	cairo_translate(cr, x, y);

	igt_paint_color(cr,     hborder,     vborder, w, h, color[0].r, color[0].g, color[0].b);
	igt_paint_color(cr, w+2+hborder,     vborder, w, h, color[1].r, color[1].g, color[1].b);
	igt_paint_color(cr,     hborder, h+2+vborder, w, h, color[2].r, color[2].g, color[2].b);
	igt_paint_color(cr, w+2+hborder, h+2+vborder, w, h, color[3].r, color[3].g, color[3].b);

	cairo_destroy(cr);
}

static bool prepare_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;

	igt_output_set_pipe(output, data->pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		return false;
	}

	return true;
}

static unsigned int pot(unsigned int x)
{
	int v = 1;
	while (v < x)
		v <<= 1;
	return v;
}

static int tile_size(uint32_t devid)
{
        if (intel_gen(devid) == 2)
                return 2048;
	else
		return 4096;
}

static int tile_width(uint32_t devid, uint64_t modifider)
{
	if (intel_gen(devid) == 2)
		return 128;
	else if (modifider == LOCAL_I915_FORMAT_MOD_X_TILED)
		return 512;
	else if (IS_915(devid))
		return 512;
        else
		return 128;
}

static int tile_height(uint32_t devid, uint64_t modifider)
{
	return tile_size(devid) / tile_width(devid, modifider);
}

static void create_fb(data_t *data, int w, int h, uint32_t format,
		      struct igt_fb *fb)
{
	unsigned int stride = pot(w * 4);
	unsigned int offset;
	cairo_t *cr;

	offset = (tile_height(data->devid, data->tiling) * 3 / 2) * stride +
		(stride + tile_width(data->devid, data->tiling)) / 2;
	if (0) //cursor
		offset -= 48 * 4;
	//offset = 888*4; //larges for w=3200
	//offset = 1688*4;//largest for w=2400
	offset = 440*4;//largets for w=1600
	igt_create_fb_with_bo_size_offset(data->drm_fd, w, h,
					  format, data->tiling,
					  fb, 0, stride, offset);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color(cr, 0, 0, w, h, 0.5, 0.5, 0.5);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);
}

static void test_crtc(data_t *data, enum igt_plane plane)
{
	drmModeModeInfo *mode;
	unsigned int w, h;
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	uint32_t format = DRM_FORMAT_XRGB8888;
	int hborder = 0, vborder = 0;
	int i;

	data->primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	data->plane = igt_output_get_plane(output, plane);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(data->pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	mode = igt_output_get_mode(output);

	w = mode->hdisplay;
	h = mode->vdisplay;

	igt_create_color_fb(data->drm_fd, w, h,
			    DRM_FORMAT_XRGB8888, data->tiling,
			    0, 0, 0,
			//    0.5, 0.5, 0.5,
			    &data->ref_fb);

	//w = w - w/4;
	w = w/2;
	fill_color(data, 0.5, 0.5, 0.5, w, h, &data->ref_fb);
	if (data->plane->is_cursor) {
		format = DRM_FORMAT_ARGB8888;
		w = h = 64;
	}

	paint_rects(data, 0, 0, w, h, 2, 2,
		    IGT_ROTATION_0, &data->ref_fb, 1.0);

	igt_plane_set_fb(data->primary, &data->ref_fb);
	igt_display_commit(display);

	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);

	igt_debug_wait_for_keypress("crc");

	/* clear it to a solid color */
	fill_color(data, 0.5, 0.5, 0.5, w, h, &data->ref_fb);

	igt_plane_set_fb(data->primary, NULL);
	igt_plane_set_fb(data->primary, &data->ref_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	igt_debug_wait_for_keypress("crc");

	/* add some pixel borders */
	if (!data->plane->is_cursor) {
		hborder = 8;
		vborder = 2;
	}

	w += 2 * hborder;
	h += 2 * vborder;

	create_fb(data, w, h, format, &data->fb[0]);
	paint_rects(data, 0, 0, w, h,
		    2 + hborder, 2 + vborder,
		    IGT_ROTATION_0, &data->fb[0], 1.0);

	create_fb(data, w, h, format, &data->fb[2]);
	paint_rects(data, 0, 0, w, h,
		    2 + hborder, 2 + vborder,
		    IGT_ROTATION_180, &data->fb[2], 1.0);

	if (data->tiling == LOCAL_I915_FORMAT_MOD_Y_TILED) {
		create_fb(data, h, w, format, &data->fb[1]);
		paint_rects(data, 0, 0, h, w,
			    2 + vborder, 2 + hborder,
			    IGT_ROTATION_270, &data->fb[1], 1.0);

		create_fb(data, h, w, format, &data->fb[3]);
		paint_rects(data, 0, 0, h, w,
			    2 + vborder, 2 + hborder,
			    IGT_ROTATION_90, &data->fb[3], 1.0);
	}

	data->w = w;
	data->h = h;

	for (i = 0; i < 4; i++) {
		igt_rotation_t rotation = 1 << i;

		if ((rotation == IGT_ROTATION_90 || rotation == IGT_ROTATION_270) &&
		    data->tiling != LOCAL_I915_FORMAT_MOD_Y_TILED)
			continue;

		igt_plane_set_fb(data->plane, NULL);
		igt_plane_set_rotation(data->plane, rotation);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_plane_set_fb(data->plane, &data->fb[i]);
		if (rotation == IGT_ROTATION_90 || rotation == IGT_ROTATION_270) {
			igt_fb_set_position(&data->fb[i], data->plane, vborder, hborder);
			igt_fb_set_size(&data->fb[i], data->plane,
					data->h - 2 * vborder, data->w - 2 * hborder);
		} else {
			igt_fb_set_position(&data->fb[i], data->plane, hborder, vborder);
			igt_fb_set_size(&data->fb[i], data->plane,
					data->w - 2 * hborder, data->h - 2 * vborder);
		}
		igt_plane_set_size(data->plane, data->w - 2 * hborder, data->h - 2 * vborder);

		igt_plane_set_rotation(data->plane, rotation);
		igt_display_commit2(display, COMMIT_UNIVERSAL);
		igt_pipe_crc_collect_crc(data->pipe_crc, &data->crc[i]);

		igt_debug_wait_for_keypress("crc");

		igt_assert_crc_equal(&data->ref_crc, &data->crc[i]);
	}
}

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	int i;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_plane_set_fb(data->plane, NULL);
	igt_plane_set_fb(data->primary, NULL);
	igt_plane_set_rotation(data->plane, IGT_ROTATION_0);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	for (i = 0; i < 4; i++) {
		igt_rotation_t rotation = 1 << i;

		if ((rotation == IGT_ROTATION_90 || rotation == IGT_ROTATION_270) &&
		    data->tiling != LOCAL_I915_FORMAT_MOD_Y_TILED)
			continue;

		igt_remove_fb(data->drm_fd, &data->fb[i]);
	}
	igt_remove_fb(data->drm_fd, &data->ref_fb);

	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_display_commit(display);
}

static void test_plane_rotation(data_t *data, enum igt_plane plane)
{
	igt_display_t *display = &data->display;
	int valid_tests = 0;

	for_each_connected_output(display, data->output) {
		for_each_pipe(display, data->pipe) {
			if (!prepare_crtc(data))
				continue;

			test_crtc(data, plane);

			cleanup_crtc(data);
			valid_tests++;
		}
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static data_t data;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);
		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest_f("primary-linear") {
		data.tiling = LOCAL_DRM_FORMAT_MOD_NONE;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}
	igt_subtest_f("primary-xtiled") {
		data.tiling = LOCAL_I915_FORMAT_MOD_X_TILED;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}
	igt_subtest_f("primary-ytiled") {
		data.tiling = LOCAL_I915_FORMAT_MOD_Y_TILED;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_subtest_f("cursor-linear") {
		data.tiling = LOCAL_DRM_FORMAT_MOD_NONE;
		test_plane_rotation(&data, IGT_PLANE_CURSOR);
	}

	igt_subtest_f("sprite-linear") {
		data.tiling = LOCAL_DRM_FORMAT_MOD_NONE;
		test_plane_rotation(&data, IGT_PLANE_1);
	}
	igt_subtest_f("sprite-xtiled") {
		data.tiling = LOCAL_I915_FORMAT_MOD_X_TILED;
		test_plane_rotation(&data, IGT_PLANE_1);
	}
	igt_subtest_f("sprite-ytiled") {
		data.tiling = LOCAL_I915_FORMAT_MOD_Y_TILED;
		test_plane_rotation(&data, IGT_PLANE_1);
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
