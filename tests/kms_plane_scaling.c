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

IGT_TEST_DESCRIPTION("Test display plane scaling");

typedef struct {
	uint32_t devid;
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb[4];
	bool extended;
} data_t;

static void cleanup_fbs(data_t *data)
{
	for (int i = 0; i < ARRAY_SIZE(data->fb); i++)
		igt_remove_fb(data->drm_fd, &data->fb[i]);
}

static void cleanup_crtc(data_t *data)
{
	igt_display_reset(&data->display);

	cleanup_fbs(data);
}

static void check_scaling_pipe_plane_rot(data_t *d, igt_plane_t *plane,
					 uint32_t pixel_format,
					 uint64_t modifier,
					 int width, int height,
					 bool is_upscale,
					 enum pipe pipe,
					 igt_output_t *output,
					 igt_rotation_t rot)
{
	igt_display_t *display = &d->display;
	drmModeModeInfo *mode;
	int commit_ret;
	int w, h;

	mode = igt_output_get_mode(output);

	if (is_upscale) {
		w = width;
		h = height;
	} else {
		w = mode->hdisplay;
		h = mode->vdisplay;
	}

	/*
	 * guarantee even value width/height to avoid fractional
	 * uv component in chroma subsampling for yuv 4:2:0 formats
	 * */
	w = ALIGN(w, 2);
	h = ALIGN(h, 2);

	igt_create_color_fb(display->drm_fd, w, h,
			    pixel_format, modifier, 0.0, 1.0, 0.0, &d->fb[0]);

	igt_plane_set_fb(plane, &d->fb[0]);
	igt_fb_set_position(&d->fb[0], plane, 0, 0);
	igt_fb_set_size(&d->fb[0], plane, w, h);
	igt_plane_set_position(plane, 0, 0);

	if (is_upscale)
		igt_plane_set_size(plane, mode->hdisplay, mode->vdisplay);
	else
		igt_plane_set_size(plane, width, height);

	igt_plane_set_rotation(plane, rot);
	commit_ret = igt_display_try_commit2(display, COMMIT_ATOMIC);

	igt_plane_set_fb(plane, NULL);
	igt_plane_set_position(plane, 0, 0);

	igt_skip_on_f(commit_ret == -ERANGE || commit_ret == -EINVAL,
		      "Unsupported scaling factor with fb size %dx%d\n",
		      w, h);
	igt_assert_eq(commit_ret, 0);
}

static const igt_rotation_t rotations[] = {
	IGT_ROTATION_0,
	IGT_ROTATION_90,
	IGT_ROTATION_180,
	IGT_ROTATION_270,
};

static bool can_rotate(data_t *d, unsigned format, uint64_t modifier,
		       igt_rotation_t rot)
{
	if (!is_i915_device(d->drm_fd))
		return true;

	switch (format) {
	case DRM_FORMAT_RGB565:
		if (intel_display_ver(d->devid) >= 11)
			break;
		/* fall through */
	case DRM_FORMAT_C8:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
		return false;
	default:
		break;
	}

	return true;
}

static bool can_scale(data_t *d, unsigned format)
{
	if (!is_i915_device(d->drm_fd))
		return true;

	switch (format) {
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		if (intel_display_ver(d->devid) >= 11)
			return true;
		/* fall through */
	case DRM_FORMAT_C8:
		return false;
	default:
		return true;
	}
}

static bool test_format(data_t *data,
			struct igt_vec *tested_formats,
			uint32_t format)
{
	if (!igt_fb_supported_format(format))
		return false;

	if (!is_i915_device(data->drm_fd) ||
	    data->extended)
		return true;

	format = igt_reduce_format(format);

	/* only test each format "class" once */
	if (igt_vec_index(tested_formats, &format) >= 0)
		return false;

	igt_vec_push(tested_formats, &format);

	return true;
}

