/*
 * Copyright 2016 Jerry Jacobs. All rights reserved.
 * Use of this source code is governed by the MIT
 * license that can be found in the LICENSE file.
 */

/**
 * @file src/semihosting.c
 */
#include <stdio.h>
#include <stdbool.h>

#include <stlink2.h>
#include <stlink2-internal.h>

void stlink2_semihosting_op_sys_open(struct stlink2 *dev)
{
	(void)dev;
}

void stlink2_semihosting_op_sys_close(struct stlink2 *dev)
{
	(void)dev;
}

void stlink2_semihosting_op_sys_writec(struct stlink2 *dev)
{
	uint32_t data;

	stlink2_read_reg(dev, 1, &data);
	stlink2_read_debug32(dev, data, &data);
	/* @todo: don't care for now */
	(void)fwrite(&data, 1, 1, stdout);
}

void stlink2_semihosting_op_sys_write0(struct stlink2 *dev)
{
	union {
		uint32_t u32;
		uint8_t u8[4];
	} data;
	uint32_t addr;

	stlink2_read_reg(dev, 1, &addr);

	while (1) {
		stlink2_read_debug32(dev, addr, &data.u32);
		for (size_t b = 0; b < sizeof(data); b++) {
			if (data.u8[b] == 0)
				return;
			printf("%c", data.u8[b]);
		}
		addr += 4;
	}
}

bool stlink2_semihosting(struct stlink2 *dev)
{
	bool ret = false;
	uint32_t data;

	uint32_t pc;
	uint32_t r0;

	stlink2_read_reg(dev, 15, &pc);
	STLINK2_LOG_DEBUG(dev, "pc: 0x%08x\n", pc);
	stlink2_read_debug32(dev, pc, &data);
	STLINK2_LOG_DEBUG(dev, "pc at: 0x%08x\n", data);

	if ((data & 0x0000ffff) == 0xbeab) {
		stlink2_read_reg(dev, 0, &r0);
		switch (r0) {
		case STLINK2_SEMIHOSTING_OP_SYS_WRITEC:
			STLINK2_LOG_DEBUG(dev, "SYS_WRITEC\n");
			stlink2_semihosting_op_sys_writec(dev);
			break;
		case STLINK2_SEMIHOSTING_OP_SYS_WRITE0:
			STLINK2_LOG_DEBUG(dev, "SYS_WRITE0\n");
			stlink2_semihosting_op_sys_write0(dev);
			break;
		case STLINK2_SEMIHOSTING_OP_SYS_WRITE:
			break;
		case STLINK2_SEMIHOSTING_OP_SYS_FLEN:
			STLINK2_LOG_DEBUG(dev, "SYS_FLEN\n");
			break;
		case STLINK2_SEMIHOSTING_EXCEPTION:
			stlink2_read_reg(dev, 1, &data);
			printf("Exception: %08x\n", data);
			break;
		default:
			break;
		}
		ret = true;
	}

	return ret;
}
