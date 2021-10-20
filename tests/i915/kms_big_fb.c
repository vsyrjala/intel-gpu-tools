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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "i915/gem_create.h"

IGT_TEST_DESCRIPTION("Test big framebuffers");

typedef struct {
	int drm_fd;
	uint32_t devid;
	igt_display_t display;
	enum pipe pipe;
	igt_output_t *output;
	igt_plane_t *plane;
	igt_pipe_crc_t *pipe_crc;
	struct igt_fb small_fb, big_fb, big_fb_flip[2];
	uint32_t format;
	uint64_t modifier;
	int width, height;
	igt_rotation_t rotation;
	int max_fb_width, max_fb_height;
	int big_fb_width, big_fb_height;
	uint64_t ram_size, aper_size, mappable_size;
	igt_render_copyfunc_t render_copy;
	struct buf_ops *bops;
	struct intel_bb *ibb;
	bool max_hw_stride_test;
	bool async_flip_test;
	int hw_stride;
	int max_hw_fb_width;
	double planeclearrgb[3];
	uint32_t format_override;
	uint32_t stride_override;
} data_t;

static struct intel_buf *init_buf(data_t *data,
				  const struct igt_fb *fb,
				  const char *buf_name)
{
	struct intel_buf *buf;
	uint32_t name, handle, tiling, stride, width, height, bpp, size;

	igt_assert_eq(fb->offsets[0], 0);

	tiling = igt_fb_mod_to_tiling(fb->modifier);
	stride = fb->strides[0];
	bpp = fb->plane_bpp[0];
	size = fb->size;
	width = stride / (bpp / 8);
	height = size / stride;

	name = gem_flink(data->drm_fd, fb->gem_handle);
	handle = gem_open(data->drm_fd, name);
	buf = intel_buf_create_using_handle(data->bops, handle, width, height,
					    bpp, 0, tiling, 0);
	intel_buf_set_name(buf, buf_name);
	intel_buf_set_ownership(buf, true);

	return buf;
}

static void fini_buf(struct intel_buf *buf)
{
	intel_buf_destroy(buf);
}

