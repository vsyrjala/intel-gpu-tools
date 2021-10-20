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
#include "igt_vec.h"
#include <math.h>

#define MAX_FENCES 32
#define MAXMULTIPLANESAMOUNT 2
#define TEST_MAX_WIDTH 640
#define TEST_MAX_HEIGHT 480
#define MAX_TESTED_MODES 8
#define MULTIPLANE_REFERENCE 0
#define MULTIPLANE_ROTATED 1

struct p_struct {
	igt_plane_t *plane;
	struct igt_fb fb;
};

enum p_pointorigo {
	p_top = 1 << 0,
	p_bottom = 1 << 1,
	p_left = 1 << 2,
	p_right = 1 << 3
};

struct p_point{
	enum p_pointorigo origo;
	float_t x;
	float_t y;
};

enum rectangle_type {
	rectangle,
	square,
	portrait,
	landscape,
	num_rectangle_types /* must be last */
};

typedef struct {
	int gfx_fd;
	igt_display_t display;
	struct igt_fb fb;
	struct igt_fb fb_reference;
	struct igt_fb fb_flip;
	igt_crc_t ref_crc;
	igt_crc_t flip_crc;
	igt_pipe_crc_t *pipe_crc;
	igt_rotation_t rotation;
	int pos_x;
	int pos_y;
	uint32_t override_fmt;
	uint64_t override_modifier;
	int devid;

	struct p_struct *multiplaneoldview;
	struct p_point planepos[MAXMULTIPLANESAMOUNT];

	bool use_native_resolution;
	bool extended;

	int output_crc_in_use, max_crc_in_use;
	struct crc_rect_tag {
		int mode;
		bool valid;
		igt_crc_t ref_crc;
		igt_crc_t flip_crc;
	} crc_rect[MAX_TESTED_MODES][num_rectangle_types];

	igt_fb_t last_on_screen;
} data_t;

typedef struct {
	float r;
	float g;
	float b;
} rgb_color_t;

static void set_color(rgb_color_t *color, float r, float g, float b)
{
	color->r = r;
	color->g = g;
	color->b = b;
}

static void rotate_colors(rgb_color_t *tl, rgb_color_t *tr, rgb_color_t *br,
			  rgb_color_t *bl, igt_rotation_t rotation)
{
	rgb_color_t bl_tmp, br_tmp, tl_tmp, tr_tmp;

	if (rotation & IGT_REFLECT_X) {
		igt_swap(*tl, *tr);
		igt_swap(*bl, *br);
	}

	if (rotation & IGT_ROTATION_90) {
		bl_tmp = *bl;
		br_tmp = *br;
		tl_tmp = *tl;
		tr_tmp = *tr;
		*tl = tr_tmp;
		*bl = tl_tmp;
		*tr = br_tmp;
		*br = bl_tmp;
	} else if (rotation & IGT_ROTATION_180) {
		igt_swap(*tl, *br);
		igt_swap(*tr, *bl);
	} else if (rotation & IGT_ROTATION_270) {
		bl_tmp = *bl;
		br_tmp = *br;
		tl_tmp = *tl;
		tr_tmp = *tr;
		*tl = bl_tmp;
		*bl = br_tmp;
		*tr = tl_tmp;
		*br = tr_tmp;
	}
}

#define RGB_COLOR(color) \
	color.r, color.g, color.b

static void
paint_squares(data_t *data, igt_rotation_t rotation,
	      struct igt_fb *fb, float o)
{
	cairo_t *cr;
	unsigned int w = fb->width;
	unsigned int h = fb->height;
	rgb_color_t tl, tr, bl, br;

	igt_assert_f(!(w&1), "rotation image must be even width, now attempted %d\n", w);
	igt_assert_f(!(h&1), "rotation image must be even height, now attempted %d\n", h);

	cr = igt_get_cairo_ctx(data->gfx_fd, fb);

	set_color(&tl, o, 0.0f, 0.0f);
	set_color(&tr, 0.0f, o, 0.0f);
	set_color(&br, o, o, o);
	set_color(&bl, 0.0f, 0.0f, o);

	rotate_colors(&tl, &tr, &br, &bl, rotation);

	igt_paint_color(cr, 0, 0, w / 2, h / 2, RGB_COLOR(tl));
	igt_paint_color(cr, w / 2, 0, w / 2, h / 2, RGB_COLOR(tr));
	igt_paint_color(cr, 0, h / 2, w / 2, h / 2, RGB_COLOR(bl));
	igt_paint_color(cr, w / 2, h / 2, w / 2, h / 2, RGB_COLOR(br));

	igt_put_cairo_ctx(cr);
}

static void remove_fbs(data_t *data)
{
	igt_remove_fb(data->gfx_fd, &data->fb);
	igt_remove_fb(data->gfx_fd, &data->fb_reference);
}

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	remove_fbs(data);

	igt_display_reset(display);
}

static void prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			 igt_plane_t *plane, bool start_crc)
{
	igt_display_t *display = &data->display;

	cleanup_crtc(data);

	igt_output_set_pipe(output, pipe);
	igt_plane_set_rotation(plane, IGT_ROTATION_0);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);

	/* defer crtc cleanup + crtc active for later on amd - not valid
	 * to enable CRTC without a plane active
	 */
	if (!is_amdgpu_device(data->gfx_fd))
		igt_display_commit2(display, COMMIT_ATOMIC);
	data->pipe_crc = igt_pipe_crc_new(data->gfx_fd, pipe,
				          IGT_PIPE_CRC_SOURCE_AUTO);

	if (!is_amdgpu_device(data->gfx_fd) && start_crc)
		igt_pipe_crc_start(data->pipe_crc);
}

