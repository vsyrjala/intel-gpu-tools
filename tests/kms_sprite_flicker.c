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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_aux.h"
#include "intel_chipset.h"
#include "intel_io.h"

//#define USE_SPRITE
//#define USE_TILING

static uint16_t pipe_offset[3] = { 0, 0x1000, 0x4000 };

static uint32_t read_reg(uint32_t reg)
{
	return INREG(0x180000 + reg);
}

#define FB_W (mode->hdisplay + 1024/4)
#define FB_H (mode->vdisplay)

#define NCRCS 20

typedef struct {
	int drm_fd;
	igt_display_t display;
	igt_output_t *output;
	enum pipe pipe;
	igt_pipe_crc_t *pipe_crc;
	uint64_t tiling;
} data_t;

static bool crc_equal(const igt_crc_t *a, const igt_crc_t *b)
{
	int i;

	igt_assert(a->n_words > 0);
	igt_assert(a->n_words == b->n_words);

	for (i = 0; i < a->n_words; i++)
		if (a->crc[i] != b->crc[i])
			return false;

	return true;
}

static bool verify_fb(data_t *data, struct igt_fb *fb)
{
	void *ptr;
	const uint32_t *p;
	bool ret = true;
	int x, y;

	gem_set_domain(data->drm_fd, fb->gem_handle, I915_GEM_DOMAIN_GTT, 0);

	ptr = gem_mmap__gtt(data->drm_fd, fb->gem_handle, fb->size, PROT_READ);

	p = ptr;
	for (y = 0; y < fb->height; y++) {
		for (x = 0; x < fb->width; x++) {
			if (p[x] != 0xff0000ff)
				ret = false;
		}
		p += fb->stride / 4;
	}

	munmap(ptr, fb->size);

	return ret;
}

static void test_plane(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	enum pipe pipe = data->pipe;
	igt_plane_t *primary, *sprite;
	struct igt_fb fb;
	drmModeModeInfo *mode;
	igt_crc_t *crc, crc_ref;
	int j = 0;

	igt_output_set_pipe(output, pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return;
	}

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
#ifdef USE_SPRITE
	sprite = igt_output_get_plane(output, IGT_PLANE_2);
#else
	sprite = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
#endif
	mode = igt_output_get_mode(output);

	igt_info("mode %dx%d\n", mode->hdisplay, mode->vdisplay);

	igt_create_color_fb(data->drm_fd,
			    FB_W, FB_H,
			    DRM_FORMAT_XRGB8888,
			    data->tiling,
			    0.0, 0.0, 1.0,
			    &fb);

	igt_plane_set_fb(primary, &fb);
	igt_fb_set_size(&fb, primary, mode->hdisplay, mode->vdisplay);
	igt_plane_set_size(primary, mode->hdisplay, mode->vdisplay);
	igt_display_commit(display);

	igt_debug_wait_for_keypress("pre");

	igt_pipe_crc_collect_crc(data->pipe_crc, &crc_ref);

#ifdef USE_SPRITE
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	igt_remove_fb(data->drm_fd, &fb);
#endif
	memset(&fb, 0, sizeof(fb));

	for (;;) {
		uint32_t surf;
		int i;

		j++;

		igt_create_color_fb(data->drm_fd,
				    FB_W, FB_H,
				    DRM_FORMAT_XRGB8888,
				    data->tiling,
				    0.0, 0.0, 1.0,
				    &fb);

		igt_plane_set_fb(sprite, &fb);
		igt_fb_set_size(&fb, sprite, mode->hdisplay, mode->vdisplay);
		igt_plane_set_size(sprite, mode->hdisplay, mode->vdisplay);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_debug_wait_for_keypress("mid");

		for (i = 0; i < 5; i++) {
			int k;

			igt_pipe_crc_start(data->pipe_crc);
			usleep(250000);
			igt_pipe_crc_get_crcs(data->pipe_crc, NCRCS, &crc);
			igt_pipe_crc_stop(data->pipe_crc);

			for (k = 0; k < NCRCS; k++) {
				if (!crc_equal(&crc[k], &crc_ref))
					break;
			}
			if (k != NCRCS)
				break;
		}
		surf = read_reg(pipe_offset[data->pipe] + DSPASURF);
		if (i != 5) {
			igt_info("0x%08x\n", surf);
			if ((surf & 0xf000) != 0x1000)
				break;
		} else {
			if ((surf & 0xf000) == 0x1000)
				igt_info("WORKING 0x%08x\n", surf);
		}

		memset(&fb, 0, sizeof(fb));
	}

	igt_info("hit the problem after %d attempts\n", j);
	if (!verify_fb(data, &fb))
		igt_warn("framebuffer is corrupted\n");
	igt_debug_wait_for_keypress("post");

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

#ifdef USE_SPRITE
	igt_plane_set_fb(sprite, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
#endif

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);
}

static data_t data;

igt_simple_main
{
	igt_skip_on_simulation();

	data.drm_fd = drm_open_any_master();

#ifdef USE_TILING
	data.tiling = LOCAL_I915_FORMAT_MOD_X_TILED;
#else
	data.tiling = LOCAL_DRM_FORMAT_MOD_NONE;
#endif

	kmstest_set_vt_graphics_mode();

	igt_require_pipe_crc();
	igt_display_init(&data.display, data.drm_fd);

	intel_register_access_init(intel_get_pci_device(), 0);

	for_each_connected_output(&data.display, data.output) {
		for_each_pipe(&data.display, data.pipe)
			test_plane(&data);
	}

	intel_register_access_fini();

	igt_display_fini(&data.display);
}
