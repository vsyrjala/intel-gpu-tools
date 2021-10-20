/*
 * Copyright © 2015 Intel Corporation
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

/* This program tests whether the igt_draw library actually works. */

#include "i915/gem.h"
#include "igt.h"

#define MAX_CONNECTORS 32

struct modeset_params {
	uint32_t crtc_id;
	uint32_t connector_id;
	drmModeModeInfoPtr mode;
};

int drm_fd;
drmModeResPtr drm_res;
drmModeConnectorPtr drm_connectors[MAX_CONNECTORS];
struct buf_ops *bops;
igt_pipe_crc_t *pipe_crc;

#define N_FORMATS 3
static const uint32_t formats[N_FORMATS] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB2101010,
};

#define N_MODIFIER_METHODS 3
static const uint64_t modifiers[N_MODIFIER_METHODS] = {
	DRM_FORMAT_MOD_LINEAR,
	I915_FORMAT_MOD_X_TILED,
	I915_FORMAT_MOD_Y_TILED,
};

struct base_crc {
	bool set;
	igt_crc_t crc;
};
struct base_crc base_crcs[N_FORMATS];

struct modeset_params ms;

static void find_modeset_params(void)
{
	int i;
	uint32_t crtc_id;
	drmModeConnectorPtr connector = NULL;
	drmModeModeInfoPtr mode = NULL;

	for (i = 0; i < drm_res->count_connectors; i++) {
		drmModeConnectorPtr c = drm_connectors[i];

		if (c->count_modes) {
			connector = c;
			mode = &c->modes[0];
			break;
		}
	}
	igt_require(connector);

	crtc_id = kmstest_find_crtc_for_connector(drm_fd, drm_res, connector,
						  0);
	igt_assert(mode);

	ms.connector_id = connector->connector_id;
	ms.crtc_id = crtc_id;
	ms.mode = mode;

}

static uint32_t get_color(uint32_t drm_format, bool r, bool g, bool b)
{
	uint32_t color = 0;

	switch (drm_format) {
	case DRM_FORMAT_RGB565:
		color |= r ? 0x1F << 11 : 0;
		color |= g ? 0x3F << 5 : 0;
		color |= b ? 0x1F : 0;
		break;
	case DRM_FORMAT_XRGB8888:
		color |= r ? 0xFF << 16 : 0;
		color |= g ? 0xFF << 8 : 0;
		color |= b ? 0xFF : 0;
		break;
	case DRM_FORMAT_XRGB2101010:
		color |= r ? 0x3FF << 20 : 0;
		color |= g ? 0x3FF << 10 : 0;
		color |= b ? 0x3FF : 0;
		break;
	default:
		igt_assert(false);
	}

	return color;
}

static void get_method_crc(enum igt_draw_method method, uint32_t drm_format,
			   uint64_t modifier, igt_crc_t *crc)
{
	struct igt_fb fb;
	int rc;

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      drm_format, modifier, &fb);
	igt_draw_rect_fb(drm_fd, bops, 0, &fb, method,
			 0, 0, fb.width, fb.height,
			 get_color(drm_format, 0, 0, 1));

	igt_draw_rect_fb(drm_fd, bops, 0, &fb, method,
			 fb.width / 4, fb.height / 4,
			 fb.width / 2, fb.height / 2,
			 get_color(drm_format, 0, 1, 0));
	igt_draw_rect_fb(drm_fd, bops, 0, &fb, method,
			 fb.width / 8, fb.height / 8,
			 fb.width / 4, fb.height / 4,
			 get_color(drm_format, 1, 0, 0));
	igt_draw_rect_fb(drm_fd, bops, 0, &fb, method,
			 fb.width / 2, fb.height / 2,
			 fb.width / 3, fb.height / 3,
			 get_color(drm_format, 1, 0, 1));
	igt_draw_rect_fb(drm_fd, bops, 0, &fb, method, 1, 1, 15, 15,
			 get_color(drm_format, 0, 1, 1));

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert_eq(rc, 0);

	igt_pipe_crc_collect_crc(pipe_crc, crc);

	igt_remove_fb(drm_fd, &fb);
}

static bool format_is_supported(uint32_t format, uint64_t modifier)
{
	uint32_t gem_handle, fb_id;
	unsigned int offsets[4] = {};
	unsigned int strides[4] = {};
	int ret;

	gem_handle = igt_create_bo_with_dimensions(drm_fd, 64, 64,
						   format, modifier,
						   0, NULL, &strides[0], NULL);
	ret =  __kms_addfb(drm_fd, gem_handle, 64, 64,
			   format, modifier, strides, offsets, 1,
			   DRM_MODE_FB_MODIFIERS, &fb_id);
	drmModeRmFB(drm_fd, fb_id);
	gem_close(drm_fd, gem_handle);

	return ret == 0;
}