#define TEST_WIDTH(km) \
	 min_t((km)->hdisplay, (km)->hdisplay, TEST_MAX_WIDTH)
#define TEST_HEIGHT(km) \
	 min_t((km)->vdisplay, (km)->vdisplay, TEST_MAX_HEIGHT)

static void prepare_fbs(data_t *data, igt_output_t *output,
			igt_plane_t *plane, enum rectangle_type rect, uint32_t format)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	unsigned int w, h, ref_w, ref_h, min_w, min_h;
	uint64_t modifier = data->override_modifier ?: DRM_FORMAT_MOD_LINEAR;
	uint32_t pixel_format = data->override_fmt ?: DRM_FORMAT_XRGB8888;
	const float flip_opacity = 0.75;

	remove_fbs(data);

	igt_plane_set_rotation(plane, IGT_ROTATION_0);

	mode = igt_output_get_mode(output);
	if (plane->type != DRM_PLANE_TYPE_CURSOR) {
		if (data->use_native_resolution) {
			w = mode->hdisplay;
			h = mode->vdisplay;
		} else {
			w = TEST_WIDTH(mode);
			h = TEST_HEIGHT(mode);
		}

		min_w = 256;
		min_h = 256;
	} else {
		pixel_format = data->override_fmt ?: DRM_FORMAT_ARGB8888;

		w = h = 256;
		min_w = min_h = 64;
	}

	switch (rect) {
	case rectangle:
		break;
	case square:
		w = h = min(h, w);
		break;
	case portrait:
		w = min_w;
		break;
	case landscape:
		h = min_h;
		break;
	case num_rectangle_types:
		igt_assert(0);
	}

	ref_w = w;
	ref_h = h;

	/*
	 * For 90/270, we will use create smaller fb so that the rotated
	 * frame can fit in
	 */
	if (igt_rotation_90_or_270(data->rotation)) {
		modifier = data->override_modifier ?: I915_FORMAT_MOD_Y_TILED;

		igt_swap(w, h);
	}

	/*
	 * Just try here if requested modifier format is generally available,
	 * if one format fail it will skip entire subtest.
	 */
	igt_require(igt_display_has_format_mod(display, pixel_format, modifier));

	if (!data->crc_rect[data->output_crc_in_use][rect].valid) {
		/*
		* Create a reference software rotated flip framebuffer.
		*/
		igt_create_fb(data->gfx_fd, ref_w, ref_h,
			      pixel_format, modifier, &data->fb_flip);
		paint_squares(data, data->rotation, &data->fb_flip,
			flip_opacity);
		igt_plane_set_fb(plane, &data->fb_flip);
		if (plane->type != DRM_PLANE_TYPE_CURSOR)
			igt_plane_set_position(plane, data->pos_x, data->pos_y);
		igt_display_commit2(display, COMMIT_ATOMIC);

		if (is_i915_device(data->gfx_fd)) {
			igt_pipe_crc_get_current(
				display->drm_fd, data->pipe_crc,
				&data->crc_rect[data->output_crc_in_use][rect].flip_crc);
			igt_remove_fb(data->gfx_fd, &data->fb_flip);
		} else if (is_amdgpu_device(data->gfx_fd)) {
			igt_pipe_crc_collect_crc(
				data->pipe_crc,
				&data->crc_rect[data->output_crc_in_use][rect].flip_crc);
		}

		/*
		* Create a reference CRC for a software-rotated fb.
		*/
		igt_create_fb(data->gfx_fd, ref_w, ref_h, pixel_format,
			data->override_modifier ?: DRM_FORMAT_MOD_LINEAR, &data->fb_reference);
		paint_squares(data, data->rotation, &data->fb_reference, 1.0);

		igt_plane_set_fb(plane, &data->fb_reference);
		if (plane->type != DRM_PLANE_TYPE_CURSOR)
			igt_plane_set_position(plane, data->pos_x, data->pos_y);
		igt_display_commit2(display, COMMIT_ATOMIC);

		if (is_i915_device(data->gfx_fd)) {
			igt_pipe_crc_get_current(display->drm_fd, data->pipe_crc,
					&data->crc_rect[data->output_crc_in_use][rect].ref_crc);
		} else if (is_amdgpu_device(data->gfx_fd)) {
			igt_pipe_crc_collect_crc(data->pipe_crc,
					&data->crc_rect[data->output_crc_in_use][rect].ref_crc);
			igt_remove_fb(data->gfx_fd, &data->fb_flip);
		}
		data->crc_rect[data->output_crc_in_use][rect].valid = true;
	}

	data->last_on_screen = data->fb_flip;
	/*
	  * Prepare the non-rotated flip fb.
	  */
	igt_create_fb(data->gfx_fd, w, h, pixel_format, modifier,
		      &data->fb_flip);
	paint_squares(data, IGT_ROTATION_0, &data->fb_flip,
		      flip_opacity);

	/*
	 * Prepare the plane with an non-rotated fb let the hw rotate it.
	 */
	igt_create_fb(data->gfx_fd, w, h, pixel_format, modifier, &data->fb);
	paint_squares(data, IGT_ROTATION_0, &data->fb, 1.0);
	igt_plane_set_fb(plane, &data->fb);

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		igt_plane_set_position(plane, data->pos_x, data->pos_y);
}

