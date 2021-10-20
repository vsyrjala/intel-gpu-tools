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
 * Authors: Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include "igt.h"
#include "igt_device.h"
#include "igt_psr.h"
#include "igt_sysfs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


IGT_TEST_DESCRIPTION("Test the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure.");

#define MAX_CONNECTORS 32

struct drm_info {
	int fd, debugfs_fd;
	struct igt_fb fb;
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
};

static void wait_user(const char *msg)
{
	igt_info("%s\n", msg);
	igt_debug_wait_for_keypress("fbt");
}

static bool fbc_supported_on_chipset(int device, int debugfs_fd)
{
	char buf[128];
	int ret;

	ret = igt_debugfs_simple_read(debugfs_fd, "i915_fbc_status",
				      buf, sizeof(buf));
	if (ret < 0)
		return false;

	return !strstr(buf, "FBC unsupported on this chipset\n");
}

static bool connector_can_fbc(drmModeConnectorPtr connector)
{
	return true;
}

static void fbc_print_status(int debugfs_fd)
{
	static char buf[128];

	igt_debugfs_simple_read(debugfs_fd, "i915_fbc_status", buf,
				sizeof(buf));
	igt_debug("FBC status: %s\n", buf);
}

static bool fbc_check_status(int debugfs_fd, bool enabled)
{
	char buf[128];

	igt_debugfs_simple_read(debugfs_fd, "i915_fbc_status", buf,
				sizeof(buf));
	if (enabled)
		return strstr(buf, "FBC enabled\n");
	else
		return strstr(buf, "FBC disabled");
}

static bool fbc_wait_until_enabled(int debugfs_fd)
{
	bool r = igt_wait(fbc_check_status(debugfs_fd, true), 5000, 1);
	fbc_print_status(debugfs_fd);
	return r;
}

static bool fbc_is_disabled(int debugfs_fd)
{
	bool r = fbc_check_status(debugfs_fd, false);

	fbc_print_status(debugfs_fd);
	return r;
}

static bool fbc_wait_until_disabled(int debugfs_fd)
{
	bool r = igt_wait(fbc_check_status(debugfs_fd, false), 5000, 1);
	fbc_print_status(debugfs_fd);
	return r;
}

static bool fbc_check_cursor_blinking(struct drm_info *drm)
{
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t crc[2];
	bool ret;
	int i;

	pipe_crc = igt_pipe_crc_new(drm->fd, PIPE_A, IGT_PIPE_CRC_SOURCE_AUTO);

	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_drain(pipe_crc);

	for (i = 0; i < 60; i++) {
		igt_pipe_crc_get_single(pipe_crc, &crc[i % 2]);

		if (i > 0) {
			ret = igt_find_crc_mismatch(&crc[0], &crc[1], NULL);
			if (ret)
				break;
		}
	}
	igt_pipe_crc_stop(pipe_crc);
	igt_pipe_crc_free(pipe_crc);

	return ret;
}

static bool fbc_wait_until_update(struct drm_info *drm)
{
	/*
	 * As now kernel enables FBC on linear surfaces on GEN9+, check if the
	 * fbcon cursor blinking is causing the FBC to uncompress the
	 * framebuffer.
	 *
	 * For older GENs FBC is still expected to be disabled as it still
	 * relies on a tiled and fenceable framebuffer to track modifications.
	 */
	if (AT_LEAST_GEN(intel_get_drm_devid(drm->fd), 9)) {
		if (!fbc_wait_until_enabled(drm->debugfs_fd))
			return false;

		return fbc_check_cursor_blinking(drm);
	} else {
		return fbc_wait_until_disabled(drm->debugfs_fd);
	}
}

typedef bool (*connector_possible_fn)(drmModeConnectorPtr connector);

