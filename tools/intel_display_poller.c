/*
 * Copyright © 2014 Intel Corporation
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
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include "intel_chipset.h"
#include "intel_io.h"
#include "intel_reg.h"
#include "igt_debugfs.h"
#include "drmtest.h"
#include "igt_aux.h"

enum test {
	TEST_INVALID,
	TEST_PIPESTAT,
	TEST_IIR,
	TEST_IIR_GEN2,
	TEST_IIR_GEN3,
	TEST_DEIIR,
	TEST_FRAMECOUNT,
	TEST_FRAMECOUNT_GEN3,
	TEST_FRAMECOUNT_G4X,
	TEST_FLIPCOUNT,
	TEST_PAN,
	TEST_FLIP,
	TEST_FLIPDONE,
	TEST_FLIPDONE_PIPESTAT,
	TEST_FLIPDONE_DEIIR,
	TEST_SURFLIVE,
	TEST_WRAP,
	TEST_FIELD,
};

static uint32_t vlv_offset;
static uint16_t pipe_offset[4] = { 0, 0x1000, 0x2000, 0x3000, };

#define PIPE_REG(pipe, reg_a) (pipe_offset[(pipe)] + (reg_a))

static volatile bool quit;

static void sighandler(int x)
{
	quit = true;
}

static uint16_t read_reg_16(uint32_t reg)
{
	return INREG16(vlv_offset + reg);
}

static uint32_t read_reg(uint32_t reg)
{
	return INREG(vlv_offset + reg);
}

static void write_reg_16(uint32_t reg, uint16_t val)
{
	OUTREG16(vlv_offset + reg, val);
}

static void write_reg(uint32_t reg, uint32_t val)
{
	OUTREG(vlv_offset + reg, val);
}

static char pipe_name(int pipe)
{
	return pipe + 'A';
}

static int pipe_to_plane(uint32_t devid, int pipe)
{
	if (intel_gen(devid) >= 4)
		return pipe;

	switch (pipe) {
	case 0:
		if ((read_reg(DSPACNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_A)
			return 0;
		if ((read_reg(DSPBCNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_A)
			return 1;
		fprintf(stderr, "no plane assigned to pipe %c, assuming %c\n",
			pipe_name(pipe), pipe_name(pipe));
		return pipe;
	case 1:
		if ((read_reg(DSPACNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_B)
			return 0;
		if ((read_reg(DSPBCNTR) & DISPPLANE_SEL_PIPE_MASK) == DISPPLANE_SEL_PIPE_B)
			return 1;
		fprintf(stderr, "no plane assigned to pipe %c, assuming %c\n",
			pipe_name(pipe), pipe_name(pipe));
		return pipe;
	}

	assert(0);

	return 0;
}

static uint32_t dspoffset_reg(uint32_t devid, int pipe)
{
	bool use_tileoff;
	int plane = pipe_to_plane(devid, pipe);

	if (intel_gen(devid) < 4)
		use_tileoff = false;
	else if (IS_HASWELL(devid) || IS_BROADWELL(devid) || intel_gen(devid) >= 9)
		use_tileoff = true;
	else
		use_tileoff = read_reg(PIPE_REG(plane, DSPACNTR)) & DISPLAY_PLANE_TILED;

	if (use_tileoff)
		return PIPE_REG(plane, DSPATILEOFF);
	else
		return PIPE_REG(plane, DSPABASE);
}

static uint32_t dspsurf_reg(uint32_t devid, int pipe, bool async)
{
	int plane = pipe_to_plane(devid, pipe);

	if (async && (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid)))
		return PIPE_REG(plane, DSPAADDR_VLV);

	if (intel_gen(devid) < 4)
		return PIPE_REG(plane, DSPABASE);
	else
		return PIPE_REG(plane, DSPASURF);
}

static void enable_async_flip(uint32_t devid, int pipe, bool enable)
{
	int plane = pipe_to_plane(devid, pipe);
	uint32_t tmp;

	if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))
		return;

	tmp = read_reg(PIPE_REG(plane, DSPACNTR));
	if (enable)
		tmp |= 1 << 9;
	else
		tmp &= ~(1 << 9);
	write_reg(PIPE_REG(plane, DSPACNTR), tmp);
}

static int wait_scanline(int pipe, int target_scanline, bool *field)
{
	uint32_t dsl_reg = PIPE_REG(pipe, PIPEA_DSL);

	while (!quit) {
		uint32_t dsl = read_reg(dsl_reg);
		*field = dsl & 0x80000000;
		dsl &= ~0x80000000;
		if (dsl == target_scanline)
			return dsl;
	}

	return 0;
}

static void poll_pixel_pipestat(int pipe, int bit, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, iir, iir1, iir2, iir_bit, iir_mask;
	int i = 0;

	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);
	iir_bit = 1 << bit;
	iir = PIPE_REG(pipe, PIPEASTAT);

	iir_mask = read_reg(iir) & 0x7fff0000;

	write_reg(iir, iir_mask | iir_bit);

	while (!quit) {
		pix1 = read_reg(pix);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		pix2 = read_reg(pix);

		if (!(iir2 & iir_bit))
			continue;

		if (iir1 & iir_bit) {
			write_reg(iir, iir_mask | iir_bit);
			continue;
		}

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_pixel_iir_gen3(int pipe, int bit, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, iir1, iir2, imr_save, ier_save;
	int i = 0;

	bit = 1 << bit;
	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);

	imr_save = read_reg(IMR);
	ier_save = read_reg(IER);

	write_reg(IER, ier_save & ~bit);
	write_reg(IMR, imr_save & ~bit);

	write_reg(IIR, bit);

	while (!quit) {
		pix1 = read_reg(pix);
		iir1 = read_reg(IIR);
		iir2 = read_reg(IIR);
		pix2 = read_reg(pix);

		if (!(iir2 & bit))
			continue;

		write_reg(IIR, bit);

		if (iir1 & bit)
			continue;

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(IMR, imr_save);
	write_reg(IER, ier_save);
}

static void poll_pixel_framecount_gen3(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2, frm1, frm2;
	int i = 0;

	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);

	while (!quit) {
		pix1 = read_reg(pix);
		pix2 = read_reg(pix);

		frm1 = pix1 >> 24;
		frm2 = pix2 >> 24;

		if (frm1 + 1 != frm2)
			continue;

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_pixel_pan(uint32_t devid, int pipe, int target_pixel, int target_fuzz,
			   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1 = 0, pix2 = 0;
	uint32_t saved, surf = 0;
	int i = 0;

	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);
	surf = dspoffset_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit){
			pix1 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix1 == target_pixel)
				break;
		}

		write_reg(surf, saved+256);

		while (!quit){
			pix2 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix2 >= target_pixel + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_pixel_flip(uint32_t devid, int pipe, int target_pixel, int target_fuzz,
			    uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1 = 0, pix2 = 0;
	uint32_t saved, surf = 0;
	int i = 0;

	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);
	surf = dspsurf_reg(devid, pipe, false);

	saved = read_reg(surf);

	while (!quit) {
		while (!quit){
			pix1 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix1 == target_pixel)
				break;
		}

		write_reg(surf, saved+256*1024);

		while (!quit){
			pix2 = read_reg(pix) & PIPE_PIXEL_MASK;
			if (pix2 >= target_pixel + target_fuzz)
				break;
		}

		write_reg(surf, saved);

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_pixel_wrap(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t pix, pix1, pix2;
	int i = 0;

	pix = PIPE_REG(pipe, PIPEAFRAMEPIXEL);

	while (!quit) {
		pix1 = read_reg(pix);
		pix2 = read_reg(pix);

		pix1 &= PIPE_PIXEL_MASK;
		pix2 &= PIPE_PIXEL_MASK;

		if (pix2 >= pix1)
			continue;

		min[i] = pix1;
		max[i] = pix2;
		if (++i >= count)
			break;
	}
}

static void poll_dsl_pipestat(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir, iir1, iir2, iir_bit, iir_mask;
	bool field1, field2;
	int i[2] = {};

	iir_bit = 1 << bit;
	iir = PIPE_REG(pipe, PIPEASTAT);
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	iir_mask = read_reg(iir) & 0x7fff0000;

	write_reg(iir, iir_mask | iir_bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & iir_bit))
			continue;

		if (iir1 & iir_bit) {
			write_reg(iir, iir_mask | iir_bit);
			continue;
		}

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_iir_gen2(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	int i[2] = {};

	bit = 1 << bit;
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	imr_save = read_reg_16(IMR);
	ier_save = read_reg_16(IER);

	write_reg_16(IER, ier_save & ~bit);
	write_reg_16(IMR, imr_save & ~bit);

	write_reg_16(IIR, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg_16(IIR);
		iir2 = read_reg_16(IIR);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg_16(IIR, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg_16(IMR, imr_save);
	write_reg_16(IER, ier_save);
}

static void poll_dsl_iir_gen3(int pipe, int bit,
			      uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	int i[2] = {};

	bit = 1 << bit;
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	imr_save = read_reg(IMR);
	ier_save = read_reg(IER);

	write_reg(IER, ier_save & ~bit);
	write_reg(IMR, imr_save & ~bit);

	write_reg(IIR, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(IIR);
		iir2 = read_reg(IIR);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg(IIR, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(IMR, imr_save);
	write_reg(IER, ier_save);
}

static void poll_dsl_deiir(uint32_t devid, int pipe, int bit,
			   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, iir1, iir2, imr_save, ier_save;
	bool field1, field2;
	uint32_t iir, ier, imr;
	int i[2] = {};

	bit = 1 << bit;
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	if (intel_gen(devid) >= 8) {
		iir = GEN8_DE_PIPE_IIR(pipe);
		ier = GEN8_DE_PIPE_IER(pipe);
		imr = GEN8_DE_PIPE_IMR(pipe);
	} else {
		iir = DEIIR;
		ier = DEIER;
		imr = DEIMR;
	}

	imr_save = read_reg(imr);
	ier_save = read_reg(ier);

	write_reg(ier, ier_save & ~bit);
	write_reg(imr, imr_save & ~bit);

	write_reg(iir, bit);

	while (!quit) {
		dsl1 = read_reg(dsl);
		iir1 = read_reg(iir);
		iir2 = read_reg(iir);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (!(iir2 & bit))
			continue;

		write_reg(iir, bit);

		if (iir1 & bit)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(imr, imr_save);
	write_reg(ier, ier_save);
}

static void poll_dsl_framecount_g4x(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, frm, frm1, frm2;
	bool field1, field2;
	int i[2] = {};

	frm = PIPE_REG(pipe, PIPEAFRMCOUNT_G4X);
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	while (!quit) {
		dsl1 = read_reg(dsl);
		frm1 = read_reg(frm);
		frm2 = read_reg(frm);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (frm1 + 1 != frm2)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_flipcount_g4x(uint32_t devid, int pipe,
				   uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, flp, flp1, flp2, surf;
	bool field1, field2;
	int i[2] = {};

	flp = PIPE_REG(pipe, PIPEAFLIPCOUNT_G4X);
	dsl = PIPE_REG(pipe, PIPEA_DSL);
	surf = dspsurf_reg(devid, pipe, false);

	while (!quit) {
		usleep(10);
		dsl1 = read_reg(dsl);
		flp1 = read_reg(flp);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			return;

		write_reg(surf, read_reg(surf));

		while (!quit) {
			dsl1 = read_reg(dsl);
			flp2 = read_reg(flp);
			dsl2 = read_reg(dsl);

			field1 = dsl1 & 0x80000000;
			field2 = dsl2 & 0x80000000;
			dsl1 &= ~0x80000000;
			dsl2 &= ~0x80000000;

			if (flp1 == flp2)
				continue;

			if (field1 != field2)
				printf("fields are different (%u:%u -> %u:%u)\n",
				       field1, dsl1, field2, dsl2);

			min[field1*count+i[field1]] = dsl1;
			max[field1*count+i[field1]] = dsl2;
			if (++i[field1] >= count)
				break;
		}
		if (i[field1] >= count)
			break;
	}
}

static void poll_dsl_framecount_gen3(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2, frm, frm1, frm2;
	bool field1, field2;
	int i[2] = {};

	frm = PIPE_REG(pipe, PIPEAFRAMEPIXEL);
	dsl = PIPE_REG(pipe, PIPEA_DSL);

	while (!quit) {
		dsl1 = read_reg(dsl);
		frm1 = read_reg(frm) >> 24;
		frm2 = read_reg(frm) >> 24;
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (frm1 + 1 != frm2)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_pan(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
			 uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl1 = 0, dsl2 = 0;
	bool field1 = false, field2 = false;
	uint32_t saved, surf = 0;
	int i[2] = {};

	surf = dspoffset_reg(devid, pipe);

	saved = read_reg(surf);

	while (!quit) {
		dsl1 = wait_scanline(pipe, target_scanline, &field1);

		write_reg(surf, saved+256);

		dsl2 = wait_scanline(pipe, target_scanline + target_fuzz, &field2);

		write_reg(surf, saved);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	write_reg(surf, saved);
}

static void poll_dsl_flip(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
			  uint32_t *min, uint32_t *max, const int count, bool async)
{
	uint32_t dsl1 = 0, dsl2 = 0;
	bool field1 = false, field2 = false;
	uint32_t saved, surf = 0;
	int i[2] = {};

	surf = dspsurf_reg(devid, pipe, async);

	saved = read_reg(surf);

	enable_async_flip(devid, pipe, async);

	while (!quit) {
		dsl1 = wait_scanline(pipe, target_scanline, &field1);

		write_reg(surf, saved+256*1024);

		dsl2 = wait_scanline(pipe, target_scanline + target_fuzz, &field2);

		write_reg(surf, saved);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	enable_async_flip(devid, pipe, false);
	write_reg(surf, saved);
}

static void poll_dsl_flipdone_pipestat(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
				       uint32_t *min, uint32_t *max, const int count, bool async)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0;
	uint32_t pipestat, pipestat1, pipestat2, pipestat_save;
	bool field1 = false, field2 = false;
	uint32_t saved, next, surf = 0, bit;
	int i[2] = {};

	dsl = PIPE_REG(pipe, PIPEA_DSL);
	pipestat = PIPE_REG(pipe, PIPEASTAT);
	surf = dspsurf_reg(devid, pipe, async);

	bit = 1 << 10;

	saved = read_reg(surf);
	next = saved;

	pipestat_save = read_reg(pipestat) & 0x7fff0000;
	pipestat1 = pipestat_save & ~(1 << (bit<<16));
	write_reg(pipestat, pipestat1 | bit);

	enable_async_flip(devid, pipe, async);

	while (!quit) {
		dsl1 = wait_scanline(pipe, target_scanline, &field1);

		write_reg(pipestat, pipestat1 | bit);
		if (next == saved)
			next = saved+256*1024;
		else
			next = saved;
		write_reg(surf, next);

		while (!quit) {
			pipestat2 = read_reg(pipestat);
			dsl2 = read_reg(dsl);

			field2 = dsl2 & 0x80000000;
			dsl2 &= ~0x80000000;

			if (pipestat2 & bit)
				break;
		}

		write_reg(pipestat, pipestat1 | bit);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	enable_async_flip(devid, pipe, false);
	write_reg(surf, saved);
	write_reg(pipestat, pipestat_save);
}

static void poll_dsl_flipdone_deiir(uint32_t devid, int pipe, int target_scanline, int target_fuzz,
				    uint32_t *min, uint32_t *max, const int count, bool async)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0;
	uint32_t iir, iir2, ier, imr;
	uint32_t ier_save, imr_save;
	bool field1 = false, field2 = false;
	uint32_t saved, next, surf = 0, bit;
	int i[2] = {};

	dsl = PIPE_REG(pipe, PIPEA_DSL);
	surf = dspsurf_reg(devid, pipe, async);

	if (intel_gen(devid) >= 9)
		bit = 3;
	else if (intel_gen(devid) >= 8)
		bit = 4;
	else if (intel_gen(devid) >= 7)
		bit = 3 + 5 * pipe;
	else if (intel_gen(devid) >= 5)
		bit = 26 + pipe;
	else
		abort();
	bit = 1 << bit;

	if (intel_gen(devid) >= 8) {
		iir = GEN8_DE_PIPE_IIR(pipe);
		ier = GEN8_DE_PIPE_IER(pipe);
		imr = GEN8_DE_PIPE_IMR(pipe);
	} else {
		iir = DEIIR;
		ier = DEIER;
		imr = DEIMR;
	}

	saved = read_reg(surf);
	next = saved;

	imr_save = read_reg(imr);
	ier_save = read_reg(ier);
	write_reg(ier, ier_save & ~bit);
	write_reg(imr, imr_save & ~bit);

	enable_async_flip(devid, pipe, async);

	while (!quit) {
		dsl1 = wait_scanline(pipe, target_scanline, &field1);

		write_reg(iir, bit);
		if (next == saved)
			next = saved+256*1024;
		else
			next = saved;
		write_reg(surf, next);

		while (!quit) {
			iir2 = read_reg(iir);
			dsl2 = read_reg(dsl);

			field2 = dsl2 & 0x80000000;
			dsl2 &= ~0x80000000;

			if (iir2 & bit)
				break;
		}

		write_reg(iir, bit);

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}

	enable_async_flip(devid, pipe, false);
	write_reg(surf, saved);
	write_reg(imr, imr_save);
	write_reg(ier, ier_save);
}

static void poll_dsl_surflive(uint32_t devid, int pipe,
			      uint32_t *min, uint32_t *max, const int count, bool async)
{
	uint32_t dsl, dsl1 = 0, dsl2 = 0, surf, surf1, surf2, surflive, surfl1 = 0, surfl2, saved, tmp;
	bool field1 = false, field2 = false;
	int i[2] = {};

	surflive = PIPE_REG(pipe, DSPASURFLIVE);
	dsl = PIPE_REG(pipe, PIPEA_DSL);
	surf = dspsurf_reg(devid, pipe, async);

	saved = read_reg(surf);

	surf1 = saved & ~0xfff;
	surf2 = surf1 + 256*1024;

	enable_async_flip(devid, pipe, async);

	while (!quit) {
		write_reg(surf, surf2);

		while (!quit) {
			dsl1 = read_reg(dsl);
			surfl1 = read_reg(surflive) & ~0xfff;
			surfl2 = read_reg(surflive) & ~0xfff;
			dsl2 = read_reg(dsl);

			field1 = dsl1 & 0x80000000;
			field2 = dsl2 & 0x80000000;
			dsl1 &= ~0x80000000;
			dsl2 &= ~0x80000000;

			if (surfl2 == surf2)
				break;
		}

		if (surfl1 != surf2) {
			if (field1 != field2)
				printf("fields are different (%u:%u -> %u:%u)\n",
				       field1, dsl1, field2, dsl2);

			min[field1*count+i[field1]] = dsl1;
			max[field1*count+i[field1]] = dsl2;
			if (++i[field1] >= count)
				break;
		}

		tmp = surf1;
		surf1 = surf2;
		surf2 = tmp;
	}

	enable_async_flip(devid, pipe, false);
	write_reg(surf, saved);
}

static void poll_dsl_wrap(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2;
	bool field1, field2;
	int i[2] = {};

	dsl = PIPE_REG(pipe, PIPEA_DSL);

	while (!quit) {
		dsl1 = read_reg(dsl);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (dsl2 >= dsl1)
			continue;

		if (field1 != field2)
			printf("fields are different (%u:%u -> %u:%u)\n",
			       field1, dsl1, field2, dsl2);

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static void poll_dsl_field(int pipe, uint32_t *min, uint32_t *max, const int count)
{
	uint32_t dsl, dsl1, dsl2;
	bool field1, field2;
	int i[2] = {};

	dsl = PIPE_REG(pipe, PIPEA_DSL);

	while (!quit) {
		dsl1 = read_reg(dsl);
		dsl2 = read_reg(dsl);

		field1 = dsl1 & 0x80000000;
		field2 = dsl2 & 0x80000000;
		dsl1 &= ~0x80000000;
		dsl2 &= ~0x80000000;

		if (field1 == field2)
			continue;

		min[field1*count+i[field1]] = dsl1;
		max[field1*count+i[field1]] = dsl2;
		if (++i[field1] >= count)
			break;
	}
}

static const char *test_name(enum test test, int pipe, int bit, bool test_pixel_count)
{
	static char str[64];
	const char *type = test_pixel_count ? "pixel" : "dsl";

	switch (test) {
	case TEST_PIPESTAT:
		snprintf(str, sizeof str, "%s / pipe %c / PIPESTAT[%d] (gmch)", type, pipe_name(pipe), bit);
		return str;
	case TEST_IIR_GEN2:
		snprintf(str, sizeof str, "%s / pipe %c / IIR[%d] (gen2)", type, pipe_name(pipe), bit);
		return str;
	case TEST_IIR_GEN3:
		snprintf(str, sizeof str, "%s / pipe %c / IIR[%d] (gen3+)", type, pipe_name(pipe), bit);
		return str;
	case TEST_DEIIR:
		snprintf(str, sizeof str, "%s / pipe %c / DEIIR[%d] (pch)", type, pipe_name(pipe), bit);
		return str;
	case TEST_FRAMECOUNT_GEN3:
		snprintf(str, sizeof str, "%s / pipe %c / Frame count (gen3/4)", type, pipe_name(pipe));
		return str;
	case TEST_FRAMECOUNT_G4X:
		snprintf(str, sizeof str, "%s / pipe %c / Frame count (g4x+)", type, pipe_name(pipe));
		return str;
	case TEST_FLIPCOUNT:
		snprintf(str, sizeof str, "%s / pipe %c / Flip count (g4x+)", type, pipe_name(pipe));
		return str;
	case TEST_PAN:
		snprintf(str, sizeof str, "%s / pipe %c / Pan", type, pipe_name(pipe));
		return str;
	case TEST_FLIP:
		snprintf(str, sizeof str, "%s / pipe %c / Flip", type, pipe_name(pipe));
		return str;
	case TEST_FLIPDONE_PIPESTAT:
		snprintf(str, sizeof str, "%s / pipe %c / Flip done (vlv/chv)", type, pipe_name(pipe));
		return str;
	case TEST_FLIPDONE_DEIIR:
		snprintf(str, sizeof str, "%s / pipe %c / Flip done (pch)", type, pipe_name(pipe));
		return str;
	case TEST_SURFLIVE:
		snprintf(str, sizeof str, "%s / pipe %c / Surflive", type, pipe_name(pipe));
		return str;
	case TEST_WRAP:
		snprintf(str, sizeof str, "%s / pipe %c / Wrap", type, pipe_name(pipe));
		return str;
	case TEST_FIELD:
		snprintf(str, sizeof str, "%s / pipe %c / Field", type, pipe_name(pipe));
		return str;
	default:
		return "";
	}
}

static void __attribute__((noreturn)) usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options]\n"
		" -t,--test <pipestat|iir|framecount|flipcount|pan|flip|flipdone|surflive|wrap|field>\n"
		" -p,--pipe <pipe>\n"
		" -b,--bit <bit>\n"
		" -l,--line <target scanline/pixel>\n"
		" -f,--fuzz <target fuzz>\n"
		" -x,--pixel\n"
		" -a,--async\n",
		name);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct intel_mmio_data mmio_data;
	int i;
	int pipe = 0, bit = 0, target_scanline = 0, target_fuzz = 1;
	bool test_pixelcount = false;
	bool test_async_flip = false;
	uint32_t devid;
	uint32_t min[2*128] = {};
	uint32_t max[2*128] = {};
	uint32_t a, b;
	enum test test = TEST_INVALID;
	const int count = ARRAY_SIZE(min)/2;

	for (;;) {
		static const struct option long_options[] = {
			{ .name = "test", .has_arg = required_argument, },
			{ .name = "pipe", .has_arg = required_argument, },
			{ .name = "bit", .has_arg = required_argument, },
			{ .name = "line", .has_arg = required_argument, },
			{ .name = "fuzz", .has_arg = required_argument, },
			{ .name = "pixel", .has_arg = no_argument, },
			{ .name = "async", .has_arg = no_argument, },
			{ },
		};

		int opt = getopt_long(argc, argv, "t:p:b:l:f:xa", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 't':
			if (!strcmp(optarg, "pipestat"))
				test = TEST_PIPESTAT;
			else if (!strcmp(optarg, "iir"))
				test = TEST_IIR;
			else if (!strcmp(optarg, "framecount"))
				test = TEST_FRAMECOUNT;
			else if (!strcmp(optarg, "flipcount"))
				test = TEST_FLIPCOUNT;
			else if (!strcmp(optarg, "pan"))
				test = TEST_PAN;
			else if (!strcmp(optarg, "flip"))
				test = TEST_FLIP;
			else if (!strcmp(optarg, "flipdone"))
				test = TEST_FLIPDONE;
			else if (!strcmp(optarg, "surflive"))
				test = TEST_SURFLIVE;
			else if (!strcmp(optarg, "wrap"))
				test = TEST_WRAP;
			else if (!strcmp(optarg, "field"))
				test = TEST_FIELD;
			else
				usage(argv[0]);
			break;
		case 'p':
			if (optarg[1] != '\0')
				usage(argv[0]);
			pipe = optarg[0];
			if (pipe >= 'a')
				pipe -= 'a';
			else if (pipe >= 'A')
				pipe -= 'A';
			else if (pipe >= '0')
				pipe -= '0';
			else
				usage(argv[0]);
			if (pipe < 0 || pipe > 3)
				usage(argv[0]);
			break;
		case 'b':
			bit = atoi(optarg);
			if (bit < 0 || bit > 31)
				usage(argv[0]);
			break;
		case 'l':
			target_scanline = atoi(optarg);
			if (target_scanline < 0)
				usage(argv[0]);
			break;
		case 'f':
			target_fuzz = atoi(optarg);
			if (target_fuzz <= 0)
				usage(argv[0]);
			break;
		case 'x':
			test_pixelcount = true;
			break;
		case 'a':
			test_async_flip = true;
			break;
		}
	}

	devid = intel_get_pci_device()->device_id;

	/*
	 * check if the requires registers are
	 * avilable on the current platform.
	 */
	if (intel_gen(devid) == 2) {
		if (pipe > 1)
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		if (test_async_flip)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN2;
			break;
		case TEST_PIPESTAT:
		case TEST_PAN:
			break;
		case TEST_FLIP:
			test = TEST_PAN;
			break;
		default:
			usage(argv[0]);
		}
	} else if (intel_gen(devid) < 5 && !IS_G4X(devid)) {
		if (pipe > 1)
			usage(argv[0]);

		if (test_async_flip)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN3;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_GEN3;
			break;
		case TEST_PIPESTAT:
		case TEST_PAN:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		case TEST_FLIP:
			if (intel_gen(devid) == 3)
				test = TEST_PAN;
			break;
		default:
			usage(argv[0]);
		}
	} else if (IS_G4X(devid) || IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid)) {
		if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))
			vlv_offset = 0x180000;
		if (IS_CHERRYVIEW(devid))
			pipe_offset[2] = 0x4000;

		if (pipe > 1 && !IS_CHERRYVIEW(devid))
			usage(argv[0]);
		if (pipe > 2)
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_IIR_GEN3;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_G4X;
			break;
		case TEST_FLIPDONE:
			/*
			 * g4x has no apparent "flip done" interrupt,
			 * and the "flip pending" interrupt does not
			 * seem to do anything with mmio flips.
			 */
			if (IS_G4X(devid))
				usage(argv[0]);
			test = TEST_FLIPDONE_PIPESTAT;
			break;
		case TEST_FLIPCOUNT:
		case TEST_PIPESTAT:
		case TEST_PAN:
		case TEST_FLIP:
		case TEST_SURFLIVE:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		default:
			usage(argv[0]);
		}
	} else {
		if (pipe > 1 && intel_gen(devid) < 7)
			usage(argv[0]);
		if (pipe > 2 && intel_gen(devid) < 12)
			usage(argv[0]);
		if (pipe > 3)
			usage(argv[0]);

		if (test_pixelcount)
			usage(argv[0]);

		switch (test) {
		case TEST_IIR:
			test = TEST_DEIIR;
			break;
		case TEST_FRAMECOUNT:
			test = TEST_FRAMECOUNT_G4X;
			break;
		case TEST_FLIPDONE:
			test = TEST_FLIPDONE_DEIIR;
			break;
		case TEST_FLIPCOUNT:
		case TEST_PAN:
		case TEST_FLIP:
		case TEST_SURFLIVE:
		case TEST_WRAP:
		case TEST_FIELD:
			break;
		default:
			usage(argv[0]);
		}
	}

	switch (test) {
	case TEST_IIR:
	case TEST_FRAMECOUNT:
	case TEST_FLIPDONE:
		/* should no longer have the generic tests here */
		assert(0);
	default:
		break;
	}

	intel_register_access_init(&mmio_data ,intel_get_pci_device(), 0, -1);

	printf("%s?\n", test_name(test, pipe, bit, test_pixelcount));

	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	switch (test) {
	case TEST_PIPESTAT:
		if (test_pixelcount)
			poll_pixel_pipestat(pipe, bit, min, max, count);
		else
			poll_dsl_pipestat(pipe, bit, min, max, count);
		break;
	case TEST_IIR_GEN2:
		assert(!test_pixelcount);
		poll_dsl_iir_gen2(pipe, bit, min, max, count);
		break;
	case TEST_IIR_GEN3:
		if (test_pixelcount)
			poll_pixel_iir_gen3(pipe, bit, min, max, count);
		else
			poll_dsl_iir_gen3(pipe, bit, min, max, count);
		break;
	case TEST_DEIIR:
		assert(!test_pixelcount);
		poll_dsl_deiir(devid, pipe, bit, min, max, count);
		break;
	case TEST_FRAMECOUNT_GEN3:
		if (test_pixelcount)
			poll_pixel_framecount_gen3(pipe, min, max, count);
		else
			poll_dsl_framecount_gen3(pipe, min, max, count);
		break;
	case TEST_FRAMECOUNT_G4X:
		assert(!test_pixelcount);
		poll_dsl_framecount_g4x(pipe, min, max, count);
		break;
	case TEST_FLIPCOUNT:
		assert(!test_pixelcount);
		poll_dsl_flipcount_g4x(devid, pipe, min, max, count);
		break;
	case TEST_PAN:
		if (test_pixelcount)
			poll_pixel_pan(devid, pipe, target_scanline, target_fuzz,
				       min, max, count);
		else
			poll_dsl_pan(devid, pipe, target_scanline, target_fuzz,
				     min, max, count);
		break;
	case TEST_FLIP:
		if (test_pixelcount)
			poll_pixel_flip(devid, pipe, target_scanline, target_fuzz,
					min, max, count);
		else
			poll_dsl_flip(devid, pipe, target_scanline, target_fuzz,
				      min, max, count, test_async_flip);
		break;
	case TEST_FLIPDONE_PIPESTAT:
		poll_dsl_flipdone_pipestat(devid, pipe, target_scanline, target_fuzz,
					   min, max, count, test_async_flip);
		break;
	case TEST_FLIPDONE_DEIIR:
		poll_dsl_flipdone_deiir(devid, pipe, target_scanline, target_fuzz,
					min, max, count, test_async_flip);
		break;
	case TEST_SURFLIVE:
		poll_dsl_surflive(devid, pipe, min, max, count, test_async_flip);
		break;
	case TEST_WRAP:
		if (test_pixelcount)
			poll_pixel_wrap(pipe, min, max, count);
		else
			poll_dsl_wrap(pipe, min, max, count);
		break;
	case TEST_FIELD:
		poll_dsl_field(pipe, min, max, count);
		break;
	default:
		assert(0);
	}

	intel_register_access_fini(&mmio_data);

	if (quit)
		return 0;

	for (i = 0; i < count; i++) {
		if (min[0*count+i] == 0 && max[0*count+i] == 0)
			break;
		printf("[%u] %4u - %4u (%4u)\n", 0, min[0*count+i], max[0*count+i],
		       (min[0*count+i] + max[0*count+i] + 1) >> 1);
	}
	for (i = 0; i < count; i++) {
		if (min[1*count+i] == 0 && max[1*count+i] == 0)
			break;
		printf("[%u] %4u - %4u (%4u)\n", 1, min[1*count+i], max[1*count+i],
		       (min[1*count+i] + max[1*count+i] + 1) >> 1);
	}

	a = 0;
	b = 0xffffffff;
	for (i = 0; i < count; i++) {
		if (min[0*count+i] == 0 && max[0*count+i] == 0)
			break;
		a = max(a, min[0*count+i]);
		b = min(b, max[0*count+i]);
	}

	printf("%s: [%u] %6u - %6u\n", test_name(test, pipe, bit, test_pixelcount), 0, a, b);

	a = 0;
	b = 0xffffffff;
	for (i = 0; i < count; i++) {
		if (min[1*count+i] == 0 && max[1*count+i] == 0)
			break;
		a = max(a, min[1*count+i]);
		b = min(b, max[1*count+i]);
	}

	printf("%s: [%u] %6u - %6u\n", test_name(test, pipe, bit, test_pixelcount), 1, a, b);

	return 0;
}
