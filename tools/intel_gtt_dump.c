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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <cairo.h>
#include "intel_io.h"
#include "intel_reg.h"
#include "intel_chipset.h"
#include "intel_batchbuffer.h"
#include "igt_aux.h"
#include "drmtest.h"

struct data {
	uint32_t offset;
	unsigned int stride;
	unsigned int x, y;
	unsigned int width, height;
	unsigned int cpp;

	unsigned int tiling;
	unsigned int tile_size, tile_width, tile_height;

	struct pci_device *pci_dev;
	uint32_t devid;
	int gen;

	int pipe, plane;

	int devmem_fd;
	void *gtt;
	void *image;
};

#define SNB_GMCH_CTRL           0x50
#define    SNB_GMCH_GGMS_SHIFT  8 /* GTT Graphics Memory Size */
#define    SNB_GMCH_GGMS_MASK   0x3
#define    SNB_GMCH_GMS_SHIFT   3 /* Graphics Mode Select */
#define    SNB_GMCH_GMS_MASK    0x1f
#define    BDW_GMCH_GGMS_SHIFT  6
#define    BDW_GMCH_GGMS_MASK   0x3
#define    BDW_GMCH_GMS_SHIFT   8
#define    BDW_GMCH_GMS_MASK    0xff

static unsigned int gen6_gtt_size(uint16_t snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static unsigned int gen8_gtt_size(uint16_t bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GGMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GGMS_MASK;
	if (bdw_gmch_ctl)
		bdw_gmch_ctl = 1 << bdw_gmch_ctl;
	return bdw_gmch_ctl << 20;
}

static unsigned int chv_gtt_size(uint16_t gmch_ctrl)
{
	gmch_ctrl >>= SNB_GMCH_GGMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GGMS_MASK;
	if (gmch_ctrl)
		return 1 << (20 + gmch_ctrl);
	return 0;
}

static unsigned int intel_gtt_size(struct data *data)
{
	if (data->gen >= 6) {
		uint16_t snb_gmch_ctl = 0;

		pci_device_cfg_read_u16(data->pci_dev, &snb_gmch_ctl, SNB_GMCH_CTRL);

		if (IS_CHERRYVIEW(data->devid))
			return chv_gtt_size(snb_gmch_ctl);
		else if (data->gen >= 8)
			return gen8_gtt_size(snb_gmch_ctl);
		else
			return gen6_gtt_size(snb_gmch_ctl);
	} else {
		static const uint16_t gtt_size_kib[8] = {
			[0] = 512,
			[1] = 256,
			[2] = 128,
			[3] = 1024,
			[4] = 2048,
			[5] = 1536,
		};
		int gtt_size;

		gtt_size = (INREG(PGETBL_CTL) & PGETBL_SIZE_MASK) >> 1;

		return gtt_size_kib[gtt_size] * 1024;
	}
}

static void map_gtt(struct data *data)
{
	unsigned int gtt_bar, gtt_offset, gtt_size;
	int error;

	switch (data->gen) {
	case 2:
		gtt_bar = 1;
		gtt_offset = 64*1024;
		break;
	case 3:
		gtt_bar = 3;
		gtt_offset = 0;
		break;
	default:
		gtt_bar = 0;
		gtt_offset = data->pci_dev->regions[gtt_bar].size / 2;
		break;
	}

	gtt_size = intel_gtt_size(data);

	error = pci_device_map_range(data->pci_dev,
				     data->pci_dev->regions[gtt_bar].base_addr + gtt_offset,
				     gtt_size, 0, &data->gtt);
	assert(error == 0);
}

static uint64_t gen2_pte_decode(uint32_t pte)
{
	return (pte & 0xfffff000) | ((pte & 0xf0) << (32 - 4));
}

static uint64_t gen6_pte_decode(uint32_t pte)
{
	return (pte & 0xfffff000) | ((pte & 0xff0) << (32 - 4));
}

static uint64_t hsw_pte_decode(uint32_t pte)
{
	return (pte & 0xfffff000) | ((pte & 0x7f0) << (32 - 4));
}

static uint64_t gen8_pte_decode(uint64_t pte)
{
	return pte & 0x7ffffff000ull;
}

static uint64_t pte_decode(struct data *data, uint64_t pte)
{
	if (data->gen >= 8)
		return gen8_pte_decode(pte);
	else if (IS_HASWELL(data->devid))
		return hsw_pte_decode(pte);
	else if (data->gen >= 6)
		return gen6_pte_decode(pte);
	else
		return gen2_pte_decode(pte);
}

static uint64_t read_pte(struct data *data, uint32_t offset)
{
	if (data->gen >= 8)
		return *((uint64_t*)data->gtt + (offset >> 12));
	else
		return *((uint32_t*)data->gtt + (offset >> 12));
}

static void update_tile_dims(struct data *data)
{
	data->tile_size = 4096; /* 1 gtt page in fact */

	switch (data->tiling) {
	case I915_TILING_NONE:
		data->tile_width = 4096;
		break;
	case I915_TILING_X:
		if (data->gen == 2)
			data->tile_width = 128 * 2; /* 2 tiles per page */
		else
			data->tile_width = 512;
		break;
	case I915_TILING_Y:
		if (data->gen == 2)
			data->tile_width = 128 * 2; /* 2 tiles per page */
		else if (IS_915(data->devid))
			data->tile_width = 512;
		else
			data->tile_width = 128;
		break;
	case I915_TILING_Yf:
		switch (data->cpp) {
		case 1:
			data->tile_width = 64;
			break;
		case 2:
		case 4:
			data->tile_width = 128;
			break;
		case 8:
		case 16:
			data->tile_width = 256;
			break;
		default:
			assert(false);
		}
		break;
	default:
		assert(false);
	}

	data->tile_height = data->tile_size / data->tile_width;

	/* to pixels */
	data->tile_width /= data->cpp;
}

static uint32_t tile_row_size(struct data *data)
{
	return data->tile_height * data->stride;
}

static void dump_png(struct data *data, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t ret;

	surface = cairo_image_surface_create_for_data(data->image,
						      data->cpp == 4 ? CAIRO_FORMAT_RGB24 :
						      data->cpp == 2 ? CAIRO_FORMAT_RGB16_565 :
						      CAIRO_FORMAT_A8,
						      data->width,
						      data->height,
						      data->width * data->cpp);
	ret = cairo_surface_write_to_png(surface, filename);
	assert(ret == CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(surface);
}

static uint32_t calc_offset(struct data *data,
			    unsigned int *x, unsigned int *y)
{
	if (data->tiling != I915_TILING_NONE) {
		unsigned int ty = *y / data->tile_height;
		unsigned int tx = *x / data->tile_width;

		*y %= data->tile_height;
		*x %= data->tile_width;

		return ty * tile_row_size(data) + tx * data->tile_size;
	} else {
		uint32_t offset = *y * data->stride + *x * data->cpp;
		uint32_t alignemnet = data->tile_size - 1;

		*y = (offset & alignemnet) / data->stride;
		*x = ((offset & alignemnet) - *y * data->stride) / data->cpp;

		return offset & ~alignemnet;
	}
}

static void *map_tile(struct data *data, uint32_t offset)
{
	uint64_t pte;
	uint64_t phys_addr;

	offset += data->offset;

	assert((offset & (data->tile_size - 1)) == 0);

	pte = read_pte(data, offset);
	phys_addr = pte_decode(data, pte);

	return mmap(NULL, data->tile_size, PROT_READ, MAP_SHARED, data->devmem_fd, phys_addr);
}

static void unmap_tile(struct data *data, void *ptr)
{
	munmap(ptr, data->tile_size);
}

static void read_tiles(struct data *data)
{
	for (unsigned int y = 0; y < data->height;) {
		unsigned int tw, th;

		for (unsigned int x = 0; x < data->width;) {
			unsigned int tx, ty;
			void *map, *src, *dst;
			uint32_t offset;

			dst = data->image + (y * data->width + x) * data->cpp;

			tx = data->x + x;
			ty = data->y + y;

			offset = calc_offset(data, &tx, &ty);

			tw = min(data->width - x, data->tile_width - tx);
			th = min(data->height - y, data->tile_height - ty);

			map = src = map_tile(data, offset);
			assert(map != (void*) -1);

			src += ty * data->tile_width * data->cpp;

			/* FIXME linear memcpy ain't right with tiling */
			for (; ty < th; ty++) {
				memcpy(dst, src + tx * data->cpp,
				       tw * data->cpp);
				src += data->tile_width * data->cpp;
				dst += data->width * data->cpp;
			}

			unmap_tile(data, map);

			x += tw;
		}

		y += th;
	}
}

static uint32_t skl_plane_read(struct data *data, uint32_t reg)
{
	return INREG(reg + 0x1000 * data->pipe + 0x100 * data->plane);
}

static bool skl_plane_init(struct data *data)
{
	uint32_t tmp, ctl, format_mask;

	ctl = skl_plane_read(data, 0x70180);
	format_mask = data->gen >= 11 ? (0x1f << 23) : (0xf << 24);

	if ((ctl & (1 << 31)) == 0) {
		fprintf(stderr, "Plane not enabled (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	switch (ctl & (0x3 << 0)) {
	case 0 << 0:
	case 2 << 0:
		break;
	default:
		fprintf(stderr, "Unknown rotation (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	switch (ctl & format_mask) {
	case 0x2 << 24:
	case 0x4 << 24:
	case 0x8 << 24:
		data->cpp = 4;
		break;
	case 0x0 << 24:
	case 0xe << 24:
		data->cpp = 2;
		break;
	case 0x1 << 24:
	case 0xc << 24:
		data->cpp = 1;
		break;
	default:
		fprintf(stderr, "Unknown pixel format (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	switch (ctl & (0x7 << 10)) {
	case 0 << 10:
		data->tiling = I915_TILING_NONE;
		break;
	case 1 << 10:
		data->tiling = I915_TILING_X;
		break;
	case 4 << 10:
		data->tiling = I915_TILING_Y;
		break;
	case 5 << 10:
		data->tiling = I915_TILING_Yf;
		break;
	default:
		fprintf(stderr, "Unknown tiling (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	update_tile_dims(data);

	data->stride = skl_plane_read(data, 0x70188);

	if (data->tiling == I915_TILING_NONE)
		data->stride *= 64;
	else
		data->stride *= data->tile_width * data->cpp;

	data->offset = skl_plane_read(data, 0x7019c);

	tmp = skl_plane_read(data, 0x70190);
	data->width = (tmp & 0xffff) + 1;
	data->height = ((tmp >> 16) & 0xffff) + 1;

	tmp = skl_plane_read(data, 0x701a4);
	data->x = tmp & 0xffff;
	data->y = (tmp >> 16) & 0xffff;

	return true;
}

static uint32_t display_base;

static uint32_t i9xx_pipe_read(struct data *data, uint32_t reg)
{
	return INREG(display_base + reg + 0x1000 * data->pipe);
}

static uint32_t i9xx_plane_read(struct data *data, uint32_t reg)
{
	return INREG(display_base + reg + 0x1000 * data->plane);
}

static bool i9xx_plane_use_tileoff(struct data *data)
{
	return IS_BROADWELL(data->devid) || IS_HASWELL(data->devid) ||
		(data->gen >= 4 && data->tiling != I915_TILING_NONE);
}

static bool i9xx_plane_init(struct data *data)
{
	uint32_t tmp, ctl;
	int pipe;

	ctl = i9xx_plane_read(data, 0x70180);

	if ((ctl & (1 << 31)) == 0) {
		fprintf(stderr, "Plane not enabled (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	if (data->gen < 5) {
		switch (ctl & (0x3 << 24)) {
		case 0 << 24:
			pipe = 0;
			break;
		case 1 << 24:
			pipe = 1;
			break;
		default:
			fprintf(stderr, "Unknown pipe selected (PLANE_CTL=0x%08x\n", ctl);
			return false;
		}
	} else {
		pipe = data->plane;
	}

	if (data->pipe < 0)
		data->pipe = pipe;

	if (data->pipe != pipe) {
		fprintf(stderr, "Incorrect pipe specified via command line (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	switch (ctl & (0xf << 26)) {
	case 0x6 << 26:
	case 0x7 << 26:
	case 0x8 << 26:
	case 0x9 << 26:
	case 0xa << 26:
	case 0xc << 26:
	case 0xe << 26:
	case 0xf << 26:
		data->cpp = 4;
		break;
	case 0x0 << 26:
	case 0x3 << 26:
	case 0x4 << 26:
	case 0x5 << 26:
		data->cpp = 2;
		break;
	case 0x2 << 26:
		data->cpp = 1;
		break;
	default:
		fprintf(stderr, "Unknown pixel format (PLANE_CTL=0x%08x\n", ctl);
		return false;
	}

	if (ctl & (1 << 10))
		data->tiling = I915_TILING_X;
	else
		data->tiling = I915_TILING_NONE;

	update_tile_dims(data);

	data->stride = i9xx_plane_read(data, 0x70188);

	if (data->gen >= 4)
		data->offset = i9xx_plane_read(data, 0x7019c);

	if (i9xx_plane_use_tileoff(data)) {
		tmp = i9xx_plane_read(data, 0x701a4);
		data->x = tmp & 0xffff;
		data->y = (tmp >> 16) & 0xffff;
	} else {
		tmp = i9xx_plane_read(data, 0x70184);
		data->y = tmp / data->stride;
		data->x = tmp % data->stride / data->cpp;
	}

	if (IS_CHERRYVIEW(data->devid) && data->pipe == 1)
		tmp = i9xx_pipe_read(data, 0x60a0c);
	else if (data->gen < 4)
		tmp = i9xx_plane_read(data, 0x70190);
	else
		tmp = 0;
	if (!tmp)
		tmp = i9xx_pipe_read(data, 0x6001c);
	data->width = (tmp & 0xffff) + 1;
	data->height = ((tmp >> 16) & 0xffff) + 1;

	return true;
}

static int num_pipes(struct data *data)
{
	if (data->gen >= 7 || !IS_VALLEYVIEW(data->devid))
		return 3;
	else if (data->gen >= 3 || IS_MOBILE(data->devid))
		return 2;
	else
		return 1;
}

static int num_planes(struct data *data)
{
	if (data->gen >= 11)
		return 7;
	else if (data->gen >= 10 || IS_GEMINILAKE(data->devid))
		return 4;
	else if (IS_BROXTON(data->devid))
		return data->pipe != 2 ? 3 : 2;
	else if (data->gen >= 5 || IS_G4X(data->devid))
		return num_pipes(data);
	else if (data->gen >= 3 || IS_MOBILE(data->devid))
		return 3;
	else
		return 1;
}

static void usage(const char *name)
{
	printf("Usage: %s [-f <filename][-w <width][-h height][-c <cpp>][-t <tiling>][-s <stride>][-o <offset][-p <pipe>][-P <plane>]\n",
	       name);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct intel_mmio_data mmio_data;
	struct data data = {
		.stride = 1024 * 4,
		.width = 1024,
		.height = 1024,
		.cpp = 4,

		.tiling = I915_TILING_NONE,
		.tile_width = 4096,
		.tile_height = 1,

		.pipe = -1,
		.plane = -1,
	};
	const char *filename = "gtt_dump.png";

	data.pci_dev = intel_get_pci_device();
	data.devid = data.pci_dev->device_id;
	data.gen = intel_gen(data.devid);

	if (IS_VALLEYVIEW(data.devid) || IS_CHERRYVIEW(data.devid))
		display_base = 0x180000;

	for (;;) {
		int r;
		const struct option opts[] = {
			{ .name = "offset", .has_arg = true, .val = 'o', },
			{ .name = "tiling", .has_arg = true, .val = 't', },
			{ .name = "stride", .has_arg = true, .val = 's', },
			{ .name = "width", .has_arg = true, .val = 'w', },
			{ .name = "height", .has_arg = true, .val = 'h', },
			{ .name = "cpp", .has_arg = true, .val = 'c', },
			{ .name = "filename", .has_arg = true, .val = 'f', },
			{ .name = "pipe", .has_arg = true, .val = 'p', },
			{ .name = "plane", .has_arg = true, .val = 'P', },
		};

		r = getopt_long(argc, argv, "o:t:s:w:h:c:f:p:P:", opts, NULL);
		if (r == -1)
			break;

		switch (r) {
		case 'o':
			data.offset = strtoull(optarg, NULL, 0);
			break;
		case 't':
			if (!strcasecmp(optarg, "linear"))
				data.tiling = I915_TILING_NONE;
			else if (!strcasecmp(optarg, "x"))
				data.tiling = I915_TILING_X;
			else if (!strcasecmp(optarg, "y"))
				data.tiling = I915_TILING_Y;
			else if (!strcasecmp(optarg, "yf"))
				data.tiling = I915_TILING_Yf;
			else
				usage(argv[0]);
			break;
		case 's':
			data.stride = strtoul(optarg, NULL, 0);
			break;
		case 'w':
			data.width = strtoul(optarg, NULL, 0);
			break;
		case 'h':
			data.height = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			data.cpp = strtoul(optarg, NULL, 0);
			switch (data.cpp) {
			case 1:
			case 2:
			case 4:
				break;
			default:
				usage(argv[0]);
			}
			break;
		case 'f':
			filename = optarg;
			break;
		case 'p':
			if (optarg[0] >= 'a')
				data.pipe = optarg[0] - 'a';
			else
				data.pipe = optarg[0] - 'A';
			break;
		case 'P':
			if (data.gen >= 9) {
				data.plane = strtoul(optarg, NULL, 0);
			} else {
				if (optarg[0] >= 'a')
					data.plane = optarg[0] - 'a';
				else
					data.plane = optarg[0] - 'A';
			}
			break;
		default:
			break;
		}
	}

	intel_register_access_init(&mmio_data, data.pci_dev, 0, -1);

	if (data.gen >= 9) {
		/* require both pipe and plane, or neither */
		if ((data.pipe < 0) != (data.plane < 0))
			usage(argv[0]);

		if (data.pipe >= num_pipes(&data))
			usage(argv[0]);
		if (data.plane >= num_planes(&data))
			usage(argv[0]);

		if (data.plane >= 0 && !skl_plane_init(&data))
			usage(argv[0]);
	} else {
		if (data.plane >= num_planes(&data))
			usage(argv[0]);

		if (data.plane >= 0 && !i9xx_plane_init(&data))
			usage(argv[0]);
	}

	if (!data.offset)
		usage(argv[0]);

	update_tile_dims(&data);

	data.image = calloc(data.width * data.height, data.cpp);
	assert(data.image);

	data.devmem_fd = open("/dev/mem", O_RDONLY);
	assert(data.devmem_fd >= 0);

	map_gtt(&data);

	intel_register_access_fini(&mmio_data);

	read_tiles(&data);

	dump_png(&data, filename);

	return 0;
}
