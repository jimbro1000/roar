/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2008  Ciaran Anscomb
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "types.h"
#include "deltados.h"
#include "dragondos.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6821.h"
#include "rsdos.h"
#include "sam.h"
#include "tape.h"
#include "vdg.h"
#include "xroar.h"

static uint8_t *selected_rom;
static unsigned int map_type;
static unsigned int ram_page_bit;

unsigned int sam_register;

static unsigned int sam_vdg_base;
unsigned int sam_vdg_mode;
unsigned int sam_vdg_address;
static unsigned int sam_vdg_mod_xdiv;
static unsigned int sam_vdg_mod_ydiv;
unsigned int sam_vdg_mod_clear;
static unsigned int sam_vdg_xcount;
static unsigned int sam_vdg_ycount;
#ifdef VARIABLE_MPU_RATE
unsigned int sam_topaddr_cycles;
#endif

static unsigned int vdg_mod_xdiv[8] = { 1, 3, 1, 2, 1, 1, 1, 1 };
static unsigned int vdg_mod_ydiv[8] = { 12, 1, 3, 1, 2, 1, 1, 1 };
static unsigned int vdg_mod_clear[8] = { ~30, ~14, ~30, ~14, ~30, ~14, ~30, ~0 };

/* SAM Data Sheet,
 *   Figure 6 - Signal routing for address multiplexer */
static unsigned int ram_row_masks[4] = { 0x007f, 0x007f, 0x00ff, 0x00ff };
static int ram_col_shifts[4] = { 2, 1, 0, 0 };
static unsigned int ram_col_masks[4] = { 0x3f00, 0x7f00, 0xff00, 0xff00 };
static unsigned int ram_row_mask;
static unsigned int ram_col_shift;
static unsigned int ram_col_mask;
static unsigned int ram_ras1;
#define VRAM_TRANSLATE(a) ( \
		((a << ram_col_shift) & ram_col_mask) \
		| (a & ram_row_mask) \
		| (!(a & 0x4000) ? ram_ras1 : 0) \
	)
#define RAM_TRANSLATE(a) (VRAM_TRANSLATE(a) | ram_page_bit)

static void update_from_register(void);

void sam_init(void) {
}

void sam_reset(void) {
	sam_set_register(0);
	sam_vdg_fsync();
}

unsigned int sam_read_byte(unsigned int addr) {
	while (EVENT_PENDING(MACHINE_EVENT_LIST))
		DISPATCH_NEXT_EVENT(MACHINE_EVENT_LIST);
	addr &= 0xffff;
	if (addr < 0x8000 || (map_type && addr < 0xff00)) {
		/* RAM access */
		unsigned int ram_addr = RAM_TRANSLATE(addr);
		current_cycle += CPU_SLOW_DIVISOR;
		if (addr < machine_ram_size)
			return ram0[ram_addr];
		return 0x7e;
	}
	if (addr < 0xff00) {
		/* ROM access */
		current_cycle += sam_topaddr_cycles;
		return selected_rom[addr-0x8000];
	}
	if (addr < 0xff20) {
		/* PIA0 */
		current_cycle += CPU_SLOW_DIVISOR;
		if (IS_COCO) {
			return mc6821_read(&PIA0, addr & 3);
		} else {
			if ((addr & 4) == 0) return mc6821_read(&PIA0, addr & 3);
			/* Not yet implemented:
			if ((addr & 7) == 4) return serial_stuff;
			if ((addr & 7) == 5) return serial_stuff;
			if ((addr & 7) == 6) return serial_stuff;
			if ((addr & 7) == 7) return serial_stuff;
			*/
		}
		return 0x7e;
	}
	current_cycle += sam_topaddr_cycles;
	if (addr < 0xff40) {
		return mc6821_read(&PIA1, addr & 3);
	}
	if (addr < 0xff60) {
		if (!DOS_ENABLED)
			return 0x7e;
		if (IS_RSDOS) {
			/* CoCo floppy disk controller */
			return rsdos_read(addr);
		}
		if (IS_DRAGONDOS) {
			/* Dragon floppy disk controller */
			return dragondos_read(addr);
		}
		if (IS_DELTADOS) {
			/* Delta floppy disk controller */
			return deltados_read(addr);
		}
		return 0x7e;
	}
	if (addr < 0xffe0)
		return 0x7f;
	return rom0[addr-0xc000];
}