static void setup_fb(data_t *data, struct igt_fb *newfb, uint32_t width,
		     uint32_t height, uint64_t format, uint64_t modifier, uint64_t stride)
{
	struct drm_mode_fb_cmd2 f = {0};
	cairo_t *cr;

	newfb->strides[0] = stride;
	igt_create_bo_for_fb(data->drm_fd, width, height, format, modifier,
			     newfb);

	igt_assert(newfb->gem_handle > 0);

	f.width = newfb->width;
	f.height = newfb->height;
	f.pixel_format = newfb->drm_format;
	f.flags = DRM_MODE_FB_MODIFIERS;

	for (int n = 0; n < newfb->num_planes; n++) {
		f.handles[n] = newfb->gem_handle;
		f.modifier[n] = newfb->modifier;
		f.pitches[n] = newfb->strides[n];
		f.offsets[n] = newfb->offsets[n];
	}

       if (data->planeclearrgb[0] != 0.0 || data->planeclearrgb[1] != 0.0 ||
           data->planeclearrgb[2] != 0.0) {
               cr = igt_get_cairo_ctx(data->drm_fd, newfb);
               igt_paint_color(cr, 0, 0, newfb->width, newfb->height,
                               data->planeclearrgb[0],
                               data->planeclearrgb[1],
                               data->planeclearrgb[2]);
               igt_put_cairo_ctx(cr);
       }

	igt_assert(drmIoctl(data->drm_fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
	newfb->fb_id = f.fb_id;
}

static void copy_pattern(data_t *data,
			 struct igt_fb *dst_fb, int dx, int dy,
			 struct igt_fb *src_fb, int sx, int sy,
			 int w, int h)
{
	struct intel_buf *src, *dst;

	src = init_buf(data, src_fb, "big fb src");
	dst = init_buf(data, dst_fb, "big fb dst");

	gem_set_domain(data->drm_fd, dst_fb->gem_handle,
		       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	gem_set_domain(data->drm_fd, src_fb->gem_handle,
		       I915_GEM_DOMAIN_GTT, 0);

	/*
	 * We expect the kernel to limit the max fb
	 * size/stride to something that can still
	 * rendered with the blitter/render engine.
	 */
	if (data->render_copy) {
		data->render_copy(data->ibb, src, sx, sy, w, h, dst, dx, dy);
	} else {
		w = min(w, src_fb->width - sx);
		w = min(w, dst_fb->width - dx);

		h = min(h, src_fb->height - sy);
		h = min(h, dst_fb->height - dy);

		intel_bb_blt_copy(data->ibb, src, sx, sy, src->surface[0].stride,
				  dst, dx, dy, dst->surface[0].stride, w, h, dst->bpp);
	}

	fini_buf(dst);
	fini_buf(src);

	/* intel_bb cache doesn't know when objects dissappear, so
	 * let's purge the cache */
	intel_bb_reset(data->ibb, true);
}

static void generate_pattern(data_t *data,
			     struct igt_fb *fb,
			     int w, int h)
{
	struct igt_fb pat_fb;

	igt_create_pattern_fb(data->drm_fd, w, h,
			      data->format, data->modifier,
			      &pat_fb);

	for (int y = 0; y < fb->height; y += h) {
		for (int x = 0; x < fb->width; x += w) {
			copy_pattern(data, fb, x, y,
				     &pat_fb, 0, 0,
				     pat_fb.width, pat_fb.height);
			w++;
			h++;
		}
	}

	igt_remove_fb(data->drm_fd, &pat_fb);
}

static bool size_ok(data_t *data, uint64_t size)
{
	/*
	 * The kernel limits scanout to the
	 * mappable portion of ggtt on gmch platforms.
	 */
	if ((intel_display_ver(data->devid) < 5 ||
	     IS_VALLEYVIEW(data->devid) ||
	     IS_CHERRYVIEW(data->devid)) &&
	    size > data->mappable_size / 2)
		return false;

	/*
	 * Limit the big fb size to at most half the RAM or half
	 * the aperture size. Could go a bit higher I suppose since
	 * we shouldn't need more than one big fb at a time.
	 */
	if (size > data->ram_size / 2 || size > data->aper_size / 2)
		return false;

	return true;
}


static void max_fb_size(data_t *data, int *width, int *height,
			uint32_t format, uint64_t modifier)
{
	unsigned int stride;
	uint64_t size;
	int i = 0;

	/* max fence stride is only 8k bytes on gen3 */
	if (intel_display_ver(data->devid) < 4 &&
	    format == DRM_FORMAT_XRGB8888)
		*width = min(*width, 8192 / 4);

	igt_calc_fb_size(data->drm_fd, *width, *height,
			 format, modifier, &size, &stride);

	while (!size_ok(data, size)) {
		if (i++ & 1)
			*width >>= 1;
		else
			*height >>= 1;

		igt_calc_fb_size(data->drm_fd, *width, *height,
				 format, modifier, &size, &stride);
	}

	igt_info("Max usable framebuffer size for format "IGT_FORMAT_FMT" / modifier 0x%"PRIx64": %dx%d\n",
		 IGT_FORMAT_ARGS(format), modifier,
		 *width, *height);
}

static void prep_fb(data_t *data)
{
	if (data->big_fb.fb_id)
		return;

	if (!data->max_hw_stride_test) {
		igt_create_fb(data->drm_fd,
			data->big_fb_width, data->big_fb_height,
			data->format, data->modifier,
			&data->big_fb);
	} else {
		setup_fb(data, &data->big_fb, data->big_fb_width,
			 data->big_fb_height, data->format, data->modifier,
			 data->hw_stride);
		igt_debug("using stride length %d\n", data->hw_stride);
	}

	generate_pattern(data, &data->big_fb, 640, 480);
}

static void cleanup_fb(data_t *data)
{
	igt_remove_fb(data->drm_fd, &data->big_fb);
	data->big_fb.fb_id = 0;
}

static void set_c8_lut(data_t *data)
{
	igt_pipe_t *pipe = &data->display.pipes[data->pipe];
	struct drm_color_lut *lut;
	int i, lut_size = 256;

	lut = calloc(lut_size, sizeof(lut[0]));

	/* igt_fb uses RGB332 for C8 */
	for (i = 0; i < lut_size; i++) {
		lut[i].red = ((i & 0xe0) >> 5) * 0xffff / 0x7;
		lut[i].green = ((i & 0x1c) >> 2) * 0xffff / 0x7;
		lut[i].blue = ((i & 0x03) >> 0) * 0xffff / 0x3;
	}

	igt_pipe_obj_replace_prop_blob(pipe, IGT_CRTC_GAMMA_LUT, lut,
				       lut_size * sizeof(lut[0]));

	free(lut);
}

static void unset_lut(data_t *data)
{
	igt_pipe_t *pipe = &data->display.pipes[data->pipe];

	igt_pipe_obj_replace_prop_blob(pipe, IGT_CRTC_GAMMA_LUT, NULL, 0);
}

static bool test_plane(data_t *data)
{
	igt_plane_t *plane = data->plane;
	struct igt_fb *small_fb = &data->small_fb;
	struct igt_fb *big_fb = &data->big_fb;
	int w = data->big_fb_width - small_fb->width;
	int h = data->big_fb_height - small_fb->height;
	struct {
		int x, y;
	} coords[] = {
		/* bunch of coordinates pulled out of thin air */
		{ 0, 0, },
		{ w * 4 / 7, h / 5, },
		{ w * 3 / 7, h / 3, },
		{ w / 2, h / 2, },
		{ w / 3, h * 3 / 4, },
		{ w, h, },
	};

	if (!igt_plane_has_format_mod(plane, data->format, data->modifier))
		return false;

	if (!igt_plane_has_rotation(plane, data->rotation))
		return false;

	if (igt_plane_has_prop(plane, IGT_PLANE_ROTATION))
		igt_plane_set_rotation(plane, data->rotation);
	igt_plane_set_position(plane, 0, 0);

	for (int i = 0; i < ARRAY_SIZE(coords); i++) {
		igt_crc_t small_crc, big_crc;
		int x = coords[i].x;
		int y = coords[i].y;

		/* Hardware limitation */
		if (data->format == DRM_FORMAT_RGB565 &&
		    igt_rotation_90_or_270(data->rotation)) {
			x &= ~1;
			y &= ~1;
		}

		igt_plane_set_fb(plane, small_fb);
		igt_plane_set_size(plane, data->width, data->height);

		/*
		 * Try to check that the rotation+format+modifier
		 * combo is supported.
		 */
		if (i == 0 && data->display.is_atomic &&
		    igt_display_try_commit_atomic(&data->display,
						  DRM_MODE_ATOMIC_ALLOW_MODESET |
						  DRM_MODE_ATOMIC_TEST_ONLY,
						  NULL) != 0) {
			if (igt_plane_has_prop(plane, IGT_PLANE_ROTATION))
				igt_plane_set_rotation(plane, IGT_ROTATION_0);
			igt_plane_set_fb(plane, NULL);
			return false;
		}

		/*
		 * To speed up skips we delay the big fb creation until
		 * the above rotation related check has been performed.
		 */
		prep_fb(data);

		/*
		 * Make a 1:1 copy of the desired part of the big fb
		 * rather than try to render the same pattern (translated
		 * accordinly) again via cairo. Something in cairo's
		 * rendering pipeline introduces slight differences into
		 * the result if we try that, and so the crc will not match.
		 */
		igt_pipe_crc_start(data->pipe_crc);
		copy_pattern(data, small_fb, 0, 0, big_fb, x, y,
			     small_fb->width, small_fb->height);

		igt_display_commit2(&data->display, data->display.is_atomic ?
				    COMMIT_ATOMIC : COMMIT_UNIVERSAL);
		igt_pipe_crc_get_current(data->display.drm_fd, data->pipe_crc, &small_crc);

		igt_plane_set_fb(plane, big_fb);
		igt_fb_set_position(big_fb, plane, x, y);
		igt_fb_set_size(big_fb, plane, small_fb->width, small_fb->height);
		igt_plane_set_size(plane, data->width, data->height);
		igt_display_commit2(&data->display, data->display.is_atomic ?
				    COMMIT_ATOMIC : COMMIT_UNIVERSAL);

		igt_pipe_crc_get_current(data->display.drm_fd, data->pipe_crc, &big_crc);

		igt_plane_set_fb(plane, NULL);

		igt_assert_crc_equal(&big_crc, &small_crc);
		igt_pipe_crc_stop(data->pipe_crc);
	}

	return true;
}

static bool test_pipe(data_t *data)
{
	uint16_t width, height;
	drmModeModeInfo *mode;
	igt_plane_t *primary;
	bool ret = false;

	if (data->format == DRM_FORMAT_C8 &&
	    !igt_pipe_obj_has_prop(&data->display.pipes[data->pipe],
				   IGT_CRTC_GAMMA_LUT))
		return false;

	mode = igt_output_get_mode(data->output);

	data->width = mode->hdisplay;
	data->height = mode->vdisplay;

	width = mode->hdisplay;
	height = mode->vdisplay;
	if (igt_rotation_90_or_270(data->rotation))
		igt_swap(width, height);

	igt_create_color_fb(data->drm_fd, width, height,
			    data->format, data->modifier,
			    0, 1, 0, &data->small_fb);

	igt_output_set_pipe(data->output, data->pipe);

	primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	if (!data->display.is_atomic) {
		struct igt_fb fb;

		igt_create_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			      DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
			      &fb);

		/* legacy setcrtc needs an fb */
		igt_plane_set_fb(primary, &fb);
		igt_display_commit2(&data->display, COMMIT_LEGACY);

		igt_plane_set_fb(primary, NULL);
		igt_display_commit2(&data->display, COMMIT_UNIVERSAL);

		igt_remove_fb(data->drm_fd, &fb);
	}

	if (data->format == DRM_FORMAT_C8)
		set_c8_lut(data);

	igt_display_commit2(&data->display, data->display.is_atomic ?
			    COMMIT_ATOMIC : COMMIT_UNIVERSAL);

	data->pipe_crc = igt_pipe_crc_new(data->drm_fd, data->pipe,
					  IGT_PIPE_CRC_SOURCE_AUTO);

	for_each_plane_on_pipe(&data->display, data->pipe, data->plane) {
		ret = test_plane(data);
		if (ret)
			break;
	}

	if (data->format == DRM_FORMAT_C8)
		unset_lut(data);

	igt_pipe_crc_free(data->pipe_crc);

	igt_output_set_pipe(data->output, PIPE_ANY);

	igt_remove_fb(data->drm_fd, &data->small_fb);

	return ret;
}

static bool
max_hw_stride_async_flip_test(data_t *data)
{
	uint32_t ret, startframe;
	const uint32_t w = data->output->config.default_mode.hdisplay,
		       h = data->output->config.default_mode.vdisplay;
	igt_plane_t *primary;
	igt_crc_t compare_crc, async_crc;

	igt_require(data->display.is_atomic);
	igt_output_set_pipe(data->output, data->pipe);

	primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_rotation(primary, data->rotation);

	igt_require_f(igt_display_try_commit2(&data->display, COMMIT_ATOMIC) == 0,
		      "rotation/flip not supported\n");

	setup_fb(data, &data->big_fb, data->big_fb_width, data->big_fb_height,
		 data->format, data->modifier, data->hw_stride);
	generate_pattern(data, &data->big_fb, 640, 480);

	data->planeclearrgb[1] = 1.0;

	setup_fb(data, &data->big_fb_flip[0], data->big_fb_width,
		 data->big_fb_height, data->format, data->modifier,
		 data->hw_stride);

	data->planeclearrgb[1] = 0.0;

	setup_fb(data, &data->big_fb_flip[1], data->big_fb_width,
		 data->big_fb_height, data->format, data->modifier,
		 data->hw_stride);
	generate_pattern(data, &data->big_fb_flip[1], 640, 480);

	data->pipe_crc = igt_pipe_crc_new(data->drm_fd, data->pipe,
					  IGT_PIPE_CRC_SOURCE_AUTO);
	igt_pipe_crc_start(data->pipe_crc);

	igt_set_timeout(5, "Async pageflipping loop got stuck!\n");
	for (int i = 0; i < 2; i++) {
		igt_plane_set_fb(primary, &data->big_fb);
		igt_fb_set_size(&data->big_fb, primary, w, h);
		igt_plane_set_size(primary, w, h);
		igt_display_commit_atomic(&data->display,
					  DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

		igt_wait_for_vblank(data->drm_fd, data->display.pipes[primary->pipe->pipe].crtc_offset);
		startframe = kmstest_get_vblank(data->drm_fd, data->pipe, 0) + 1;

		for (int j = 0; j < 2; j++) {
			do {
				ret = drmModePageFlip(data->drm_fd, data->output->config.crtc->crtc_id,
						      data->big_fb_flip[i].fb_id,
						      DRM_MODE_PAGE_FLIP_ASYNC, NULL);
			} while (ret == -EBUSY);
			igt_assert(ret == 0);

			do {
				ret = drmModePageFlip(data->drm_fd, data->output->config.crtc->crtc_id,
						      data->big_fb.fb_id,
						      DRM_MODE_PAGE_FLIP_ASYNC, NULL);
			} while (ret == -EBUSY);
			igt_assert(ret == 0);
		}

		igt_pipe_crc_get_for_frame(data->drm_fd, data->pipe_crc,
					   startframe, &compare_crc);
		igt_pipe_crc_get_for_frame(data->drm_fd, data->pipe_crc,
					   startframe + 1, &async_crc);

		igt_assert_f(kmstest_get_vblank(data->drm_fd, data->pipe, 0) -
			     startframe == 1, "lost frames\n");

		igt_assert_f(igt_check_crc_equal(&compare_crc, &async_crc)^(i^1),
			     "CRC failure with async flip, crc %s match for checked round\n",
			     i?"should":"shouldn't");
	}
	igt_reset_timeout();

	igt_pipe_crc_free(data->pipe_crc);
	igt_output_set_pipe(data->output, PIPE_NONE);
	igt_remove_fb(data->drm_fd, &data->big_fb);
	igt_remove_fb(data->drm_fd, &data->big_fb_flip[0]);
	igt_remove_fb(data->drm_fd, &data->big_fb_flip[1]);
	return true;
}

static void test_scanout(data_t *data)
{
	igt_output_t *output;

	if (data->max_hw_stride_test) {
		data->big_fb_width = data->max_hw_fb_width;
		data->big_fb_height = 0;

		for_each_connected_output(&data->display, output) {
			if (data->big_fb_height < output->config.default_mode.vdisplay * 2)
				data->big_fb_height = output->config.default_mode.vdisplay * 2;
		}
	} else {
		data->big_fb_width = data->max_fb_width;
		data->big_fb_height = data->max_fb_height;
	}

	max_fb_size(data, &data->big_fb_width, &data->big_fb_height,
		    data->format, data->modifier);

	for_each_pipe_with_valid_output(&data->display, data->pipe, data->output) {
		if (data->async_flip_test) {
			if (max_hw_stride_async_flip_test(data))
				return;
		} else {
			if (test_pipe(data))
				return;
		}
		break;
	}

	igt_skip("unsupported configuration\n");
}

static void
test_size_overflow(data_t *data)
{
	uint32_t fb_id;
	uint32_t bo;
	uint32_t offsets[4] = {};
	uint32_t strides[4] = { 256*1024, };
	int ret;

	igt_require(igt_display_has_format_mod(&data->display,
					       DRM_FORMAT_XRGB8888,
					       data->modifier));

	/*
	 * Try to hit a specific integer overflow in i915 fb size
	 * calculations. 256k * 16k == 1<<32 which is checked
	 * against the bo size. The check should fail on account
	 * of the bo being smaller, but due to the overflow the
	 * computed fb size is 0 and thus the check never trips.
	 */
	igt_require(data->max_fb_width >= 16383 &&
		    data->max_fb_height >= 16383);

	bo = gem_buffer_create_fb_obj(data->drm_fd, (1ULL << 32) - 4096);

	igt_require(bo);

	ret = __kms_addfb(data->drm_fd, bo,
			  16383, 16383,
			  DRM_FORMAT_XRGB8888,
			  data->modifier,
			  strides, offsets, 1,
			  DRM_MODE_FB_MODIFIERS, &fb_id);

	igt_assert_neq(ret, 0);

	gem_close(data->drm_fd, bo);
}

static void
test_size_offset_overflow(data_t *data)
{
	uint32_t fb_id;
	uint32_t bo;
	uint32_t offsets[4] = {};
	uint32_t strides[4] = { 8192, };
	int ret;

	igt_require(igt_display_has_format_mod(&data->display,
					       DRM_FORMAT_NV12,
					       data->modifier));

	/*
	 * Try to hit a specific integer overflow in i915 fb size
	 * calculations. This time it's offsets[1] + the tile
	 * aligned chroma plane size that overflows and
	 * incorrectly passes the bo size check.
	 */
	igt_require(igt_display_has_format_mod(&data->display,
					       DRM_FORMAT_NV12,
					       data->modifier));

	bo = gem_buffer_create_fb_obj(data->drm_fd, (1ULL << 32) - 4096);
	igt_require(bo);

	offsets[0] = 0;
	offsets[1] = (1ULL << 32) - 8192 * 4096;

	ret = __kms_addfb(data->drm_fd, bo,
			  8192, 8188,
			  DRM_FORMAT_NV12,
			  data->modifier,
			  strides, offsets, 1,
			  DRM_MODE_FB_MODIFIERS, &fb_id);
	igt_assert_neq(ret, 0);

	gem_close(data->drm_fd, bo);
}

static int rmfb(int fd, uint32_t id)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_MODE_RMFB, &id))
		err = -errno;

	errno = 0;
	return err;
}

