/*
 * Copyright © 2017 Intel Corporation
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

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include "intel_io.h"
#include "intel_chipset.h"
#include "drmtest.h"

static uint8_t slave_address = 0x50;
static int low_period_us = 10;

static uint32_t gpio_reg;
static uint32_t gpio_reserved;

static uint32_t reg_read(void)
{
	return INREG(gpio_reg);
}

static void reg_write(uint32_t val)
{
	return OUTREG(gpio_reg, val);
}

#if 0
#define debug printf
#else
#define debug(x ...) do {} while (0)
#endif

#if 0
#define debug_state printf
#else
#define debug_state(x ...) do {} while (0)
#endif

#define GPIO(i)	(0x5010 + 4 * (i))

static bool getsda(void)
{
	return reg_read() & GPIO_DATA_VAL_IN;
}

static bool getscl(void)
{
	return reg_read() & GPIO_CLOCK_VAL_IN;
}

static void setsda(bool state)
{
	uint32_t val;

	debug("SDA=%d\n", state);

	if (state)
		val = GPIO_DATA_DIR_MASK |
			GPIO_DATA_DIR_IN;
	else
		val = GPIO_DATA_DIR_MASK |
			GPIO_DATA_DIR_OUT |
			GPIO_DATA_VAL_MASK;

	reg_write(gpio_reserved | val);
}

static void setscl(bool state)
{
	uint32_t val;

	debug("SCL=%d\n", state);

	if (state)
		val = GPIO_CLOCK_DIR_MASK |
			GPIO_CLOCK_DIR_IN;
	else
		val = GPIO_CLOCK_DIR_MASK |
			GPIO_CLOCK_DIR_OUT |
			GPIO_CLOCK_VAL_MASK;

	reg_write(gpio_reserved | val);
}

enum i2c_state {
	IDLE,
	START,
	SCL_LOW_0,
	SCL_HIGH_0,
	SCL_LOW_1,
	SCL_HIGH_1,
	SCL_LOW_2,
	SCL_HIGH_2,
	SCL_LOW_3,
	SCL_HIGH_3,
	SCL_LOW_4,
	SCL_HIGH_4,
	SCL_LOW_5,
	SCL_HIGH_5,
	SCL_LOW_6,
	SCL_HIGH_6,
	SCL_LOW_7,
	SCL_HIGH_7,
	SCL_LOW_ACK,
	SCL_HIGH_ACK,
	STOP,
};

enum i2c_cycle {
	READ,
	WRITE,
	ADDRESS,
};

#define NAME(x) [x] = #x

static const char * const i2c_state_name[] = {
	NAME(IDLE),
	NAME(START),
	NAME(SCL_LOW_0),
	NAME(SCL_HIGH_0),
	NAME(SCL_LOW_1),
	NAME(SCL_HIGH_1),
	NAME(SCL_LOW_2),
	NAME(SCL_HIGH_2),
	NAME(SCL_LOW_3),
	NAME(SCL_HIGH_3),
	NAME(SCL_LOW_4),
	NAME(SCL_HIGH_4),
	NAME(SCL_LOW_5),
	NAME(SCL_HIGH_5),
	NAME(SCL_LOW_6),
	NAME(SCL_HIGH_6),
	NAME(SCL_LOW_7),
	NAME(SCL_HIGH_7),
	NAME(SCL_LOW_ACK),
	NAME(SCL_HIGH_ACK),
	NAME(STOP),
};

static const char * const i2c_cycle_name[] = {
	NAME(READ),
	NAME(WRITE),
	NAME(ADDRESS),
};

#undef NAME

static const char *acknak(bool ack)
{
	return ack ? "ACK" : "NAK";
}

static volatile bool quit;

static void sighandler(int x)
{
	quit = true;
}

struct log_entry {
	enum i2c_state state;
	enum i2c_cycle cycle;
	uint8_t data;
	bool ack;
};

static struct log_entry log[512];
static int num_log;

static void add_log(enum i2c_state state,
		    enum i2c_cycle cycle,
		    uint8_t data, bool ack)
{
	log[num_log].state = state;
	log[num_log].cycle = cycle;
	log[num_log].data = data;
	log[num_log].ack = ack;
	num_log = (num_log + 1) & (ARRAY_SIZE(log) - 1);
}

static void print_log(void)
{
	int i;

	for (i = 0; i < num_log; i++) {
		printf("%s %s 0x%02x %s\n",
		       i2c_state_name[log[i].state],
		       i2c_cycle_name[log[i].cycle],
		       log[i].data, acknak(log[i].ack));
	}
	num_log = 0;
}

static void run_slave(void)
{
	enum i2c_state state = IDLE;
	enum i2c_cycle cycle;
	int data_idx;
	uint8_t data;
	int num_writes = 0;
	uint8_t slave_data[8] = {
		0xfa, 0x13, 0x00, 0xad, 0x23, 0x56, 0x34, 0xff,
	};

	gpio_reserved = reg_read() & (GPIO_CLOCK_PULLUP_DISABLE |
				      GPIO_DATA_PULLUP_DISABLE);


	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	setscl(1);
	setsda(1);

	for (;;) {
		switch (state) {
			bool sda_first;
			bool scl, sda;
			bool ack;
			int bit;
		case IDLE:
			data_idx = 0;

			add_log(state, 0, 0, false);

			do {
				sda = getsda();
				scl = getscl();
			} while (sda && scl);

			if (!sda && scl) {
				state = START;
				debug_state("state -> %s\n", i2c_state_name[state]);
			} else {
				fprintf(stderr,
					"invalid transition SDA=1,SCL=1 -> SDA=%d,SCL=%d\n",
					sda, scl);
				exit(1);
			}
			break;
		case START:
			cycle = ADDRESS;
			num_writes = 0;

			add_log(state, 0, 0, false);
			//printf("%s\n", i2c_state_name[state]);

			do {
				sda = getsda();
				scl = getscl();
			} while (!sda && scl);

			if (!sda && !scl) {
				setscl(0);
				state = SCL_LOW_0;
				debug_state("state -> %s\n", i2c_state_name[state]);
			} else if (sda && scl) {
				state = STOP;
				debug_state("state -> %s\n", i2c_state_name[state]);
			} else {
				fprintf(stderr,
					"invalid transition SDA=1,SCL=1 -> SDA=%d,SCL=%d\n",
					sda, scl);
				exit(1);
			}
			break;
		case SCL_LOW_0:
			if (cycle == READ)
				data = slave_data[data_idx];
			else
				data = 0;
			/* fall through */
		case SCL_LOW_1:
		case SCL_LOW_2:
		case SCL_LOW_3:
		case SCL_LOW_4:
		case SCL_LOW_5:
		case SCL_LOW_6:
		case SCL_LOW_7:
			bit = 7 - ((state - SCL_LOW_0) >> 1);

			switch (cycle) {
			case READ:
				setsda(data & (1 << bit));
				break;
			default:
				setsda(1);
				break;
			}
			add_log(state, cycle, data, false);

			usleep(low_period_us);
			setscl(1);

			do {
				scl = getscl();
			} while (!scl);

			state++;
			debug_state("state -> %s\n", i2c_state_name[state]);
			break;
		case SCL_HIGH_0:
		case SCL_HIGH_1:
		case SCL_HIGH_2:
		case SCL_HIGH_3:
		case SCL_HIGH_4:
		case SCL_HIGH_5:
		case SCL_HIGH_6:
		case SCL_HIGH_7:
			bit = 7 - ((state - SCL_HIGH_0) >> 1);

			sda_first = getsda();

			switch (cycle) {
			case READ:
				break;
			default:
				data |= sda_first << bit;
				debug("data after bit %d = %x\n", bit, data);
				break;
			}
			add_log(state, cycle, data, false);

			do {
				sda = getsda();
				scl = getscl();
			} while (scl && sda == sda_first);

			debug("SDA %d->%d, SCL=%d\n", sda_first, sda, scl);
			if (scl) {
				state = sda_first ? START : STOP;
			} else {
				setscl(0);
				state++;
			}
			debug_state("state -> %s\n", i2c_state_name[state]);
			break;
		case SCL_LOW_ACK:
			switch (cycle) {
			case WRITE:
				ack = num_writes == 0 &&
					data < sizeof(slave_data);
				if (ack)
					data_idx = data;
				setsda(!ack);
				//printf("WRITE %s\n", acknak(ack));
				break;
			case ADDRESS:
				ack = (data >> 1) == slave_address;
				setsda(!ack);
				//printf("ADDRESS %s\n", acknak(ack));
				break;
			case READ:
				setsda(1);
				break;
			}
			add_log(state, cycle, data, ack);

			usleep(low_period_us);
			setscl(1);

			do {
				scl = getscl();
			} while (!scl);

			state++;
			debug_state("state -> %s\n", i2c_state_name[state]);
			break;
		case SCL_HIGH_ACK:
			sda_first = getsda();

			switch (cycle) {
			case READ:
				ack = !sda_first;
				if (ack)
					data_idx = (data_idx + 1) % sizeof(slave_data);
				else
					/* release SDA after NAK */
					cycle = WRITE;
				//printf("READ 0x%02x (%s)\n", data, acknak(ack));
				break;
			case WRITE:
				//printf("WRITE 0x%02x\n", data);
				num_writes++;
				break;
			case ADDRESS:
				if (data & 1)
					cycle = READ;
				else
					cycle = WRITE;

				//printf("ADDRESS 0x%02x / %s\n", data >> 1, i2c_cycle_name[cycle]);
			}
			add_log(state, cycle, data, ack);

			do {
				sda = getsda();
				scl = getscl();
			} while (scl && sda == sda_first);

			debug("SDA %d->%d, SCL=%d\n", sda_first, sda, scl);
			if (scl) {
				state = sda_first ? START : STOP;
			} else {
				setscl(0);
				state = SCL_LOW_0;
			}
			debug_state("state -> %s\n", i2c_state_name[state]);
			break;
		case STOP:
			add_log(state, 0, 0, false);
			//printf("%s\n", i2c_state_name[state]);
			state = IDLE;
			print_log();
			break;
		}

		if (quit && state == IDLE)
			break;
	}

	reg_write(gpio_reserved);
}

