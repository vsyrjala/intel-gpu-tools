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

IGT_TEST_DESCRIPTION("Frob planes");

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_output_t *output;
	enum pipe pipe;
	uint32_t devid;
	igt_pipe_crc_t *pipe_crc;
} data_t;

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;

	if (data->pipe_crc) {
		igt_pipe_crc_free(data->pipe_crc);
		data->pipe_crc = NULL;
	}

	igt_display_reset(display);

	igt_remove_fb(data->drm_fd, &data->fb);
	igt_remove_fb(data->drm_fd, &data->fb);
}

static void prepare_crtc(data_t *data)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	cleanup_crtc(data);

	/* select the pipe we want to use */
	igt_output_set_pipe(data->output, data->pipe);

	mode = igt_output_get_mode(data->output);
	igt_create_pattern_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			      DRM_FORMAT_XRGB8888,
//			      LOCAL_DRM_FORMAT_MOD_NONE,
//			      I915_FORMAT_MOD_X_TILED,
			      I915_FORMAT_MOD_Y_TILED,
			      &data->fb);

	primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, &data->fb);

	igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	data->pipe_crc = igt_pipe_crc_new_nonblock(data->drm_fd, data->pipe,
						   INTEL_PIPE_CRC_SOURCE_AUTO);

}

static void test_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary, *sprite;
	/*
	 * Ugh. In addition to the expected four CRCs, we seem to have
	 * two extra ones making an appearance occasionally. I suspect
	 * these happen when a specific state transition happens. Similar
	 * thing was observered on IVB earlier where no planes<->any planes
	 * transitions would produce a single unexpected CRC. So far
	 * these frames don't seem visually corrupted, but of course it's
	 * quite impossible to be sure when measuring by eye.
	 *
	 * TODO: analyze when these actually happen.
	 */
	igt_crc_t ref_crc[6] = {
		[4] = { .has_valid_frame = true, .n_words = 5, .crc[0] = 0xcaf163ef, },
		[5] = { .has_valid_frame = true, .n_words = 5, .crc[0] = 0x2b52e38e, },
	};
	unsigned int i;

	prepare_crtc(data);

	primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	sprite = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_OVERLAY);

	for (i = 0; i < 4; i++) {
		primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
		if (i & 1) {
			igt_plane_set_fb(primary, &data->fb);
			igt_fb_set_size(&data->fb, primary, data->fb.width, data->fb.height);
			igt_plane_set_size(primary, data->fb.width, data->fb.height);
		} else {
			igt_plane_set_fb(primary, NULL);
		}

		if (i & 2) {
			igt_plane_set_fb(sprite, &data->fb);
			igt_fb_set_size(&data->fb, sprite, data->fb.width/2, data->fb.height/2);
			igt_plane_set_size(sprite, data->fb.width/2, data->fb.height/2);
			igt_plane_set_position(sprite, data->fb.width/2, data->fb.height/2);
		} else {
			igt_plane_set_fb(sprite, NULL);
		}

		igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_UNIVERSAL);

		igt_pipe_crc_collect_crc(data->pipe_crc, &ref_crc[i]);

		if (0) {
			char *s = igt_crc_to_string(&ref_crc[i]);
			printf("ref %s\n", s);
			free(s);
		}
	}

	igt_pipe_crc_start(data->pipe_crc);
	for (i = 0; true; i++) {
		igt_crc_t *crc;
		int n;

		n = igt_pipe_crc_get_crcs(data->pipe_crc, 10, &crc);
		if (n == 0)
			continue;

		while (n--) {
			int j;

			for (j = 0; j < 6; j++) {
				if (igt_check_crc_equal(&crc[n], &ref_crc[j]))
					break;
			}
			if (0) {
				char *s = igt_crc_to_string(&crc[n]);
				printf("crc[%d] %s\n", n, s);
				free(s);
			}
			igt_assert_neq(j, 6);
		}
		if (0)
			printf("'\n");
		free(crc);
		igt_debug_wait_for_keypress("pln");

		primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
		if (i & 1) {
			igt_plane_set_fb(primary, &data->fb);
			igt_fb_set_size(&data->fb, primary, data->fb.width, data->fb.height);
			igt_plane_set_size(primary, data->fb.width, data->fb.height);
		} else {
			igt_plane_set_fb(primary, NULL);
		}

		if (i & 2) {
			igt_plane_set_fb(sprite, &data->fb);
			igt_fb_set_size(&data->fb, sprite, data->fb.width/2, data->fb.height/2);
			igt_plane_set_size(sprite, data->fb.width/2, data->fb.height/2);
			igt_plane_set_position(sprite, data->fb.width/2, data->fb.height/2);
		} else {
			igt_plane_set_fb(sprite, NULL);
		}

		igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_UNIVERSAL);
	}
	igt_pipe_crc_stop(data->pipe_crc);

	cleanup_crtc(data);
}

static data_t data;

igt_simple_main
{
	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);

		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_display_init(&data.display, data.drm_fd);
	}

	for_each_pipe_static(data.pipe) {
		igt_display_require_output_on_pipe(&data.display, data.pipe);
		data.output = igt_get_single_output_for_pipe(&data.display, data.pipe);
		test_crtc(&data);
	}

	igt_fixture
		igt_display_fini(&data.display);
}