static void test_single_case(data_t *data, enum pipe pipe,
			     igt_output_t *output, igt_plane_t *plane,
			     enum rectangle_type rect,
			     uint32_t format, bool test_bad_format)
{
	igt_display_t *display = &data->display;
	igt_crc_t crc_output;
	int ret;

	igt_debug("Testing case %i on pipe %s, format %s\n", rect, kmstest_pipe_name(pipe), igt_format_str(format));
	prepare_fbs(data, output, plane, rect, format);

	igt_plane_set_rotation(plane, data->rotation);
	if (igt_rotation_90_or_270(data->rotation))
		igt_plane_set_size(plane, data->fb.height, data->fb.width);

	ret = igt_display_try_commit2(display, COMMIT_ATOMIC);

	/*
	 * Remove this last fb after it was taken out from screen
	 * to avoid unnecessary delays.
	 */
	igt_remove_fb(data->gfx_fd, &data->last_on_screen);

	if (test_bad_format) {
		igt_pipe_crc_drain(data->pipe_crc);
		igt_assert_eq(ret, -EINVAL);
		return;
	}

	/* Verify commit was ok. */
	igt_assert_eq(ret, 0);

	/* Check CRC */
	if (is_i915_device(data->gfx_fd))
		igt_pipe_crc_get_current(display->drm_fd, data->pipe_crc, &crc_output);
	else if (is_amdgpu_device(data->gfx_fd))
		igt_pipe_crc_collect_crc(data->pipe_crc, &crc_output);
	igt_assert_crc_equal(&data->crc_rect[data->output_crc_in_use][rect].ref_crc,
			     &crc_output);

	/*
	 * If flips are requested flip to a different fb and
	 * check CRC against that one as well.
	 */
	if (data->fb_flip.fb_id) {
		igt_plane_set_fb(plane, &data->fb_flip);
		if (igt_rotation_90_or_270(data->rotation))
			igt_plane_set_size(plane, data->fb.height, data->fb.width);

		if (plane->type != DRM_PLANE_TYPE_PRIMARY) {
			igt_display_commit_atomic(display, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK, NULL);
		} else {
			ret = drmModePageFlip(data->gfx_fd,
					output->config.crtc->crtc_id,
					data->fb_flip.fb_id,
					DRM_MODE_PAGE_FLIP_EVENT,
					NULL);
			igt_assert_eq(ret, 0);
		}
		kmstest_wait_for_pageflip(data->gfx_fd);
		if (is_i915_device(data->gfx_fd))
			igt_pipe_crc_get_current(display->drm_fd, data->pipe_crc, &crc_output);
		else if (is_amdgpu_device(data->gfx_fd))
			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_output);
		igt_assert_crc_equal(&data->crc_rect[data->output_crc_in_use][rect].flip_crc,
				     &crc_output);
	}
}

static bool test_format(data_t *data,
			struct igt_vec *tested_formats,
			uint32_t format)
{
	if (!igt_fb_supported_format(format))
		return false;

	if (!is_i915_device(data->gfx_fd) ||
	    data->extended)
		return true;

	format = igt_reduce_format(format);

	/* only test each format "class" once */
	if (igt_vec_index(tested_formats, &format) >= 0)
		return false;

	igt_vec_push(tested_formats, &format);

	return true;
}