static void set_mode_for_one_screen(struct drm_info *drm,
				    connector_possible_fn connector_possible)
{
	int i, rc;
	uint32_t crtc_id;
	drmModeModeInfoPtr mode;
	uint32_t buffer_id;
	drmModeConnectorPtr c = NULL;

	for (i = 0; i < drm->res->count_connectors; i++) {
		c = drm->connectors[i];

		if (c->connection == DRM_MODE_CONNECTED && c->count_modes &&
		    connector_possible(c)) {
			mode = &c->modes[0];
			break;
		}
	}
	igt_require_f(i < drm->res->count_connectors,
		      "No connector available\n");

	crtc_id = kmstest_find_crtc_for_connector(drm->fd, drm->res, c, 0);

	buffer_id = igt_create_fb(drm->fd, mode->hdisplay, mode->vdisplay,
				  DRM_FORMAT_XRGB8888,
				  I915_FORMAT_MOD_X_TILED, &drm->fb);
	igt_draw_fill_fb(drm->fd, &drm->fb, 0xFF);

	igt_info("Setting %dx%d mode for %s connector\n",
		 mode->hdisplay, mode->vdisplay,
		 kmstest_connector_type_str(c->connector_type));

	rc = drmModeSetCrtc(drm->fd, crtc_id, buffer_id, 0, 0,
			    &c->connector_id, 1, mode);
	igt_assert_eq(rc, 0);
}

static bool connector_can_psr(drmModeConnectorPtr connector)
{
	return (connector->connector_type == DRM_MODE_CONNECTOR_eDP);
}

static void psr_print_status(int debugfs_fd)
{
	static char buf[PSR_STATUS_MAX_LEN];

	igt_debugfs_simple_read(debugfs_fd, "i915_edp_psr_status", buf,
				sizeof(buf));
	igt_debug("PSR status: %s\n", buf);
}

static bool psr_wait_until_enabled(int debugfs_fd)
{
	bool r = psr_wait_entry(debugfs_fd, PSR_MODE_1);

	psr_print_status(debugfs_fd);
	return r;
}

static bool psr_is_disabled(int debugfs_fd)
{
	bool r = psr_disabled_check(debugfs_fd);

	psr_print_status(debugfs_fd);
	return r;
}

static bool psr_supported_on_chipset(int device, int debugfs_fd)
{
	return psr_sink_support(device, debugfs_fd, PSR_MODE_1);
}

static bool psr_wait_until_update(struct drm_info *drm)
{
	return psr_long_wait_update(drm->debugfs_fd, PSR_MODE_1);
}

static void disable_features(int device, int debugfs_fd)
{
	igt_set_module_param_int(device, "enable_fbc", 0);
	if (psr_sink_support(device, debugfs_fd, PSR_MODE_1))
		psr_disable(device, debugfs_fd);
}

static inline void fbc_modparam_enable(int device, int debugfs_fd)
{
	igt_set_module_param_int(device, "enable_fbc", 1);
}

static inline void psr_debugfs_enable(int device, int debugfs_fd)
{
	psr_enable(device, debugfs_fd, PSR_MODE_1);
}

static void fbc_skips_on_fbcon(int debugfs_fd)
{
	const char *reasons[] = {
		"incompatible mode",
		"mode too large for compression",
		"framebuffer not tiled or fenced",
		"pixel format is invalid",
		"rotation unsupported",
		"tiling unsupported",
		"framebuffer stride not supported",
		"per-pixel alpha blending is incompatible with FBC",
		"pixel rate is too big",
		"CFB requirements changed",
		"plane Y offset is misaligned",
		"plane height + offset is non-modulo of 4"
	};
	bool skip = false;
	char buf[128];
	int i;

	igt_debugfs_simple_read(debugfs_fd, "i915_fbc_status", buf, sizeof(buf));
	if (strstr(buf, "FBC enabled\n"))
		return;

	for (i = 0; skip == false && i < ARRAY_SIZE(reasons); i++)
		skip = strstr(buf, reasons[i]);

	if (skip)
		igt_skip("fbcon modeset is not compatible with FBC\n");
}

static void psr_skips_on_fbcon(int debugfs_fd)
{
	/*
	 * Unless fbcon enables interlaced mode all other PSR restrictions
	 * will be caught and skipped in supported_on_chipset() hook.
	 * As PSR don't expose in debugfs why it is not enabling for now
	 * not checking not even if it was not enabled because of interlaced
	 * mode, if some day it happens changes will be needed first.
	 */
}

