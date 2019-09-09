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

IGT_TEST_DESCRIPTION("Test LUT updates for tearing");

typedef struct {
	int drm_fd;
	igt_display_t *display;
	igt_output_t *output;
	igt_plane_t *plane;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t ref_crc;
	struct igt_fb fb[2];
	int duration;
	enum pipe pipe;
	bool is_atomic;
} data_t;

static void set_atomic_lut(data_t *data, int lut_size,
			   const struct drm_color_lut *lut)
{
	igt_pipe_t *pipe_obj = &data->display->pipes[data->pipe];

	igt_pipe_obj_replace_prop_blob(pipe_obj, IGT_CRTC_GAMMA_LUT, lut, lut_size * sizeof(lut[0]));
}

#define N_CRCS 20

static void check_crcs(data_t *data)
{
	igt_crc_t *crcs;
	int n_crcs;

	n_crcs = igt_pipe_crc_get_crcs(data->pipe_crc, N_CRCS, &crcs);
	igt_assert_lt(0, n_crcs);
	igt_assert_lt(n_crcs, N_CRCS);

	for (int i = 0; i < n_crcs; i++)
		igt_assert_crc_equal(&crcs[i], &data->ref_crc);
}

static void grab_ref_crc(data_t *data, int lut_size,
			 struct drm_color_lut *lut[2])
{
	igt_crc_t ref_crc[2];

