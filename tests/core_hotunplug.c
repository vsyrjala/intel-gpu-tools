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

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "i915/gem.h"
#include "igt.h"
#include "igt_device_scan.h"
#include "igt_kmod.h"
#include "igt_sysfs.h"

IGT_TEST_DESCRIPTION("Examine behavior of a driver on device hot unplug");

struct hotunplug {
	struct {
		int drm;
		int sysfs_dev;
		int sysfs_bus;
		int sysfs_drv;
	} fd;	/* >= 0: valid fd, == -1: closed, < -1: close failed */
	const char *dev_bus_addr;
	const char *failure;
};

/* Helpers */

/**
 * Subtests must be able to close examined devices completely.  Don't
 * use drm_open_driver() since in case of an i915 device it opens it
 * twice and keeps a second file descriptor open for exit handler use.
 */
static int local_drm_open_driver(const char *when, const char *why)
{
	int fd_drm;

	igt_debug("%sopening device%s\n", when, why);

	fd_drm = __drm_open_driver(DRIVER_ANY);
	igt_assert_fd(fd_drm);

	return fd_drm;
}

static int local_close(int fd, const char *warning)
{
	errno = 0;
	if (igt_warn_on_f(close(fd), "%s\n", warning))
		return -errno;	/* (never -1) */

	return -1;	/* success - return 'closed' */
}

static int close_device(int fd_drm, const char *when, const char *which)
{
	igt_debug("%sclosing %sdevice instance\n", when, which);
	return local_close(fd_drm, "Device close failed");
}

static int close_sysfs(int fd_sysfs_dev)
{
	return local_close(fd_sysfs_dev, "Device sysfs node close failed");
}

static void prepare(struct hotunplug *priv)
{
	const char *filter = igt_device_filter_get(0), *sysfs_path;

	igt_assert(filter);

	priv->dev_bus_addr = strrchr(filter, '/');
	igt_assert(priv->dev_bus_addr++);

	sysfs_path = strchr(filter, ':');
	igt_assert(sysfs_path++);

	igt_assert_eq(priv->fd.sysfs_dev, -1);
	priv->fd.sysfs_dev = open(sysfs_path, O_DIRECTORY);
	igt_assert_fd(priv->fd.sysfs_dev);

	priv->fd.sysfs_drv = openat(priv->fd.sysfs_dev, "driver", O_DIRECTORY);
	igt_assert_fd(priv->fd.sysfs_drv);

	priv->fd.sysfs_bus = openat(priv->fd.sysfs_dev, "subsystem/devices",
				    O_DIRECTORY);
	igt_assert_fd(priv->fd.sysfs_bus);

	priv->fd.sysfs_dev = close_sysfs(priv->fd.sysfs_dev);
}

/* Unbind the driver from the device */
static void driver_unbind(struct hotunplug *priv, const char *prefix)
{
	igt_debug("%sunbinding the driver from the device\n", prefix);

	priv->failure = "Driver unbind timeout!";
	igt_set_timeout(60, priv->failure);
	igt_sysfs_set(priv->fd.sysfs_drv, "unbind", priv->dev_bus_addr);
	igt_reset_timeout();
	priv->failure = NULL;
}

/* Re-bind the driver to the device */
static void driver_bind(struct hotunplug *priv)
{
	igt_debug("rebinding the driver to the device\n");

	priv->failure = "Driver re-bind timeout!";
	igt_set_timeout(60, priv->failure);
	igt_sysfs_set(priv->fd.sysfs_drv, "bind", priv->dev_bus_addr);
	igt_reset_timeout();
	priv->failure = NULL;
}

/* Remove (virtually unplug) the device from its bus */
static void device_unplug(struct hotunplug *priv, const char *prefix)
{
	igt_require(priv->fd.sysfs_dev == -1);

	priv->fd.sysfs_dev = openat(priv->fd.sysfs_bus, priv->dev_bus_addr,
				    O_DIRECTORY);
	igt_assert_fd(priv->fd.sysfs_dev);

	igt_debug("%sunplugging the device\n", prefix);

	priv->failure = "Device unplug timeout!";
	igt_set_timeout(60, priv->failure);
	igt_sysfs_set(priv->fd.sysfs_dev, "remove", "1");
	igt_reset_timeout();
	priv->failure = NULL;

	priv->fd.sysfs_dev = close_sysfs(priv->fd.sysfs_dev);
}

/* Re-discover the device by rescanning its bus */
static void bus_rescan(struct hotunplug *priv)
{
	igt_debug("rediscovering the device\n");

	priv->failure = "Bus rescan timeout!";
	igt_set_timeout(60, priv->failure);
	igt_sysfs_set(priv->fd.sysfs_bus, "../rescan", "1");
	igt_reset_timeout();
	priv->failure = NULL;
}