static void
test_addfb(data_t *data)
{
	uint64_t size;
	uint32_t fb_id;
	uint32_t bo;
	uint32_t offsets[4] = {};
	uint32_t strides[4] = {};
	uint32_t format;
	int ret;

	/*
	 * gen3 max tiled stride is 8k bytes, but
	 * max fb size of 4k pixels, hence we can't test
	 * with 32bpp and must use 16bpp instead.
	 */
	if (intel_display_ver(data->devid) == 3)
		format = DRM_FORMAT_RGB565;
	else
		format = DRM_FORMAT_XRGB8888;

	igt_require(igt_display_has_format_mod(&data->display,
					       format, data->modifier));

	igt_calc_fb_size(data->drm_fd,
			 data->max_fb_width,
			 data->max_fb_height,
			 format, data->modifier,
			 &size, &strides[0]);

	bo = gem_buffer_create_fb_obj(data->drm_fd, size);
	igt_require(bo);

	if (intel_display_ver(data->devid) < 4)
		gem_set_tiling(data->drm_fd, bo,
			       igt_fb_mod_to_tiling(data->modifier), strides[0]);

	ret = __kms_addfb(data->drm_fd, bo,
			  data->max_fb_width,
			  data->max_fb_height,
			  format, data->modifier,
			  strides, offsets, 1,
			  DRM_MODE_FB_MODIFIERS, &fb_id);
	igt_assert_eq(ret, 0);

	rmfb(data->drm_fd, fb_id);
	gem_close(data->drm_fd, bo);
}