static bool test_pipe_iteration(data_t *data, enum pipe pipe, int iteration)
{
	if (!is_i915_device(data->drm_fd) ||
	    data->extended)
		return true;

	if ((pipe > PIPE_B) && (iteration >= 2))
		return false;

	return true;
}

static void test_scaler_with_rotation_pipe(data_t *d,
					   int width, int height,
					   bool is_upscale,
					   enum pipe pipe,
					   igt_output_t *output)
{
	igt_display_t *display = &d->display;
	uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
	igt_plane_t *plane;

	cleanup_crtc(d);

	igt_output_set_pipe(output, pipe);

	for_each_plane_on_pipe(display, pipe, plane) {
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		for (int i = 0; i < ARRAY_SIZE(rotations); i++) {
			igt_rotation_t rot = rotations[i];
			struct igt_vec tested_formats;

			igt_vec_init(&tested_formats, sizeof(uint32_t));

			for (int j = 0; j < plane->drm_plane->count_formats; j++) {
				unsigned format = plane->drm_plane->formats[j];

				if (!test_pipe_iteration(d, pipe, j))
					continue;

				if (test_format(d, &tested_formats, format) &&
				    igt_plane_has_format_mod(plane, format, modifier) &&
				    igt_plane_has_rotation(plane, rot) &&
				    can_rotate(d, format, modifier, rot) &&
				    can_scale(d, format))
					check_scaling_pipe_plane_rot(d, plane,
								     format, modifier,
								     width, height,
								     is_upscale,
								     pipe, output,
								     rot);
			}

			igt_vec_fini(&tested_formats);
		}
	}
}

static const uint64_t modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	I915_FORMAT_MOD_X_TILED,
	I915_FORMAT_MOD_Y_TILED,
	I915_FORMAT_MOD_Yf_TILED
};

static void test_scaler_with_pixel_format_pipe(data_t *d, int width, int height, bool is_upscale,
					       enum pipe pipe, igt_output_t *output)
{
	igt_display_t *display = &d->display;
	igt_plane_t *plane;

	cleanup_crtc(d);

	igt_output_set_pipe(output, pipe);

	for_each_plane_on_pipe(display, pipe, plane) {
		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		for (int i = 0; i < ARRAY_SIZE(modifiers); i++) {
			uint64_t modifier = modifiers[i];
			struct igt_vec tested_formats;

			igt_vec_init(&tested_formats, sizeof(uint32_t));

			for (int j = 0; j < plane->drm_plane->count_formats; j++) {
				uint32_t format = plane->drm_plane->formats[j];

				if (!test_pipe_iteration(d, pipe, j))
					continue;

				if (test_format(d, &tested_formats, format) &&
				    igt_plane_has_format_mod(plane, format, modifier) &&
				    can_scale(d, format))
					check_scaling_pipe_plane_rot(d, plane,
								     format, modifier,
								     width, height,
								     is_upscale,
								     pipe, output, IGT_ROTATION_0);
			}

			igt_vec_fini(&tested_formats);
		}
	}
}

static void find_connected_pipe(igt_display_t *display, bool second, enum pipe *pipe, igt_output_t **output)
{
	enum pipe first = PIPE_NONE;
	igt_output_t *first_output = NULL;
	bool found = false;

	for_each_pipe_with_valid_output(display, *pipe, *output) {
		if (first == *pipe || *output == first_output)
			continue;

		if (second) {
			first = *pipe;
			first_output = *output;
			second = false;
			continue;
		}

		return;
	}

	if (first_output)
		igt_require_f(found, "No second valid output found\n");
	else
		igt_require_f(found, "No valid outputs found\n");
}