static void test_plane_rotation(data_t *data, int plane_type, bool test_bad_format)
{
	igt_display_t *display = &data->display;
	drmModeModeInfo *mode;
	igt_output_t *output;
	enum pipe pipe;
	int pipe_count = 0, connected_outputs = 0;

	if (is_amdgpu_device(data->gfx_fd))
		igt_require(plane_type != DRM_PLANE_TYPE_OVERLAY &&
					plane_type != DRM_PLANE_TYPE_CURSOR);

	if (plane_type == DRM_PLANE_TYPE_CURSOR)
		igt_require(display->has_cursor_plane);

	igt_display_require_output(display);

	for_each_connected_output(&data->display, output)
		connected_outputs++;

	for_each_pipe_with_valid_output(display, pipe, output) {
		igt_plane_t *plane;
		int i, j, c;

		mode = igt_output_get_mode(output);

		/*
		 * Find mode which is in use in connector. If this is mode
		 * which was not run on earlier we'll end up on zeroed
		 * struct crc_rect and recalculate reference crcs.
		 */
		for (data->output_crc_in_use = 0;
		     data->output_crc_in_use < data->max_crc_in_use &&
		     data->crc_rect[data->output_crc_in_use][0].mode != mode->vdisplay;
		     data->output_crc_in_use++)
			;

		/*
		 * This is if there was different mode on different connector
		 * and this mode was not run on before.
		 */
		if (data->crc_rect[data->output_crc_in_use][0].mode != mode->vdisplay) {
			data->crc_rect[data->output_crc_in_use][0].mode = mode->vdisplay;
			data->max_crc_in_use++;

			if (data->max_crc_in_use >= MAX_TESTED_MODES)
				data->max_crc_in_use = MAX_TESTED_MODES - 1;
		}

		for (c = 0; c < num_rectangle_types; c++)
			data->crc_rect[data->output_crc_in_use][c].valid = false;

		/* restricting the execution to 2 pipes to reduce execution time*/
		if (pipe_count == 2 * connected_outputs && !data->extended)
			break;
		pipe_count++;

		igt_output_set_pipe(output, pipe);

		plane = igt_output_get_plane_type(output, plane_type);
		igt_require(igt_plane_has_prop(plane, IGT_PLANE_ROTATION));
		igt_require(igt_plane_has_rotation(plane, data->rotation));
		/* CHV can't rotate and reflect simultaneously */
		igt_require(!is_i915_device(data->gfx_fd) ||
			    !IS_CHERRYVIEW(data->devid) ||
			    data->rotation != (IGT_ROTATION_180 | IGT_REFLECT_X));

		prepare_crtc(data, output, pipe, plane, true);

		for (i = 0; i < num_rectangle_types; i++) {
			/* Unsupported on i915 */
			if (plane_type == DRM_PLANE_TYPE_CURSOR &&
			    i != square)
				continue;

			/* Only support partial covering primary plane on gen9+ */
			if (is_amdgpu_device(data->gfx_fd) ||
				(plane_type == DRM_PLANE_TYPE_PRIMARY &&
			    intel_display_ver(intel_get_drm_devid(data->gfx_fd)) < 9)) {
				if (i != rectangle)
					continue;
				else
					data->use_native_resolution = true;
			} else {
				data->use_native_resolution = false;
			}

			if (!data->override_fmt) {
				struct igt_vec tested_formats;

				igt_vec_init(&tested_formats, sizeof(uint32_t));

				for (j = 0; j < plane->drm_plane->count_formats; j++) {
					uint32_t format = plane->drm_plane->formats[j];

					if (!test_format(data, &tested_formats, format))
						continue;

					test_single_case(data, pipe, output, plane, i,
							 format, test_bad_format);
				}

				igt_vec_fini(&tested_formats);
			} else {
				test_single_case(data, pipe, output, plane, i,
						 data->override_fmt, test_bad_format);
			}
		}
		if (is_i915_device(data->gfx_fd)) {
			igt_pipe_crc_stop(data->pipe_crc);
		}
	}
}

typedef struct {
	int32_t x1, y1;
	uint64_t width, height, modifier, format;
	igt_plane_t *plane;
	igt_rotation_t rotation_sw, rotation_hw;
} planeinfos;

static bool setup_multiplane(data_t *data, planeinfos *planeinfo,
			     struct igt_fb *fbleft,  struct igt_fb *fbright)
{
	uint32_t w, h;
	struct igt_fb *planes[2] = {fbleft, fbright};
	int c;

	for (c = 0; c < ARRAY_SIZE(planes); c++) {
		/*
		 * make plane and fb width and height always divisible by 4
		 * due to NV12 support and Intel hw workarounds.
		 */
		w = planeinfo[c].width & ~3;
		h = planeinfo[c].height & ~3;

		if (igt_rotation_90_or_270(planeinfo[c].rotation_sw))
			igt_swap(w, h);

		if (!igt_plane_has_format_mod(planeinfo[c].plane,
					      planeinfo[c].format,
					      planeinfo[c].modifier))
			return false;

		/*
		 * was this hw/sw rotation ran already or need to create
		 * new fb?
		 */
		if (planes[c]->fb_id == 0) {
			igt_create_fb(data->gfx_fd, w, h, planeinfo[c].format,
				      planeinfo[c].modifier, planes[c]);

			paint_squares(data, planeinfo[c].rotation_sw,
				      planes[c], 1.0f);
		}
		igt_plane_set_fb(planeinfo[c].plane, planes[c]);

		if (igt_rotation_90_or_270(planeinfo[c].rotation_hw))
			igt_plane_set_size(planeinfo[c].plane, h, w);

		igt_plane_set_position(planeinfo[c].plane, planeinfo[c].x1,
				       planeinfo[c].y1);

		igt_plane_set_rotation(planeinfo[c].plane,
				       planeinfo[c].rotation_hw);
	}
	return true;
}

static void pointlocation(data_t *data, planeinfos *p, drmModeModeInfo *mode,
			  int c)
{
	if (data->planepos[c].origo & p_right) {
		p[c].x1 = (int32_t)(data->planepos[c].x * TEST_WIDTH(mode)
				+ mode->hdisplay);
		p[c].x1 &= ~3;
		/*
		 * At this point is handled surface on right side. If display
		 * mode is not divisible by 4 but with 2 point location is
		 * fixed to match requirements. Because of YUV planes here is
		 * intentionally ignored bit 1.
		 */
		p[c].x1 -= mode->hdisplay & 2;
	} else {
		p[c].x1 = (int32_t)(data->planepos[c].x * TEST_WIDTH(mode));
		p[c].x1 &= ~3;
	}

	if (data->planepos[c].origo & p_bottom) {
		p[c].y1 = (int32_t)(data->planepos[c].y * TEST_HEIGHT(mode)
				+ mode->vdisplay);
		p[c].y1 &= ~3;
		p[c].y1 -= mode->vdisplay & 2;
	} else {
		p[c].y1 = (int32_t)(data->planepos[c].y * TEST_HEIGHT(mode));
		p[c].y1 &= ~3;
	}
}

/*
 * count trailing zeroes
 */
#define ctz __builtin_ctz