/*
 * TODO: adapt i9xx_plane_max_stride(..) here from intel_display.c
 * in kernel sources to support older gen for max hw stride length
 * testing.
 */
static void
set_max_hw_stride(data_t *data)
{
	if (intel_display_ver(data->devid) >= 13) {
		/*
		 * The stride in bytes must not exceed of the size
		 * of 128K bytes. For pixel formats of 64bpp will allow
		 * for a 16K pixel surface.
		 */
		data->hw_stride = 131072;
	} else {
		data->hw_stride = 32768;
	}
}

static data_t data = {};

static const struct {
	uint64_t modifier;
	const char *name;
} modifiers[] = {
	{ DRM_FORMAT_MOD_LINEAR, "linear", },
	{ I915_FORMAT_MOD_X_TILED, "x-tiled", },
	{ I915_FORMAT_MOD_Y_TILED, "y-tiled", },
	{ I915_FORMAT_MOD_Yf_TILED, "yf-tiled", },
};

static const struct {
	uint32_t format;
	uint8_t bpp;
} formats[] = {
	{ DRM_FORMAT_C8, 8, },
	{ DRM_FORMAT_RGB565, 16, },
	{ DRM_FORMAT_XRGB8888, 32, },
	{ DRM_FORMAT_XBGR16161616F, 64, },
};