static void usage(const char *name)
{
	printf("Usage: %s [-h][-s <slave address>][-g <gpio pin]\n"
	       " -h/--help                           Show this help\n"
	       " -s/--slave-address <slave address>  Set the slave address (default: 0x50)\n"
	       " -g/--gpio-pin <gpio pin>            Select the GPIO pin (default: A)\n"
	       " -l/--low-period <usecs>             SCL low period in usecs (default: 10)\n"
	       , name);
	exit(1);
}

int main(int argc, char *argv[])
{
	uint32_t devid;
	uint8_t gpio_pin = 0;

	for (;;) {
		static const struct option opts[] = {
			{ .name = "help", .val = 'h', },
			{ .name = "slave-address", .val = 's', },
			{ .name = "gpio-pin", .val = 'g', },
			{ .name = "low-period", .val = 'l', },
			{}
		};

		int c = getopt_long(argc, argv, "hs:g:l:", opts, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 's':
			slave_address = strtoul(optarg, NULL, 0);
			if (slave_address >= 0x80)
				usage(argv[0]);
			break;
		case 'g':
			if (optarg[0] >= 'a')
				gpio_pin = optarg[0] - 'a';
			else if (optarg[0] >= 'A')
				gpio_pin = optarg[0] - 'A';
			else
				usage(argv[0]);
			if (gpio_pin >= 8)
				usage(argv[0]);
			break;
		case 'l':
			low_period_us = strtoul(optarg, NULL, 0);
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	devid = intel_get_pci_device()->device_id;

	gpio_reg = GPIO(gpio_pin);

	if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))
		gpio_reg += 0x180000;
	else if (intel_gen(devid) >= 5)
		gpio_reg += 0xc0000;

	intel_register_access_init(intel_get_pci_device(), 0, -1);

	run_slave();

	intel_register_access_fini();

	return 0;
}