static void test_scaler_with_multi_pipe_plane(data_t *d)
{
	igt_display_t *display = &d->display;
	igt_output_t *output1, *output2;
	drmModeModeInfo *mode1, *mode2;
	igt_plane_t *plane[4];
	enum pipe pipe1, pipe2;
	int ret1, ret2;

	cleanup_crtc(d);

	find_connected_pipe(display, false, &pipe1, &output1);
	find_connected_pipe(display, true, &pipe2, &output2);

	igt_skip_on(!output1 || !output2);

	igt_output_set_pipe(output1, pipe1);
	igt_output_set_pipe(output2, pipe2);

	plane[0] = igt_output_get_plane(output1, 0);
	igt_require(plane[0]);
	plane[1] = igt_output_get_plane(output1, 0);
	igt_require(plane[1]);
	plane[2] = igt_output_get_plane(output2, 1);
	igt_require(plane[2]);
	plane[3] = igt_output_get_plane(output2, 1);
	igt_require(plane[3]);

	igt_create_pattern_fb(d->drm_fd, 600, 600,
			      DRM_FORMAT_XRGB8888,
			      I915_TILING_NONE, &d->fb[0]);
	igt_create_pattern_fb(d->drm_fd, 500, 500,
			      DRM_FORMAT_XRGB8888,
			      I915_TILING_NONE, &d->fb[1]);
	igt_create_pattern_fb(d->drm_fd, 700, 700,
			      DRM_FORMAT_XRGB8888,
			      I915_TILING_NONE, &d->fb[2]);
	igt_create_pattern_fb(d->drm_fd, 400, 400,
			      DRM_FORMAT_XRGB8888,
			      I915_TILING_NONE, &d->fb[3]);

	igt_plane_set_fb(plane[0], &d->fb[0]);
	igt_plane_set_fb(plane[1], &d->fb[1]);
	igt_plane_set_fb(plane[2], &d->fb[2]);
	igt_plane_set_fb(plane[3], &d->fb[3]);

	if (igt_display_try_commit_atomic(display,
				DRM_MODE_ATOMIC_TEST_ONLY |
				DRM_MODE_ATOMIC_ALLOW_MODESET,
				NULL) != 0) {
		bool found = igt_override_all_active_output_modes_to_fit_bw(display);
		igt_require_f(found, "No valid mode combo found.\n");
	}

	igt_display_commit2(display, COMMIT_ATOMIC);

	mode1 = igt_output_get_mode(output1);
	mode2 = igt_output_get_mode(output2);

	/* upscaling primary */
	igt_plane_set_size(plane[0], mode1->hdisplay, mode1->vdisplay);
	igt_plane_set_size(plane[2], mode2->hdisplay, mode2->vdisplay);
	ret1 = igt_display_try_commit2(display, COMMIT_ATOMIC);

	/* upscaling sprites */
	igt_plane_set_size(plane[1], mode1->hdisplay, mode1->vdisplay);
	igt_plane_set_size(plane[3], mode2->hdisplay, mode2->vdisplay);
	ret2 = igt_display_try_commit2(display, COMMIT_ATOMIC);

	igt_plane_set_fb(plane[0], NULL);
	igt_plane_set_fb(plane[1], NULL);
	igt_plane_set_fb(plane[2], NULL);
	igt_plane_set_fb(plane[3], NULL);

	igt_skip_on_f(ret1 == -ERANGE || ret1 == -EINVAL ||
		      ret2 == -ERANGE || ret1 == -EINVAL,
		      "Scaling op is not supported by driver\n");
	igt_assert_eq(ret1 && ret2, 0);
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
	enum pipe pipe;

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_ANY);
		igt_display_require(&data.display, data.drm_fd);
		data.devid = is_i915_device(data.drm_fd) ?
			intel_get_drm_devid(data.drm_fd) : 0;
		igt_require(data.display.is_atomic);
	}

	igt_subtest_group {
		igt_output_t *output;

		igt_describe("Tests upscaling with pixel formats, from 20x20 fb.");
		igt_subtest_with_dynamic("upscale-with-pixel-format-20x20") {
			for_each_pipe_with_single_output(&data.display, pipe, output)
				igt_dynamic_f("pipe-%s-%s-upscale-with-pixel-format", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, 20, 20, true, pipe, output);
		}

		igt_describe("Tests upscaling with pixel formats for 0.25 scaling factor.");
		igt_subtest_with_dynamic("upscale-with-pixel-format-factor-0-25") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-upscale-with-pixel-format", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, 0.25 * mode->hdisplay,
							0.25 * mode->vdisplay, true, pipe, output);
			}
		}

		igt_describe("Tests downscaling with pixel formats for 0.25 scaling factor.");
		igt_subtest_with_dynamic("downscale-with-pixel-format-factor-0-25") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-downscale-with-pixel-format", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, 0.25 * mode->hdisplay,
							0.25 * mode->vdisplay, false, pipe, output);
			}
		}

		igt_describe("Tests downscaling with pixel formats for 0.5 scaling factor.");
		igt_subtest_with_dynamic("downscale-with-pixel-format-factor-0-5") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-downscale-with-pixel-format", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, 0.5 * mode->hdisplay,
							0.5 * mode->vdisplay, false, pipe, output);
			}
		}

		igt_describe("Tests scaling with pixel formats, unity scaling.");
		igt_subtest_with_dynamic("scaler-with-pixel-format-unity-scaling") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-scaler-with-pixel-format", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, mode->hdisplay,
							mode->vdisplay, true, pipe, output);
			}
		}

		igt_describe("Tests upscaling with tiling rotation, from 20x20 fb.");
		igt_subtest_with_dynamic("upscale-with-rotation-20x20") {
			for_each_pipe_with_single_output(&data.display, pipe, output)
				igt_dynamic_f("pipe-%s-%s-upscale-with-rotation", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_rotation_pipe(&data, 20, 20, true, pipe, output);
		}

		igt_describe("Tests upscaling with tiling rotation for 0.25 scaling factor.");
		igt_subtest_with_dynamic("upscale-with-rotation-factor-0-25") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-upscale-with-rotation", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_rotation_pipe(&data, 0.25 * mode->hdisplay,
							0.25 * mode->vdisplay, true, pipe, output);
			}
		}

		igt_describe("Tests downscaling with tiling rotation for 0.25 scaling factor.");
		igt_subtest_with_dynamic("downscale-with-rotation-factor-0-25") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-downscale-with-rotation", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_rotation_pipe(&data, 0.25 * mode->hdisplay,
							0.25 * mode->vdisplay, false, pipe, output);
			}
		}

		igt_describe("Tests downscaling with tiling rotation for 0.5 scaling factor.");
		igt_subtest_with_dynamic("downscale-with-rotation-factor-0-5") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-downscale-with-rotation", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_rotation_pipe(&data, 0.5 * mode->hdisplay,
							0.5 * mode->vdisplay, false, pipe, output);
			}
		}

		igt_describe("Tests scaling with tiling rotation, unity scaling.");
		igt_subtest_with_dynamic("scaler-with-rotation-unity-scaling") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-scaler-with-rotation", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_rotation_pipe(&data, mode->hdisplay,
							mode->vdisplay, true, pipe, output);
			}
		}

		igt_describe("Tests scaling with clipping and clamping.");
		igt_subtest_with_dynamic("scaler-with-clipping-clamping") {
			for_each_pipe_with_single_output(&data.display, pipe, output) {
				drmModeModeInfo *mode;

				mode = igt_output_get_mode(output);

				igt_dynamic_f("pipe-%s-%s-scaler-with-clipping-clamping", kmstest_pipe_name(pipe), igt_output_name(output))
					test_scaler_with_pixel_format_pipe(&data, mode->hdisplay + 100,
							mode->vdisplay + 100, false, pipe, output);
			}
		}
	}

	igt_describe("Tests scaling with multi-pipe scenario.");
	igt_subtest_f("2x-scaler-multi-pipe")
		test_scaler_with_multi_pipe_plane(&data);

	igt_fixture
		igt_display_fini(&data.display);
}