static void healthcheck(struct hotunplug *priv)
{
	/* preserve potentially dirty device status stored in priv->fd.drm */
	bool closed = priv->fd.drm == -1;
	int fd_drm;

	/* device name may have changed, rebuild IGT device list */
	igt_devices_scan(true);

	priv->failure = "Device reopen failure!";
	fd_drm = local_drm_open_driver("re", " for health check");
	if (closed)	/* store fd for post_healthcheck if not dirty */
		priv->fd.drm = fd_drm;
	priv->failure = NULL;

	if (is_i915_device(fd_drm)) {
		priv->failure = "GEM failure";
		igt_require_gem(fd_drm);
		priv->failure = NULL;
	}

	fd_drm = close_device(fd_drm, "", "health checked ");
	if (closed || fd_drm < -1)	/* update status for post_healthcheck */
		priv->fd.drm = fd_drm;
}

static void post_healthcheck(struct hotunplug *priv)
{
	igt_abort_on_f(priv->failure, "%s\n", priv->failure);

	igt_require(priv->fd.drm == -1);
}

static void set_filter_from_device(int fd)
{
	const char *filter_type = "sys:";
	char filter[strlen(filter_type) + PATH_MAX + 1];
	char *dst = stpcpy(filter, filter_type);
	char path[PATH_MAX + 1];

	igt_assert(igt_sysfs_path(fd, path, PATH_MAX));
	strncat(path, "/device", PATH_MAX - strlen(path));
	igt_assert(realpath(path, dst));

	igt_device_filter_free_all();
	igt_assert_eq(igt_device_filter_add(filter), 1);
}

/* Subtests */

static void unbind_rebind(struct hotunplug *priv)
{
	igt_assert_eq(priv->fd.drm, -1);

	driver_unbind(priv, "");

	driver_bind(priv);

	healthcheck(priv);
}

static void unplug_rescan(struct hotunplug *priv)
{
	igt_assert_eq(priv->fd.drm, -1);

	device_unplug(priv, "");

	bus_rescan(priv);

	healthcheck(priv);
}

static void hotunbind_lateclose(struct hotunplug *priv)
{
	igt_assert_eq(priv->fd.drm, -1);
	priv->fd.drm = local_drm_open_driver("", " for hot unbind");

	driver_unbind(priv, "hot ");

	driver_bind(priv);

	priv->fd.drm = close_device(priv->fd.drm, "late ", "unbound ");

	healthcheck(priv);
}

static void hotunplug_lateclose(struct hotunplug *priv)
{
	igt_assert_eq(priv->fd.drm, -1);
	priv->fd.drm = local_drm_open_driver("", " for hot unplug");

	device_unplug(priv, "hot ");

	bus_rescan(priv);

	priv->fd.drm = close_device(priv->fd.drm, "late ", "removed ");

	healthcheck(priv);
}

/* Main */

igt_main
{
	struct hotunplug priv = {
		.fd		= { .drm = -1, .sysfs_dev = -1, },
		.failure	= NULL,
	};

	igt_fixture {
		int fd_drm;

		fd_drm = __drm_open_driver(DRIVER_ANY);
		igt_skip_on_f(fd_drm < 0, "No known DRM device found\n");

		if (is_i915_device(fd_drm)) {
			gem_quiescent_gpu(fd_drm);
			igt_require_gem(fd_drm);
		}

		/* Make sure subtests always reopen the same device */
		set_filter_from_device(fd_drm);

		igt_assert_eq(close_device(fd_drm, "", "selected "), -1);

		prepare(&priv);
	}

	igt_describe("Check if the driver can be cleanly unbound from a device believed to be closed");
	igt_subtest("unbind-rebind")
		unbind_rebind(&priv);

	igt_fixture
		post_healthcheck(&priv);

	igt_describe("Check if a device believed to be closed can be cleanly unplugged");
	igt_subtest("unplug-rescan")
		unplug_rescan(&priv);

	igt_fixture
		post_healthcheck(&priv);

	igt_describe("Check if the driver can be cleanly unbound from a still open device, then released");
	igt_subtest("hotunbind-lateclose")
		hotunbind_lateclose(&priv);

	igt_fixture
		post_healthcheck(&priv);

	igt_describe("Check if a still open device can be cleanly unplugged, then released");
	igt_subtest("hotunplug-lateclose")
		hotunplug_lateclose(&priv);

	igt_fixture {
		post_healthcheck(&priv);

		close(priv.fd.sysfs_bus);
		close(priv.fd.sysfs_drv);
	}
}