struct feature {
	bool (*supported_on_chipset)(int device, int debugfs_fd);
	bool (*wait_until_enabled)(int debugfs_fd);
	bool (*is_disabled)(int debugfs_fd);
	bool (*wait_until_update)(struct drm_info *drm);
	bool (*connector_possible_fn)(drmModeConnectorPtr connector);
	void (*enable)(int device, int debugfs_fd);
	/* skip test if feature can't be enabled due fbcon modeset */
	void (*skips_on_fbcon)(int debugfs_fd);
} fbc = {
	.supported_on_chipset = fbc_supported_on_chipset,
	.wait_until_enabled = fbc_wait_until_enabled,
	.is_disabled = fbc_is_disabled,
	.wait_until_update = fbc_wait_until_update,
	.connector_possible_fn = connector_can_fbc,
	.enable = fbc_modparam_enable,
	.skips_on_fbcon = fbc_skips_on_fbcon,
}, psr = {
	.supported_on_chipset = psr_supported_on_chipset,
	.wait_until_enabled = psr_wait_until_enabled,
	.is_disabled = psr_is_disabled,
	.wait_until_update = psr_wait_until_update,
	.connector_possible_fn = connector_can_psr,
	.enable = psr_debugfs_enable,
	.skips_on_fbcon = psr_skips_on_fbcon,
};

static void restore_fbcon(struct drm_info *drm)
{
	kmstest_unset_all_crtcs(drm->fd, drm->res);
	igt_remove_fb(drm->fd, &drm->fb);
	igt_device_drop_master(drm->fd);
	kmstest_set_vt_text_mode();
}

static void subtest(struct drm_info *drm, struct feature *feature, bool suspend)
{
	igt_device_set_master(drm->fd);
	kmstest_set_vt_graphics_mode();

	igt_require(feature->supported_on_chipset(drm->fd, drm->debugfs_fd));

	disable_features(drm->fd, drm->debugfs_fd);
	feature->enable(drm->fd, drm->debugfs_fd);

	kmstest_unset_all_crtcs(drm->fd, drm->res);
	wait_user("Modes unset.");
	igt_assert(feature->is_disabled(drm->debugfs_fd));

	set_mode_for_one_screen(drm, feature->connector_possible_fn);
	wait_user("Screen set.");
	igt_assert(feature->wait_until_enabled(drm->debugfs_fd));

	if (suspend) {
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);
		sleep(5);
		igt_assert(feature->wait_until_enabled(drm->debugfs_fd));
	}

	restore_fbcon(drm);

	/* Wait for fbcon to restore itself. */
	sleep(3);

	wait_user("Back to fbcon.");
	feature->skips_on_fbcon(drm->debugfs_fd);
	igt_assert(feature->wait_until_update(drm));

	if (suspend) {
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);
		sleep(5);
		igt_assert(feature->wait_until_update(drm));
	}
}

static void setup_environment(struct drm_info *drm)
{
	int i;

	drm->fd = drm_open_driver_master(DRIVER_INTEL);
	igt_require(drm->fd >= 0);
	drm->debugfs_fd = igt_debugfs_dir(drm->fd);
	igt_require(drm->debugfs_fd >= 0);

	drm->res = drmModeGetResources(drm->fd);
	igt_require(drm->res);
	igt_assert(drm->res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm->res->count_connectors; i++)
		drm->connectors[i] = drmModeGetConnectorCurrent(drm->fd,
						drm->res->connectors[i]);

	/*
	 * igt_main()->igt_subtest_init_parse_opts()->common_init() disables the
	 * fbcon bind, so to test it is necessary enable it again
	 */
	bind_fbcon(true);
	fbcon_blink_enable(true);
}

static void teardown_environment(struct drm_info *drm)
{
	int i;

	for (i = 0; i < drm->res->count_connectors; i++)
		drmModeFreeConnector(drm->connectors[i]);

	drmModeFreeResources(drm->res);
	close(drm->debugfs_fd);
	close(drm->fd);
	kmstest_restore_vt_mode();
}

igt_main
{
	struct drm_info drm = { .fd = -1 };

	igt_fixture
		setup_environment(&drm);

	igt_describe("Test the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure with fbc enabled.");
	igt_subtest("fbc")
		subtest(&drm, &fbc, false);
	igt_describe("Test the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure with psr enabled.");
	igt_subtest("psr")
		subtest(&drm, &psr, false);
	igt_describe("Suspend test to validate  the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure with fbc enabled.");
	igt_subtest("fbc-suspend")
		subtest(&drm, &fbc, true);
	igt_describe("Suspend test to validate the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure with psr enabled.");
	igt_subtest("psr-suspend")
		subtest(&drm, &psr, true);

	igt_fixture
		teardown_environment(&drm);
}
