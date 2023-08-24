/** \file
 *
 *  \brief Support for various binary representations.
 *
 *  \copyright Copyright 2003-2022 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  Supports:
 *
 *  - Intel HEX
 *
 *  - DragonDOS binary
 *
 *  - CoCo RS-DOS ("DECB") binary
 */

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "delegate.h"

#include "debug_cpu.h"
#include "fs.h"
#include "hexs19.h"
#include "logging.h"
#include "machine.h"
#include "xroar.h"

static int dragon_bin_load(FILE *fd, int autorun);
static int coco_bin_load(FILE *fd, int autorun);

static uint8_t read_nibble(FILE *fd) {
	int in;
	in = fs_read_uint8(fd);
	if (in >= '0' && in <= '9')
		return (in-'0');
	in |= 0x20;
	if (in >= 'a' && in <= 'f')
		return (in-'a')+10;
	return 0xff;
}

static uint8_t read_byte(FILE *fd) {
	return read_nibble(fd) << 4 | read_nibble(fd);
}

static uint16_t read_word(FILE *fd) {
	return read_byte(fd) << 8 | read_byte(fd);
}

static int skip_eol(FILE *fd) {
	int d;
	do {
		d = fs_read_uint8(fd);
	} while (d >= 0 && d != 10);
	if (d >= 0)
		return 1;
	return 0;
}

int intel_hex_read(const char *filename, int autorun) {
	FILE *fd;
	int data;
	uint16_t exec = 0;
	struct log_handle *log_hex = NULL;
	if (filename == NULL)
		return -1;
	if (!(fd = fopen(filename, "rb")))
		return -1;
	LOG_DEBUG(1, "Reading Intel HEX record file\n");
	if (logging.debug_file & LOG_FILE_BIN_DATA)
		log_open_hexdump(&log_hex, "Intel HEX read: ");
	while ((data = fs_read_uint8(fd)) >= 0) {
		if (data != ':') {
			fclose(fd);
			if (logging.debug_file & LOG_FILE_BIN_DATA) {
				log_hexdump_flag(log_hex);
				log_close(&log_hex);
			}
			return -1;
		}
		int length = read_byte(fd);
		int addr = read_word(fd);
		int type = read_byte(fd);
		if (type == 0 && (logging.debug_file & LOG_FILE_BIN_DATA))
			log_hexdump_set_addr(log_hex, addr);
		uint8_t rsum = length + (length >> 8) + addr + (addr >> 8) + type;
		for (int i = 0; i < length; i++) {
			data = read_byte(fd);
			rsum += data;
			if (type == 0) {
				if (logging.debug_file & LOG_FILE_BIN_DATA)
					log_hexdump_byte(log_hex, data);
				xroar.machine->write_byte(xroar.machine, addr & 0xffff, data);
				addr++;
			}
		}
		int sum = read_byte(fd);
		rsum = ~rsum + 1;
		if (sum != rsum) {
			if (logging.debug_file & LOG_FILE_BIN_DATA)
				log_hexdump_flag(log_hex);
		}
		if (skip_eol(fd) == 0)
			break;
		if (type == 1) {
			exec = addr;
			break;
		}
	}

	if (logging.debug_file & LOG_FILE_BIN_DATA)
		log_close(&log_hex);
	if (exec != 0) {
		struct debug_cpu *dcpu = NULL;
		if (autorun) {
			dcpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)xroar.machine, "CPU", "DEBUG-CPU");
		}
		if (autorun && dcpu) {
			LOG_DEBUG_FILE(LOG_FILE_BIN, "Intel HEX: EXEC $%04x - autorunning\n", exec);
			DELEGATE_CALL(dcpu->set_pc, exec);
		} else {
			LOG_DEBUG_FILE(LOG_FILE_BIN, "Intel HEX: EXEC $%04x - not autorunning\n", exec);
		}
	}

	fclose(fd);
	return 0;
}

int bin_load(const char *filename, int autorun) {
	FILE *fd;
	int type;
	if (filename == NULL)
		return -1;
	if (!(fd = fopen(filename, "rb")))
		return -1;
	type = fs_read_uint8(fd);
	switch (type) {
	case 0x55:
		return dragon_bin_load(fd, autorun);
	case 0x00:
		return coco_bin_load(fd, autorun);
	default:
		break;
	}
	LOG_DEBUG(1, "Unknown binary file type.\n");
	fclose(fd);
	return -1;
}