static void draw_method_subtest(enum igt_draw_method method,
				uint32_t format_index, uint64_t modifier)
{
	igt_crc_t crc;

	igt_skip_on(method == IGT_DRAW_MMAP_WC && !gem_mmap__has_wc(drm_fd));
	igt_skip_on(method == IGT_DRAW_MMAP_GTT &&
		    !gem_has_mappable_ggtt(drm_fd));

	igt_require(format_is_supported(formats[format_index], modifier));

	/* Use IGT_DRAW_MMAP_GTT/WC on an untiled buffer as the parameter for
	 * comparison. Cache the value so we don't recompute it for every single
	 * subtest. */
	if (!base_crcs[format_index].set) {
		get_method_crc(gem_has_mappable_ggtt(drm_fd) ? IGT_DRAW_MMAP_GTT :
							       IGT_DRAW_MMAP_WC,
			       formats[format_index],
			       DRM_FORMAT_MOD_LINEAR,
			       &base_crcs[format_index].crc);
		base_crcs[format_index].set = true;
	}

	get_method_crc(method, formats[format_index], modifier, &crc);
	igt_assert_crc_equal(&crc, &base_crcs[format_index].crc);
}

static void get_fill_crc(uint64_t modifier, igt_crc_t *crc)
{
	struct igt_fb fb;
	int rc;

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, modifier, &fb);

	igt_draw_fill_fb(drm_fd, &fb, 0xFF);

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert_eq(rc, 0);

	igt_pipe_crc_collect_crc(pipe_crc, crc);

	igt_remove_fb(drm_fd, &fb);
}

static void fill_fb_subtest(void)
{
	int rc;
	struct igt_fb fb;
	igt_crc_t base_crc, crc;

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, &fb);

	igt_draw_rect_fb(drm_fd, bops, 0, &fb,
			 gem_has_mappable_ggtt(drm_fd) ? IGT_DRAW_MMAP_GTT :
							 IGT_DRAW_MMAP_WC,
			 0, 0, fb.width, fb.height, 0xFF);

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert_eq(rc, 0);

	igt_pipe_crc_collect_crc(pipe_crc, &base_crc);

	get_fill_crc(DRM_FORMAT_MOD_LINEAR, &crc);
	igt_assert_crc_equal(&crc, &base_crc);

	get_fill_crc(I915_FORMAT_MOD_X_TILED, &crc);
	igt_assert_crc_equal(&crc, &base_crc);

	if (intel_display_ver(intel_get_drm_devid(drm_fd)) >= 9) {
		get_fill_crc(I915_FORMAT_MOD_Y_TILED, &crc);
		igt_assert_crc_equal(&crc, &base_crc);
	}

	igt_remove_fb(drm_fd, &fb);
}

static void setup_environment(void)
{
	int i;

	drm_fd = drm_open_driver_master(DRIVER_INTEL);
	igt_require(drm_fd >= 0);

	drm_res = drmModeGetResources(drm_fd);
	igt_require(drm_res);
	igt_assert(drm_res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm_res->count_connectors; i++)
		drm_connectors[i] = drmModeGetConnectorCurrent(drm_fd,
							       drm_res->connectors[i]);

	kmstest_set_vt_graphics_mode();

	bops = buf_ops_create(drm_fd);

	find_modeset_params();
	pipe_crc = igt_pipe_crc_new(drm_fd, kmstest_get_crtc_idx(drm_res, ms.crtc_id),
				    IGT_PIPE_CRC_SOURCE_AUTO);
}

static void teardown_environment(void)
{
	int i;

	igt_pipe_crc_free(pipe_crc);

	buf_ops_destroy(bops);

	for (i = 0; i < drm_res->count_connectors; i++)
		drmModeFreeConnector(drm_connectors[i]);

	drmModeFreeResources(drm_res);
	close(drm_fd);
}

static const char *format_str(int format_index)
{
	switch (formats[format_index]) {
	case DRM_FORMAT_RGB565:
		return "rgb565";
	case DRM_FORMAT_XRGB8888:
		return "xrgb8888";
	case DRM_FORMAT_XRGB2101010:
		return "xrgb2101010";
	default:
		igt_assert(false);
	}
}

static const char *modifier_str(int modifier_index)
{
	switch (modifiers[modifier_index]) {
	case DRM_FORMAT_MOD_LINEAR :
		return "untiled";
	case I915_FORMAT_MOD_X_TILED:
		return "xtiled";
	case I915_FORMAT_MOD_Y_TILED:
		return "ytiled";
	default:
		igt_assert(false);
	}
}

igt_main
{
	enum igt_draw_method method;
	int format_idx, modifier_idx;

	igt_fixture
		setup_environment();

	for (format_idx = 0; format_idx < N_FORMATS; format_idx++) {
	for (method = 0; method < IGT_DRAW_METHOD_COUNT; method++) {
	for (modifier_idx = 0; modifier_idx < N_MODIFIER_METHODS; modifier_idx++) {
		igt_describe("This subtest verfies igt_draw library works "
			     "with different modifiers, DRM_FORMATS, DRAW_METHODS.");
		igt_subtest_f("draw-method-%s-%s-%s",
			      format_str(format_idx),
			      igt_draw_get_method_name(method),
			      modifier_str(modifier_idx))
			draw_method_subtest(method, format_idx,
					    modifiers[modifier_idx]);
	} } }

	igt_describe("This subtest verifies CRC after filling fb with x-tiling "
		     "or none.");
	igt_subtest("fill-fb")
		fill_fb_subtest();

	igt_fixture
		teardown_environment();
}