/*
 * Here is pipe parameter which is now used only for first pipe.
 * It is left here if this test ever was wanted to be run on
 * different pipes.
 */
static void test_multi_plane_rotation(data_t *data, enum pipe pipe)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	igt_crc_t retcrc_sw, retcrc_hw;
	planeinfos p[2];
	int used_w, used_h, lastroundirotation = 0, lastroundjrotation = 0,
	    lastroundjformat = 0, c, d;
	drmModeModeInfo *mode;
	bool have_crc; // flag if can use previously logged crc for comparison
	igt_crc_t crclog[16] = {}; //4 * 4 rotation crc storage for packed formats
	char *str1, *str2; // for debug printouts

	/*
	 * These are those modes which are tested. For testing feel interesting
	 * case with modifier are 2 bpp, 4 bpp and NV12.
	 */
	static const uint32_t formatlist[] = {DRM_FORMAT_RGB565,
		DRM_FORMAT_XRGB8888, DRM_FORMAT_NV12};

	static struct {
		igt_rotation_t rotation;
		float_t width;
		float_t height;
		uint64_t modifier;
		struct igt_fb fbs[ARRAY_SIZE(formatlist)][2];
	} planeconfigs[] = {
	{IGT_ROTATION_0, .2f, .4f, DRM_FORMAT_MOD_LINEAR },
	{IGT_ROTATION_0, .2f, .4f, I915_FORMAT_MOD_X_TILED },
	{IGT_ROTATION_0, .2f, .4f, I915_FORMAT_MOD_Y_TILED },
	{IGT_ROTATION_0, .2f, .4f, I915_FORMAT_MOD_Yf_TILED },
	{IGT_ROTATION_90, .2f, .4f, I915_FORMAT_MOD_Y_TILED },
	{IGT_ROTATION_90, .2f, .4f, I915_FORMAT_MOD_Yf_TILED },
	{IGT_ROTATION_180, .2f, .4f, DRM_FORMAT_MOD_LINEAR },
	{IGT_ROTATION_180, .2f, .4f, I915_FORMAT_MOD_X_TILED },
	{IGT_ROTATION_180, .2f, .4f, I915_FORMAT_MOD_Y_TILED },
	{IGT_ROTATION_180, .2f, .4f, I915_FORMAT_MOD_Yf_TILED },
	{IGT_ROTATION_270, .2f, .4f, I915_FORMAT_MOD_Y_TILED },
	{IGT_ROTATION_270, .2f, .4f, I915_FORMAT_MOD_Yf_TILED },
	};

	for_each_valid_output_on_pipe(display, pipe, output) {
		int i, j, k, l, flipsw, fliphw;
		igt_output_set_pipe(output, pipe);
		mode = igt_output_get_mode(output);
		igt_display_require_output(display);
		igt_display_commit2(display, COMMIT_ATOMIC);

		used_w = TEST_WIDTH(mode);
		used_h = TEST_HEIGHT(mode);

		p[0].plane = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
		p[1].plane = igt_output_get_plane_type(output, DRM_PLANE_TYPE_OVERLAY);

		data->pipe_crc = igt_pipe_crc_new(data->gfx_fd, pipe,
						  IGT_PIPE_CRC_SOURCE_AUTO);
		igt_pipe_crc_start(data->pipe_crc);

		for (i = 0; i < ARRAY_SIZE(planeconfigs); i++) {
			p[0].width = (uint64_t)(planeconfigs[i].width * used_w);
			p[0].height = (uint64_t)(planeconfigs[i].height * used_h);
			p[0].modifier = planeconfigs[i].modifier;
			pointlocation(data, (planeinfos *)&p, mode, 0);

			for (k = 0; k < ARRAY_SIZE(formatlist); k++) {
				p[0].format = formatlist[k];

				for (j = 0; j < ARRAY_SIZE(planeconfigs); j++) {
					p[1].width = (uint64_t)(planeconfigs[j].width * used_w);
					p[1].height = (uint64_t)(planeconfigs[j].height * used_h);
					p[1].modifier = planeconfigs[j].modifier;
					pointlocation(data, (planeinfos *)&p,
						      mode, 1);

					for (l = 0; l < ARRAY_SIZE(formatlist); l++) {
						p[1].format = formatlist[l];
						/*
						 * RGB565 90/270 degrees rotation is supported
						 * from gen11 onwards.
						 */
						if (p[0].format == DRM_FORMAT_RGB565 &&
						     igt_rotation_90_or_270(planeconfigs[i].rotation)
						     && intel_display_ver(data->devid) < 11)
							continue;

						if (p[1].format == DRM_FORMAT_RGB565 &&
						     igt_rotation_90_or_270(planeconfigs[j].rotation)
						     && intel_display_ver(data->devid) < 11)
							continue;

						if (!igt_plane_has_rotation(p[0].plane,
									    planeconfigs[i].rotation))
							continue;

						if (!igt_plane_has_rotation(p[1].plane,
									    planeconfigs[j].rotation))
							continue;

						/*
						 * if using packed formats crc's will be
						 * same and can store them so there's
						 * no need to redo comparison image and
						 * just use stored crc.
						 */
						if (p[0].format != DRM_FORMAT_NV12 &&
						    p[1].format != DRM_FORMAT_NV12 &&
						    crclog[ctz(planeconfigs[i].rotation) | (ctz(planeconfigs[j].rotation) << 2)].frame != 0) {
							retcrc_sw = crclog[ctz(planeconfigs[i].rotation) | (ctz(planeconfigs[j].rotation) << 2)];
							have_crc = true;
						} else if (p[0].format == DRM_FORMAT_NV12 &&
							   p[1].format != DRM_FORMAT_NV12 &&
							   lastroundjformat != DRM_FORMAT_NV12 &&
							   planeconfigs[i].rotation == lastroundirotation &&
							   planeconfigs[j].rotation == lastroundjrotation) {
							/*
							 * With NV12 can benefit from
							 * previous crc if rotations
							 * stay same. If both planes
							 * have NV12 in use we need to
							 * skip that case.
							 * If last round right plane
							 * had NV12 need to skip this.
							 */
							have_crc = true;
						} else {
							/*
							 * here will be created
							 * comparison image and get crc
							 * if didn't have stored crc
							 * or planar format is in use.
							 * have_crc flag will control
							 * crc comparison part.
							 */
							p[0].rotation_sw = planeconfigs[i].rotation;
							p[0].rotation_hw = IGT_ROTATION_0;
							p[1].rotation_sw = planeconfigs[j].rotation;
							p[1].rotation_hw = IGT_ROTATION_0;
							if (!setup_multiplane(data,
									(planeinfos *)&p,
									&planeconfigs[i].fbs[k][MULTIPLANE_REFERENCE],
									&planeconfigs[j].fbs[l][MULTIPLANE_REFERENCE]))
								continue;
							igt_display_commit_atomic(display, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
							flipsw = kmstest_get_vblank(data->gfx_fd, pipe, 0) + 1;
							have_crc = false;
						}

						/*
						 * create hw rotated image and
						 * get vblank where interesting
						 * crc will be at, grab crc bit later
						 */
						p[0].rotation_sw = IGT_ROTATION_0;
						p[0].rotation_hw = planeconfigs[i].rotation;
						p[1].rotation_sw = IGT_ROTATION_0;
						p[1].rotation_hw = planeconfigs[j].rotation;

						if (!setup_multiplane(data,
								      (planeinfos *)&p,
								      &planeconfigs[i].fbs[k][MULTIPLANE_ROTATED],
								      &planeconfigs[j].fbs[l][MULTIPLANE_ROTATED]))
							continue;

						igt_display_commit_atomic(display, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
						fliphw = kmstest_get_vblank(data->gfx_fd, pipe, 0) + 1;

						if (!have_crc) {
							igt_pipe_crc_get_for_frame(data->gfx_fd,
										   data->pipe_crc,
										   flipsw,
										   &retcrc_sw);

							if (p[0].format != DRM_FORMAT_NV12 && p[1].format != DRM_FORMAT_NV12)
								crclog[ctz(planeconfigs[i].rotation) | (ctz(planeconfigs[j].rotation) << 2)]
								= retcrc_sw;
						}
						igt_pipe_crc_get_for_frame(data->gfx_fd, data->pipe_crc, fliphw, &retcrc_hw);

						str1 = igt_crc_to_string(&retcrc_sw);
						str2 = igt_crc_to_string(&retcrc_hw);

						igt_debug("crc %.8s vs %.8s -- %.4s - %.4s crc buffered:%s rot1 %d rot2 %d\n",
							str1, str2,
							(char *) &p[0].format, (char *) &p[1].format,
							have_crc?"yes":" no",
							(int[]) {0, 90, 180, 270} [ctz(planeconfigs[i].rotation)],
							(int[]) {0, 90, 180, 270} [ctz(planeconfigs[j].rotation)]);

						free(str1);
						free(str2);


						igt_assert_crc_equal(&retcrc_sw, &retcrc_hw);

						lastroundjformat = p[1].format;
						lastroundirotation = planeconfigs[i].rotation;
						lastroundjrotation = planeconfigs[j].rotation;
					}
				}
			}
		}
		igt_pipe_crc_stop(data->pipe_crc);
		igt_pipe_crc_free(data->pipe_crc);

		igt_plane_set_fb(p[0].plane, NULL);
		igt_plane_set_fb(p[1].plane, NULL);
		igt_display_commit_atomic(display, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

		for (i = 0; i < ARRAY_SIZE(crclog); i++)
			crclog[i].frame = 0;

		lastroundjformat = 0;
		lastroundirotation = 0;
		lastroundjrotation = 0;


		igt_output_set_pipe(output, PIPE_NONE);
	}
	data->pipe_crc = NULL;

	for (c = 0; c < ARRAY_SIZE(planeconfigs); c++) {
		for  (d = 0; d < ARRAY_SIZE(formatlist); d++) {
			igt_remove_fb(data->gfx_fd, &planeconfigs[c].fbs[d][MULTIPLANE_REFERENCE]);
			igt_remove_fb(data->gfx_fd, &planeconfigs[c].fbs[d][MULTIPLANE_ROTATED]);
		}
	}
}

static void test_plane_rotation_exhaust_fences(data_t *data,
					       enum pipe pipe,
					       igt_output_t *output,
					       igt_plane_t *plane)
{
	igt_display_t *display = &data->display;
	uint64_t modifier = I915_FORMAT_MOD_Y_TILED;
	uint32_t format = DRM_FORMAT_XRGB8888;
	int fd = data->gfx_fd;
	drmModeModeInfo *mode;
	struct igt_fb fb[MAX_FENCES+1] = {};
	uint64_t size;
	unsigned int stride, w, h;
	uint64_t total_aperture_size, total_fbs_size;
	int i;

	igt_require(igt_plane_has_prop(plane, IGT_PLANE_ROTATION));
	igt_require(igt_plane_has_rotation(plane, IGT_ROTATION_0 | IGT_ROTATION_90));
	igt_require(gem_available_fences(display->drm_fd) > 0);

	prepare_crtc(data, output, pipe, plane, false);

	mode = igt_output_get_mode(output);
	w = mode->hdisplay;
	h = mode->vdisplay;

	igt_calc_fb_size(fd, w, h, format, modifier, &size, &stride);

	/*
	 * Make sure there is atleast 90% of the available GTT space left
	 * for creating (MAX_FENCES+1) framebuffers.
	 */
	total_fbs_size = size * (MAX_FENCES + 1);
	total_aperture_size = gem_available_aperture_size(fd);
	igt_require(total_fbs_size < total_aperture_size * 0.9);

	for (i = 0; i < MAX_FENCES + 1; i++) {
		igt_create_fb(fd, w, h, format, modifier, &fb[i]);

		igt_plane_set_fb(plane, &fb[i]);
		igt_plane_set_rotation(plane, IGT_ROTATION_0);
		igt_display_commit2(display, COMMIT_ATOMIC);

		igt_plane_set_rotation(plane, IGT_ROTATION_90);
		igt_plane_set_size(plane, h, w);
		igt_display_commit2(display, COMMIT_ATOMIC);
	}

	for (i = 0; i < MAX_FENCES + 1; i++)
		igt_remove_fb(fd, &fb[i]);
}

static const char *plane_test_str(unsigned plane)
{
	switch (plane) {
	case DRM_PLANE_TYPE_PRIMARY:
		return "primary";
	case DRM_PLANE_TYPE_OVERLAY:
		return "sprite";
	case DRM_PLANE_TYPE_CURSOR:
		return "cursor";
	default:
		igt_assert(0);
	}
}

static const char *rot_test_str(igt_rotation_t rot)
{
	switch (rot) {
	case IGT_ROTATION_0:
		return "0";
	case IGT_ROTATION_90:
		return "90";
	case IGT_ROTATION_180:
		return "180";
	case IGT_ROTATION_270:
		return "270";
	default:
		igt_assert(0);
	}
}

static const char *modifier_test_str(uint64_t modifier)
{
	switch (modifier) {
	case I915_FORMAT_MOD_X_TILED:
		return "x-tiled";
	case I915_FORMAT_MOD_Y_TILED:
		return "y-tiled";
	case I915_FORMAT_MOD_Yf_TILED:
		return "yf-tiled";
	default:
		igt_assert(0);
	}
}

static int opt_handler(int opt, int opt_index, void *_data)
{
	data_t *data = _data;

	switch (opt) {
	case 'e':
		data->extended = true;
		break;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

static const struct option long_opts[] = {
	{ .name = "extended", .has_arg = false, .val = 'e', },
	{}
};

static const char help_str[] =
	"  --extended\t\tRun the extended tests\n";

static data_t data;

igt_main_args("", long_opts, help_str, opt_handler, &data)
{
	struct rot_subtest {
		unsigned plane;
		igt_rotation_t rot;
	} *subtest, subtests[] = {
		{ DRM_PLANE_TYPE_PRIMARY, IGT_ROTATION_90 },
		{ DRM_PLANE_TYPE_PRIMARY, IGT_ROTATION_180 },
		{ DRM_PLANE_TYPE_PRIMARY, IGT_ROTATION_270 },
		{ DRM_PLANE_TYPE_OVERLAY, IGT_ROTATION_90 },
		{ DRM_PLANE_TYPE_OVERLAY, IGT_ROTATION_180 },
		{ DRM_PLANE_TYPE_OVERLAY, IGT_ROTATION_270 },
		{ DRM_PLANE_TYPE_CURSOR, IGT_ROTATION_180 },
		{ 0, 0}
	};

	struct reflect_x {
		uint64_t modifier;
		igt_rotation_t rot;
	} *reflect_x, reflect_x_subtests[] = {
		{ I915_FORMAT_MOD_X_TILED, IGT_ROTATION_0 },
		{ I915_FORMAT_MOD_X_TILED, IGT_ROTATION_180 },
		{ I915_FORMAT_MOD_Y_TILED, IGT_ROTATION_0 },
		{ I915_FORMAT_MOD_Y_TILED, IGT_ROTATION_90 },
		{ I915_FORMAT_MOD_Y_TILED, IGT_ROTATION_180 },
		{ I915_FORMAT_MOD_Y_TILED, IGT_ROTATION_270 },
		{ I915_FORMAT_MOD_Yf_TILED, IGT_ROTATION_0 },
		{ I915_FORMAT_MOD_Yf_TILED, IGT_ROTATION_90 },
		{ I915_FORMAT_MOD_Yf_TILED, IGT_ROTATION_180 },
		{ I915_FORMAT_MOD_Yf_TILED, IGT_ROTATION_270 },
		{ 0, 0 }
	};

	int gen = 0;

	igt_fixture {
		data.gfx_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_AMDGPU);
		if (is_i915_device(data.gfx_fd)) {
			data.devid = intel_get_drm_devid(data.gfx_fd);
			gen = intel_display_ver(data.devid);
		}

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc(data.gfx_fd);

		igt_display_require(&data.display, data.gfx_fd);
		igt_require(data.display.is_atomic);
	}

	igt_describe("Rotation test with 90/270 degree for primary and sprite planes of gen9+");
	for (subtest = subtests; subtest->rot; subtest++) {
		igt_subtest_f("%s-rotation-%s",
			      plane_test_str(subtest->plane),
			      rot_test_str(subtest->rot)) {
			if (is_amdgpu_device(data.gfx_fd)) {
				data.override_fmt = DRM_FORMAT_XRGB8888;
				if (igt_rotation_90_or_270(subtest->rot))
					data.override_modifier = AMD_FMT_MOD |
						AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S) |
						AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);
				else
					data.override_modifier = DRM_FORMAT_MOD_LINEAR;
			}
			data.rotation = subtest->rot;
			test_plane_rotation(&data, subtest->plane, false);
		}
	}

	igt_describe("Rotation test with 90 degree for a plane of gen9+ with given position");
	igt_subtest_f("sprite-rotation-90-pos-100-0") {
		data.rotation = IGT_ROTATION_90;
		data.pos_x = 100,
		data.pos_y = 0;
		test_plane_rotation(&data, DRM_PLANE_TYPE_OVERLAY, false);
	}
	data.pos_x = 0,
	data.pos_y = 0;

	igt_describe("Checking unsupported pixel format for gen9+ with 90 degree of rotation");
	igt_subtest_f("bad-pixel-format") {
		 /* gen11 enables RGB565 rotation for 90/270 degrees.
		  * so apart from this, any other gen11+ pixel format
		  * can be used which doesn't support 90/270 degree
		  * rotation */
		data.rotation = IGT_ROTATION_90;
		data.override_fmt = gen < 11 ? DRM_FORMAT_RGB565 : DRM_FORMAT_Y212;
		test_plane_rotation(&data, DRM_PLANE_TYPE_PRIMARY, true);
	}
	data.override_fmt = 0;

	igt_describe("Checking unsupported tiling for gen9+ with 90 degree of rotation");
	igt_subtest_f("bad-tiling") {
		data.rotation = IGT_ROTATION_90;
		data.override_modifier = I915_FORMAT_MOD_X_TILED;
		test_plane_rotation(&data, DRM_PLANE_TYPE_PRIMARY, true);
	}
	data.override_modifier = 0;

	igt_describe("Tiling and Rotation test for gen 10+ for primary plane");
	for (reflect_x = reflect_x_subtests; reflect_x->modifier; reflect_x++) {
		igt_subtest_f("primary-%s-reflect-x-%s",
			      modifier_test_str(reflect_x->modifier),
			      rot_test_str(reflect_x->rot)) {
			data.rotation = (IGT_REFLECT_X | reflect_x->rot);
			data.override_modifier = reflect_x->modifier;
			test_plane_rotation(&data, DRM_PLANE_TYPE_PRIMARY, false);
		}
	}

	igt_describe("Rotation test on both planes by making them fully visible");
	igt_subtest_f("multiplane-rotation") {
		igt_require(gen >= 9);
		cleanup_crtc(&data);
		data.planepos[0].origo = p_top | p_left;
		data.planepos[0].x = .2f;
		data.planepos[0].y = .1f;
		data.planepos[1].origo = p_top | p_right;
		data.planepos[1].x = -.4f;
		data.planepos[1].y = .1f;
		test_multi_plane_rotation(&data, 0);
	}

	igt_describe("Rotation test on both planes by cropping left/top corner of primary plane and"
			"right/top corner of sprite plane");
	igt_subtest_f("multiplane-rotation-cropping-top") {
		igt_require(gen >= 9);
		cleanup_crtc(&data);
		data.planepos[0].origo = p_top | p_left;
		data.planepos[0].x = -.05f;
		data.planepos[0].y = -.15f;
		data.planepos[1].origo = p_top | p_right;
		data.planepos[1].x = -.15f;
		data.planepos[1].y = -.15f;
		test_multi_plane_rotation(&data, 0);
	}

	igt_describe("Rotation test on both planes by cropping left/bottom corner of primary plane"
			"and right/bottom corner of sprite plane");
	igt_subtest_f("multiplane-rotation-cropping-bottom") {
		igt_require(gen >= 9);
		cleanup_crtc(&data);
		data.planepos[0].origo = p_bottom | p_left;
		data.planepos[0].x = -.05f;
		data.planepos[0].y = -.20f;
		data.planepos[1].origo = p_bottom | p_right;
		data.planepos[1].x = -.15f;
		data.planepos[1].y = -.20f;
		test_multi_plane_rotation(&data, 0);
	}

	/*
	 * exhaust-fences should be last test, if it fails we may OOM in
	 * the following subtests otherwise.
	 */
	igt_describe("This test intends to check for fence leaks exhaustively");
	igt_subtest_f("exhaust-fences") {
		enum pipe pipe;
		igt_output_t *output;

		igt_display_require_output(&data.display);

		for_each_pipe_with_valid_output(&data.display, pipe, output) {
			igt_plane_t *primary = &data.display.pipes[pipe].planes[0];

			test_plane_rotation_exhaust_fences(&data, pipe, output, primary);
			break;
		}
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
