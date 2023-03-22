// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "igt.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	int drm_fd;
	igt_display_t display;
	int pipe;
	igt_output_t *output;
	int cursor_size;
} data_t;

static void run_test(data_t *data)
{
	struct igt_fb plane_fb, cursor_fb;
	igt_plane_t *plane, *cursor;
	drmModeModeInfo *mode;

	igt_set_module_param_int(data->drm_fd, "enable_psr", 0);
	igt_set_module_param_int(data->drm_fd, "enable_fbc", 0);

	igt_output_set_pipe(data->output, data->pipe);

	plane  = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	cursor = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_CURSOR);

	mode = igt_output_get_mode(data->output);

	igt_create_fb(data->drm_fd,
		      mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888,
		      DRM_FORMAT_MOD_LINEAR, &plane_fb);

	igt_plane_set_fb(plane, &plane_fb);

	for (int cursor_size = 64; cursor_size <= 256; cursor_size <<= 1) {
		igt_create_pattern_fb(data->drm_fd,
				      cursor_size, cursor_size,
				      DRM_FORMAT_ARGB8888,
				      DRM_FORMAT_MOD_LINEAR, &cursor_fb);

		igt_plane_set_fb(cursor, &cursor_fb);
		igt_plane_set_position(cursor,
				       (mode->hdisplay - cursor_size) / 2,
				       (mode->vdisplay - cursor_size) / 2);

		igt_set_module_param_int(data->drm_fd, "cursor_max_size", cursor_size);

		for (int extra_dbuf = 0; extra_dbuf <= 512; extra_dbuf ? (extra_dbuf <<= 1) : (extra_dbuf++)) {
			igt_set_module_param_int(data->drm_fd, "cursor_ddb_extra", extra_dbuf);

			igt_info("test start: cursor size: %dx%d, extra dbuf size: %d",
				 cursor_size, cursor_size, extra_dbuf);
			igt_kmsg(KMSG_INFO "test start: cursor size: %dx%d, extra dbuf size: %d",
				 cursor_size, cursor_size, extra_dbuf);

			igt_plane_set_fb(cursor, &cursor_fb);
			igt_display_commit2(&data->display, COMMIT_ATOMIC);

			/* FIXME could sleep here instead of a fully automated test... */
			igt_debug_wait_for_keypress("measure");

			igt_plane_set_fb(cursor, NULL);
			igt_display_commit2(&data->display, COMMIT_ATOMIC);

			igt_info("test end: cursor size: %dx%d, extra dbuf size: %d",
				 cursor_size, cursor_size, extra_dbuf);
			igt_kmsg(KMSG_INFO "test end: cursor size: %dx%d, extra dbuf size: %d",
				 cursor_size, cursor_size, extra_dbuf);
		}

		igt_remove_fb(data->drm_fd, &cursor_fb);
	}

	igt_output_set_pipe(data->output, PIPE_NONE);
	igt_plane_set_fb(plane, NULL);
	igt_remove_fb(data->drm_fd, &plane_fb);
}

static void run_tests(data_t *data)
{
	for_each_pipe_with_single_output(&data->display, data->pipe, data->output) {
		run_test(data);
		break;
	}
}

static data_t data;

igt_simple_main
{
	data.drm_fd = drm_open_driver_master(DRIVER_ANY);

	kmstest_set_vt_graphics_mode();

	igt_require_pipe_crc(data.drm_fd);
	igt_display_require(&data.display, data.drm_fd);

	run_tests(&data);

	igt_display_fini(&data.display);
	close(data.drm_fd);
}