void sam_store_byte(unsigned int addr, unsigned int octet) {
	while (EVENT_PENDING(MACHINE_EVENT_LIST))
		DISPATCH_NEXT_EVENT(MACHINE_EVENT_LIST);
	addr &= 0xffff;
	if (addr < 0x8000 || (map_type && addr < 0xff00)) {
		/* RAM access */
		unsigned int ram_addr = RAM_TRANSLATE(addr);
		current_cycle += CPU_SLOW_DIVISOR;
		if (IS_DRAGON32 && addr >= 0x8000 && machine_ram_size <= 0x8000) {
			ram_addr &= 0x7fff;
			if (ram_addr < machine_ram_size)
				ram0[ram_addr] = rom0[addr & 0x7fff];
			return;
		}
		if (addr < machine_ram_size)
			ram0[ram_addr] = octet;
		return;
	}
	if (addr < 0xff00) {
		/* ROM access */
		current_cycle += sam_topaddr_cycles;
		return;
	}
	if (addr < 0xff20) {
		current_cycle += CPU_SLOW_DIVISOR;
		if (IS_COCO) {
			mc6821_write(&PIA0, addr & 3, octet);
		} else {
			if ((addr & 4) == 0) mc6821_write(&PIA0, addr & 3, octet);
			/* Not yet implemented:
			if ((addr & 7) == 4) serial_stuff;
			if ((addr & 7) == 5) serial_stuff;
			if ((addr & 7) == 6) serial_stuff;
			if ((addr & 7) == 7) serial_stuff;
			*/
		}
		return;
	}
	current_cycle += sam_topaddr_cycles;
	if (addr < 0xff40) {
		mc6821_write(&PIA1, addr & 3, octet);
		if ((addr & 3) == 2 && IS_DRAGON64 && !map_type) {
			/* Update ROM select on Dragon 64 */
			selected_rom = (PIA1.b.port_output & 0x04) ? rom0 : rom1;
		}
		return;
	}
	if (addr < 0xff60) {
		if (!DOS_ENABLED)
			return;
		if (IS_RSDOS) {
			/* CoCo floppy disk controller */
			rsdos_write(addr, octet);
		}
		if (IS_DRAGONDOS) {
			/* Dragon floppy disk controller */
			dragondos_write(addr, octet);
		}
		if (IS_DELTADOS) {
			/* Delta floppy disk controller */
			deltados_write(addr, octet);
		}
		return;
	}
	if (addr < 0xffc0)
		return;
	if (addr < 0xffe0) {
		addr -= 0xffc0;
		if (addr & 1)
			sam_register |= 1 << (addr>>1);
		else
			sam_register &= ~(1 << (addr>>1));
		update_from_register();
	}
}

void sam_vdg_fsync(void) {
	sam_vdg_address = sam_vdg_base;
	sam_vdg_xcount = 0;
	sam_vdg_ycount = 0;
}

uint8_t *sam_vdg_bytes(int number) {
	unsigned int addr = VRAM_TRANSLATE(sam_vdg_address);
	unsigned int b15_5 = sam_vdg_address & ~0x1f;
	unsigned int b4 = sam_vdg_address & 0x10;
	unsigned int b3_0 = (sam_vdg_address & 0xf) + number;
	if (b3_0 & 0x10) {
		b3_0 &= 0x0f;
		sam_vdg_xcount++;
		if (sam_vdg_xcount >= sam_vdg_mod_xdiv) {
			sam_vdg_xcount = 0;
			b4 += 0x10;
			if (b4 & 0x20) {
				b4 &= 0x10;
				sam_vdg_ycount++;
				if (sam_vdg_ycount >= sam_vdg_mod_ydiv) {
					sam_vdg_ycount = 0;
					b15_5 += 0x20;
					b15_5 &= 0xffff;
				}
			}
		}
	}
	sam_vdg_address = b15_5 | b4 | b3_0;
	return &ram0[addr];
}

void sam_set_register(unsigned int value) {
	sam_register = value;
	update_from_register();
}

static void update_from_register(void) {
	int memory_size = (sam_register >> 13) & 3;
	sam_vdg_mode = sam_register & 0x0007;
	sam_vdg_base = (sam_register & 0x03f8) << 6;
	sam_vdg_mod_xdiv = vdg_mod_xdiv[sam_vdg_mode];
	sam_vdg_mod_ydiv = vdg_mod_ydiv[sam_vdg_mode];
	sam_vdg_mod_clear = vdg_mod_clear[sam_vdg_mode];

	ram_row_mask = ram_row_masks[memory_size];
	ram_col_shift = ram_col_shifts[memory_size];
	ram_col_mask = ram_col_masks[memory_size];
	switch (memory_size) {
		case 0: /* 4K */
		case 1: /* 16K */
			ram_page_bit = 0;
			ram_ras1 = 0x8080;
			break;
		default: /* 64K */
			ram_page_bit = (sam_register & 0x0400) << 5;
			ram_ras1 = 0;
			break;
	}

	if ((map_type = sam_register & 0x8000)) {
		/* Map type 1 */
#ifdef VARIABLE_MPU_RATE
		sam_topaddr_cycles = CPU_SLOW_DIVISOR;
#endif
	} else {
		/* Map type 0 */
		if (IS_DRAGON64 && !(PIA1.b.port_output & 0x04))
			selected_rom = rom1;
		else
			selected_rom = rom0;
#ifdef VARIABLE_MPU_RATE
		sam_topaddr_cycles = (sam_register & 0x0800) ? CPU_FAST_DIVISOR : CPU_SLOW_DIVISOR;
#endif
	}
}