static const struct {
	igt_rotation_t rotation;
	uint16_t angle;
} rotations[] = {
	{ IGT_ROTATION_0, 0, },
	{ IGT_ROTATION_90, 90, },
	{ IGT_ROTATION_180, 180, },
	{ IGT_ROTATION_270, 270, },
};

static const struct {
	igt_rotation_t flip;
	const char *flipname;
} fliptab[] = {
	{ 0, "" },
	{ IGT_REFLECT_X, "-hflip" },
};
igt_main
{
	igt_fixture {
		drmModeResPtr res;

		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);

		igt_require(is_i915_device(data.drm_fd));

		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc(data.drm_fd);
		igt_display_require(&data.display, data.drm_fd);

		res = drmModeGetResources(data.drm_fd);
		igt_assert(res);

		data.max_fb_width = res->max_width;
		data.max_fb_height = res->max_height;

		drmModeFreeResources(res);

		igt_info("Max driver framebuffer size %dx%d\n",
			 data.max_fb_width, data.max_fb_height);

		data.ram_size = intel_get_total_ram_mb() << 20;
		data.aper_size = gem_aperture_size(data.drm_fd);
		data.mappable_size = gem_mappable_aperture_size(data.drm_fd);

		igt_info("RAM: %"PRIu64" MiB, GPU address space: %"PRId64" MiB, GGTT mappable size: %"PRId64" MiB\n",
			 data.ram_size >> 20, data.aper_size >> 20,
			 data.mappable_size >> 20);

		/*
		 * Gen3 render engine is limited to 2kx2k, whereas
		 * the display engine can do 4kx4k. Use the blitter
		 * on gen3 to avoid exceeding the render engine limits.
		 * On gen2 we could use either, but let's go for the
		 * blitter there as well.
		 */
		if (intel_display_ver(data.devid) >= 4)
			data.render_copy = igt_get_render_copyfunc(data.devid);

		data.bops = buf_ops_create(data.drm_fd);
		data.ibb = intel_bb_create(data.drm_fd, 4096);

		data.planeclearrgb[0] = 0.0;
		data.planeclearrgb[1] = 0.0;
		data.planeclearrgb[2] = 0.0;

		data.max_hw_stride_test = false;
		data.async_flip_test = false;
	}

	/*
	 * Skip linear as it doesn't hit the overflow we want
	 * on account of the tile height being effectively one,
	 * and thus the kenrnel rounding up to the next tile
	 * height won't do anything.
	 */
	igt_describe("Sanity check if addfb ioctl fails correctly for given modifier with small bo");
	for (int i = 1; i < ARRAY_SIZE(modifiers); i++) {
		igt_subtest_f("%s-addfb-size-overflow",
			      modifiers[i].name) {
			data.modifier = modifiers[i].modifier;
			data.ibb = intel_bb_create(data.drm_fd, 4096);
			test_size_overflow(&data);
			intel_bb_destroy(data.ibb);
		}
	}

	igt_describe("Sanity check if addfb ioctl fails correctly for given modifier and offsets with small bo");
	for (int i = 1; i < ARRAY_SIZE(modifiers); i++) {
		igt_subtest_f("%s-addfb-size-offset-overflow",
			      modifiers[i].name) {
			data.modifier = modifiers[i].modifier;
			data.ibb = intel_bb_create(data.drm_fd, 4096);
			test_size_offset_overflow(&data);
			intel_bb_destroy(data.ibb);
		}
	}

	igt_describe("Sanity check if addfb ioctl works correctly for given size and strides of fb");
	for (int i = 0; i < ARRAY_SIZE(modifiers); i++) {
		igt_subtest_f("%s-addfb", modifiers[i].name) {
			data.modifier = modifiers[i].modifier;
			data.ibb = intel_bb_create(data.drm_fd, 4096);
			test_addfb(&data);
			intel_bb_destroy(data.ibb);
		}
	}

	for (int i = 0; i < ARRAY_SIZE(modifiers); i++) {
		data.modifier = modifiers[i].modifier;

		for (int j = 0; j < ARRAY_SIZE(formats); j++) {
			data.format = formats[j].format;

			for (int k = 0; k < ARRAY_SIZE(rotations); k++) {
				data.rotation = rotations[k].rotation;

				igt_describe("Sanity check if addfb ioctl works correctly for given "
						"combination of modifier formats and rotation");
				igt_subtest_f("%s-%dbpp-rotate-%d", modifiers[i].name,
					      formats[j].bpp, rotations[k].angle) {
					igt_require(data.format == DRM_FORMAT_C8 ||
						    igt_fb_supported_format(data.format));
					igt_require(igt_display_has_format_mod(&data.display, data.format, data.modifier));
					data.ibb = intel_bb_create(data.drm_fd, 4096);
					test_scanout(&data);
					intel_bb_destroy(data.ibb);
				}
			}

			igt_fixture
				cleanup_fb(&data);
		}
	}

	data.max_hw_stride_test = true;
	// Run max hw stride length tests on gen5 and later.
	for (int i = 0; i < ARRAY_SIZE(modifiers); i++) {
		data.modifier = modifiers[i].modifier;

		set_max_hw_stride(&data);

		for (int l = 0; l < ARRAY_SIZE(fliptab); l++) {
			for (int j = 0; j < ARRAY_SIZE(formats); j++) {
				/*
				* try only those formats which can show full length.
				* Here 32K is used to have CI test results consistent
				* for all platforms, 32K is smallest number possbily
				* coming to data.hw_stride from above set_max_hw_stride()
				*/
				if (32768 / (formats[j].bpp >> 3) > 8192)
					continue;

				data.format = formats[j].format;

				for (int k = 0; k < ARRAY_SIZE(rotations); k++) {
					data.rotation = rotations[k].rotation | fliptab[l].flip;

					// this combination will never happen.
					if (igt_rotation_90_or_270(data.rotation) ||
					    (fliptab[l].flip == IGT_REFLECT_X && modifiers[i].modifier == DRM_FORMAT_MOD_LINEAR))
						continue;

					igt_describe("test maximum hardware supported stride length for given bpp and modifiers.");
					igt_subtest_f("%s-max-hw-stride-%dbpp-rotate-%d%s", modifiers[i].name,
						formats[j].bpp, rotations[k].angle, fliptab[l].flipname) {
						igt_require(intel_display_ver(intel_get_drm_devid(data.drm_fd)) >= 5);
						if (data.format_override != 0) {
							igt_info("using format override fourcc %.4s\n", (char *)&data.format_override);
							data.format = data.format_override;
						}
						if (data.stride_override != 0) {
							igt_info("using FB width override %.d\n", data.stride_override);
							data.hw_stride = data.stride_override;
							data.max_hw_fb_width = data.stride_override;

						} else {
							data.max_hw_fb_width = min(data.hw_stride / (formats[j].bpp >> 3), data.max_fb_width);
						}

						igt_require(data.format == DRM_FORMAT_C8 ||
							igt_fb_supported_format(data.format));
						igt_require(igt_display_has_format_mod(&data.display, data.format, data.modifier));
						test_scanout(&data);
					}

					// async flip doesn't support linear fbs.
					if (modifiers[i].modifier == DRM_FORMAT_MOD_LINEAR)
						continue;

					data.async_flip_test = true;
					igt_describe("test async flip on maximum hardware supported stride length for given bpp and modifiers.");
					igt_subtest_f("%s-max-hw-stride-%dbpp-rotate-%d%s-async-flip", modifiers[i].name,
						formats[j].bpp, rotations[k].angle, fliptab[l].flipname) {
							igt_require(data.format == DRM_FORMAT_C8 ||
								igt_fb_supported_format(data.format));
							igt_require(igt_display_has_format_mod(&data.display, data.format, data.modifier));
							igt_require(igt_has_drm_cap(data.drm_fd, DRM_CAP_ASYNC_PAGE_FLIP));
							data.max_hw_fb_width = min(data.hw_stride / (formats[j].bpp >> 3), data.max_fb_width);
							test_scanout(&data);
					}
					data.async_flip_test = false;
				}

				igt_fixture
					cleanup_fb(&data);
			}
		}
	}
	data.max_hw_stride_test = false;

	igt_fixture {
		igt_display_fini(&data.display);
		buf_ops_destroy(data.bops);
	}
}