static int dragon_bin_load(FILE *fd, int autorun) {
	int filetype, load, exec;
	size_t length;
	LOG_DEBUG(1, "Reading Dragon BIN file\n");
	filetype = fs_read_uint8(fd);
	(void)filetype;  // XXX verify this makes sense
	load = fs_read_uint16(fd);
	length = fs_read_uint16(fd);
	exec = fs_read_uint16(fd);
	(void)fs_read_uint8(fd);
	LOG_DEBUG_FILE(LOG_FILE_BIN, "Dragon BIN: LOAD $%04zx bytes to $%04x, EXEC $%04x\n", length, load, exec);
	struct log_handle *log_bin = NULL;
	if (logging.debug_file & LOG_FILE_BIN_DATA) {
		log_open_hexdump(&log_bin, "Dragon BIN read: ");
		log_hexdump_set_addr(log_bin, load);
	}
	for (size_t i = 0; i < length; i++) {
		int data = fs_read_uint8(fd);
		if (data < 0) {
			log_hexdump_flag(log_bin);
			log_close(&log_bin);
			LOG_WARN("Dragon BIN: short read\n");
			break;
		}
		xroar.machine->write_byte(xroar.machine, (load + i) & 0xffff, data);
		log_hexdump_byte(log_bin, data);
	}
	log_close(&log_bin);
	struct debug_cpu *dcpu = NULL;
	if (autorun) {
		dcpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)xroar.machine, "CPU", "DEBUG-CPU");
	}
	if (autorun && dcpu) {
		LOG_DEBUG_FILE(LOG_FILE_BIN, "Dragon BIN: EXEC $%04x - autorunning\n", exec);
		DELEGATE_CALL(dcpu->set_pc, exec);
	} else {
		LOG_DEBUG_FILE(LOG_FILE_BIN, "Dragon BIN: EXEC $%04x - not autorunning\n", exec);
	}
	fclose(fd);
	return 0;
}

static int coco_bin_load(FILE *fd, int autorun) {
	size_t length;
	int chunk, load, exec;
	LOG_DEBUG(1, "Reading CoCo BIN file\n");
	fseek(fd, 0, SEEK_SET);
	while ((chunk = fs_read_uint8(fd)) >= 0) {
		if (chunk == 0) {
			length = fs_read_uint16(fd);
			load = fs_read_uint16(fd);
			LOG_DEBUG_FILE(LOG_FILE_BIN, "CoCo BIN: LOAD $%04zx bytes to $%04x\n", length, load);
			// Generate a hex dump per chunk
			struct log_handle *log_bin = NULL;
			if (logging.debug_file & LOG_FILE_BIN_DATA) {
				log_open_hexdump(&log_bin, "CoCo BIN: read: ");
				log_hexdump_set_addr(log_bin, load);
			}
			for (size_t i = 0; i < length; i++) {
				int data = fs_read_uint8(fd);
				if (data < 0) {
					log_hexdump_flag(log_bin);
					log_close(&log_bin);
					LOG_WARN("CoCo BIN: short read in data chunk\n");
					break;
				}
				xroar.machine->write_byte(xroar.machine, (load + i) & 0xffff, data);
				log_hexdump_byte(log_bin, data);
			}
			log_close(&log_bin);
			continue;
		} else if (chunk == 0xff) {
			(void)fs_read_uint16(fd);  // skip 0
			exec = fs_read_uint16(fd);
			if (exec < 0) {
				LOG_WARN("CoCo BIN: short read in exec chunk\n");
				break;
			}
			struct debug_cpu *dcpu = NULL;
			if (autorun) {
				dcpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)xroar.machine, "CPU", "DEBUG-CPU");
			}
			if (autorun && dcpu) {
				LOG_DEBUG_FILE(LOG_FILE_BIN, "CoCo BIN: EXEC $%04x - autorunning\n", exec);
				DELEGATE_CALL(dcpu->set_pc, exec);
			} else {
				LOG_DEBUG_FILE(LOG_FILE_BIN, "CoCo BIN: EXEC $%04x - not autorunning\n", exec);
			}
			break;
		} else {
			LOG_WARN("CoCo BIN: unknown chunk type 0x%02x\n", chunk);
			break;
		}
	}
	fclose(fd);
	return 0;
}
