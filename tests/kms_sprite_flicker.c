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

//#define USE_SPRITE

typedef struct {
	int drm_fd;
	igt_display_t display;
	igt_output_t *output;
	enum pipe pipe;
	igt_pipe_crc_t *pipe_crc;
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

	gem_set_domain(data->drm_fd, fb->gem_handle, I915_GEM_DOMAIN_CPU, 0);

	ptr = gem_mmap__cpu(data->drm_fd, fb->gem_handle, 0, fb->size, PROT_READ);

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
	igt_crc_t crc, crc_ref;
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
#ifndef USE_SPRITE
	sprite = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
#else
	sprite = igt_output_get_plane(output, IGT_PLANE_2);
#endif
	mode = igt_output_get_mode(output);

	igt_info("mode %dx%d\n", mode->hdisplay, mode->vdisplay);

	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    LOCAL_DRM_FORMAT_MOD_NONE,
			    0.0, 0.0, 1.0,
			    &fb);

	igt_plane_set_fb(primary, &fb);
	igt_display_commit(display);

	igt_debug_wait_for_keypress("pre");

	igt_pipe_crc_collect_crc(data->pipe_crc, &crc_ref);

#ifndef USE_SPRITE
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	igt_remove_fb(data->drm_fd, &fb);
#endif
	memset(&fb, 0, sizeof(fb));

	for (;;) {
		int i;

		j++;

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888,
				    LOCAL_DRM_FORMAT_MOD_NONE,
				    0.0, 0.0, 1.0,
				    &fb);

		igt_plane_set_fb(sprite, &fb);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_debug_wait_for_keypress("mid");

		for (i = 0; i < 5; i++) {
			usleep(250000);
			igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
			if (!crc_equal(&crc, &crc_ref))
				break;
		}
		if (i != 5)
			break;

		memset(&fb, 0, sizeof(fb));
	}

	igt_info("hit the problem after %d attempts\n", j);
	if (!verify_fb(data, &fb))
		igt_warn("framebuffer is corrupted\n");
	igt_debug_wait_for_keypress("post");

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_plane_set_fb(sprite, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);
}

static data_t data;

igt_simple_main
{
	igt_skip_on_simulation();

	data.drm_fd = drm_open_any_master();

	kmstest_set_vt_graphics_mode();

	igt_require_pipe_crc();
	igt_display_init(&data.display, data.drm_fd);

	for_each_connected_output(&data.display, data.output) {
		for_each_pipe(&data.display, data.pipe)
			test_plane(&data);
	}

	igt_display_fini(&data.display);
}