	for (int i = 0; i < 2; i++) {
		set_atomic_lut(data, lut_size, lut[i]);
		igt_plane_set_fb(data->plane, &data->fb[i]);
		igt_display_commit2(data->display, data->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
		/* extra vblank wait to make sure our ref frame didn't tear */
		igt_wait_for_vblank(data->drm_fd, data->pipe);
		igt_pipe_crc_collect_crc(data->pipe_crc, &ref_crc[i]);
	}

	igt_assert_crc_equal(&ref_crc[0], &ref_crc[1]);
	data->ref_crc = ref_crc[0];
}

static struct drm_color_lut *create_lut(int lut_size,
					uint16_t red,
					uint16_t green,
					uint16_t blue)
{
	struct drm_color_lut *lut;

	lut = calloc(sizeof(lut[0]), lut_size);

	for (int i = 1; i < lut_size; i++) {
		lut[i].red = red;
		lut[i].green = green;
		lut[i].blue = blue;
	}

	return lut;
}

static unsigned long gettime_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void test_lut(data_t data[], int n_pipes, int duration_ms)
{
	igt_pipe_t *pipe_obj = &data[0].display->pipes[data->pipe];
	struct drm_color_lut *lut[2];
	unsigned long start;
	drmModeCrtc *crtc;
	int lut_size;

	crtc = drmModeGetCrtc(data[0].drm_fd, pipe_obj->crtc_id);
	lut_size = crtc->gamma_size;
	drmModeFreeCrtc(crtc);

	lut[0] = create_lut(lut_size, 0xff00, 0x0000, 0xff00);
	lut[1] = create_lut(lut_size, 0x0000, 0xff00, 0x0000);

	for (int i = 0; i < n_pipes; i++)
		grab_ref_crc(&data[i], lut_size, lut);

	for (int i = 0; i < n_pipes; i++)
		igt_pipe_crc_start(data[i].pipe_crc);

	start = gettime_ms();

	while ((gettime_ms() - start) < duration_ms) {
		for (int i = 0; i < n_pipes; i++) {
			igt_plane_set_fb(data[i].plane, &data[i].fb[0]);
			set_atomic_lut(&data[i], lut_size, lut[0]);
		}
		igt_display_commit2(data[0].display, data[0].is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

		for (int i = 0; i < n_pipes; i++) {
			igt_plane_set_fb(data[i].plane, &data[i].fb[1]);
			set_atomic_lut(&data[i], lut_size, lut[1]);
		}
		igt_display_commit2(data[0].display, data[0].is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

		for (int i = 0; i < n_pipes; i++)
			check_crcs(&data[i]);
	}

	for (int i = 0; i < n_pipes; i++)
		igt_pipe_crc_stop(data[i].pipe_crc);

	free(lut[1]);
	free(lut[0]);
}

static void create_fb(data_t *data,
		      int width, int height,
		      uint32_t format, uint64_t tiling,
		      double r, double g, double b,
		      struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(data->drm_fd, width, height,
			      format, tiling, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	//igt_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_paint_color_gradient_range(cr, 0, 0, width, height/2,
				       0, 0, 0, r, g, b);
	igt_paint_color_gradient_range(cr, 0, height/2, width, height/2,
				       r, g, b, 0, 0, 0);

	/*
	 * On i915 the LUT(s) are single buffered. The driver
	 * updates them well within the vblank, but still the
	 * hardware manages to process a small amount of pixels
	 * with the old LUT contents :( We just ignore those
	 * pixels here. The amount of wrong pixels seems to
	 * depend on the display timings somehow. Let's assume
	 * 128 pixels is enough to cover all the cases.
	 */
	if (is_i915_device(data->drm_fd))
	  igt_paint_color(cr, 0, 0, 128, 1, 0, 0, 0);

	igt_put_cairo_ctx(data->drm_fd, fb, cr);
}

static void prep_output(data_t *data)
{
	drmModeModeInfo *mode;

	mode = igt_output_get_mode(data->output);

	create_fb(data, mode->hdisplay, mode->vdisplay,
		  DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
		  0.0, 1.0, 0.0,
		  &data->fb[0]);
	create_fb(data, mode->hdisplay, mode->vdisplay,
		  DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
		  1.0, 0.0, 1.0,
		  &data->fb[1]);

	igt_output_set_pipe(data->output, data->pipe);
	data->plane = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(data->plane, &data->fb[0]);
	igt_display_commit2(data->display, data->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	data->pipe_crc = igt_pipe_crc_new_nonblock(data->drm_fd, data->pipe,
						   INTEL_PIPE_CRC_SOURCE_AUTO);
}

static void clean_pipe(data_t *data)
{
	igt_pipe_crc_free(data->pipe_crc);

	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_plane_set_fb(data->plane, NULL);
	igt_display_commit2(data->display, data->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	igt_remove_fb(data->drm_fd, &data->fb[1]);
	igt_remove_fb(data->drm_fd, &data->fb[0]);
}

static bool output_taken(data_t data[], int idx)
{
	for (int i = 0; i < idx; i++) {
		if (data[idx].output == data[i].output)
			return true;
	}
	return false;
}

static void
prep_pipe(data_t data[], int idx, int n_pipes)
{
	igt_require(data[idx].pipe < data[idx].display->n_pipes);
	igt_require(data[idx].display->pipes[data[idx].pipe].n_planes > 0);
	igt_display_require_output_on_pipe(data[idx].display, data[idx].pipe);

	for_each_valid_output_on_pipe(data[idx].display, data[idx].pipe, data[idx].output) {
		if (output_taken(data, idx))
			continue;

		prep_output(&data[idx]);
		return;
	}

	igt_skip("no suitable output found for pipe %s\n",
		 kmstest_pipe_name(data[idx].pipe));
}

__attribute__((format(printf, 3, 4)))
static char *snprintf_cont(char *buf, int *len,
			   const char *fmt, ...)
{
	va_list ap;
	int r;

	if (*len)
		*buf = '\0';

	va_start(ap, fmt);
	r = vsnprintf(buf, *len, fmt, ap);
	va_end(ap);

	if (r > 0 && r < *len) {
		buf += r;
		*len -= r;
	}

	return buf;
}

static void test_pipes(data_t data[], int n_pipes)
{
	igt_require(!data[0].is_atomic || data[0].display->is_atomic);

	for_each_pipe(data[0].display, data[0].pipe) {
		char str[128];
		char *ptr = str;
		int len = sizeof(str);

		ptr = snprintf_cont(ptr, &len, "pipe %s",
				    kmstest_pipe_name(data[0].pipe));
		for (int i = 1; i < n_pipes; i++) {
			data[i] = data[0];
			data[i].pipe = (data[i-1].pipe + 1) % data[i].display->n_pipes;

			ptr = snprintf_cont(ptr, &len, " + pipe %s",
					    kmstest_pipe_name(data[i].pipe));
		}

		igt_info("Testing %s\n", str);

		for (int i = 0; i < n_pipes; i++)
			prep_pipe(data, i, n_pipes);

		test_lut(data, n_pipes, data->duration * 1000);

		for (int i = 0; i < n_pipes; i++)
			clean_pipe(&data[i]);
	}
}

static int opt_handler(int opt, int opt_index, void *_data)
{
	data_t *data = _data;

	switch (opt) {
	case 'd':
		data->duration = atoi(optarg);
		break;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

static const struct option long_opts[] = {
	{ .name = "duration", .has_arg = true, .val = 'd', },
	{}
};

const char *help_str =
	"  --duration <seconds>\t\tSet the test duration (default: 2 seconds)\n";

static igt_display_t display;
static data_t data[3];

igt_main_args("", long_opts, help_str, opt_handler, &data[0])
{
	igt_skip_on_simulation();

	igt_fixture {
		data[0].display = &display;
		data[0].drm_fd = drm_open_driver_master(DRIVER_INTEL);

		kmstest_set_vt_graphics_mode();

		igt_display_require(data[0].display, data[0].drm_fd);
	}

	for (int n_pipes = 1; n_pipes < 4; n_pipes++) {
		igt_subtest_f("%dx-lut-atomic", n_pipes) {
			data[0].is_atomic = true;
			for_each_pipe(data[0].display, data[0].pipe)
				test_pipes(data, n_pipes);
		}
	}

	for (int n_pipes = 1; n_pipes < 4; n_pipes++) {
		igt_subtest_f("%dx-lut-legacy", n_pipes) {
			data[0].is_atomic = false;
			for_each_pipe(data[0].display, data[0].pipe)
				test_pipes(data, n_pipes);
		}
	}

	igt_fixture {
		igt_display_fini(data[0].display);
	}
}
