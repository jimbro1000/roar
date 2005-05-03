/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2005  Ciaran Anscomb
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

#include "config.h"
#include "types.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "pia.h"
#include "sound.h"
#include "vdg.h"
#include "wd2797.h"
#include "sam.h"

uint8_t *addrptr_low;
uint8_t *addrptr_high;
uint_least16_t mapped_ram;
uint_least16_t sam_page1;

uint_least16_t sam_register;

uint_least16_t sam_vdg_base;
unsigned int  sam_vdg_mode;
uint_least16_t sam_vdg_address;
unsigned int  sam_vdg_mod_xdiv;
unsigned int  sam_vdg_mod_ydiv;
uint_least16_t sam_vdg_mod_clear;
unsigned int  sam_vdg_xcount;
unsigned int  sam_vdg_ycount;

static unsigned int vdg_mod_xdiv[8] = { 1, 3, 1, 2, 1, 1, 1, 1 };
static unsigned int vdg_mod_ydiv[8] = { 12, 1, 3, 1, 2, 1, 1, 1 };
static uint_least16_t vdg_mod_clear[8] = { ~30, ~14, ~30, ~14, ~30, ~14, ~30, ~0 };

void sam_init(void) {
}

void sam_reset(void) {
	sam_register = 0;
	sam_update_from_register();
	sam_vdg_fsync();
}

unsigned int sam_read_byte(uint_least16_t addr) {
	addr &= 0xffff;
	if (addr < 0x8000) { return addrptr_low[addr]; }
	if (addr < 0xff00) {
		if (mapped_ram) return ram1[addr-0x8000];
		if (IS_DRAGON64 && !(PIA_1B.port_output & 0x04))
			return rom1[addr-0x8000];
		return rom0[addr-0x8000];
	}
	if (addr < 0xff20) {
		if (IS_COCO) {
			if ((addr & 3) == 0) return PIA_READ(PIA_0A);
			if ((addr & 3) == 1) return PIA_CONTROL_READ(PIA_0A);
			if ((addr & 3) == 2) return PIA_READ(PIA_0B);
			if ((addr & 3) == 3) return PIA_CONTROL_READ(PIA_0B);
		} else {
			if ((addr & 7) == 0) return PIA_READ(PIA_0A);
			if ((addr & 7) == 1) return PIA_CONTROL_READ(PIA_0A);
			if ((addr & 7) == 2) return PIA_READ(PIA_0B);
			if ((addr & 7) == 3) return PIA_CONTROL_READ(PIA_0B);
			/* Not yet implemented:
			if ((addr & 7) == 4) return serial_stuff;
			if ((addr & 7) == 5) return serial_stuff;
			if ((addr & 7) == 6) return serial_stuff;
			if ((addr & 7) == 7) return serial_stuff;
			*/
		}
		return 0x7f;
	}
	if (addr < 0xff40) {
		if ((addr & 3) == 0) return PIA_READ(PIA_1A);
		if ((addr & 3) == 1) return PIA_CONTROL_READ(PIA_1A);
		if ((addr & 3) == 2) return PIA_READ(PIA_1B);
		return PIA_CONTROL_READ(PIA_1B);
	}
	if (addr < 0xff60 && dragondos_enabled) {
		/* WD2797 Floppy Disk Controller */
		if ((addr & 15) == 0) return wd2797_status_read();
		if ((addr & 15) == 1) return wd2797_track_register_read();
		if ((addr & 15) == 2) return wd2797_sector_register_read();
		if ((addr & 15) == 3) return wd2797_data_register_read();
		//if (addr & 8) return wd2797_ff48_read();
		return 0x7e;
	}
	if (addr >= 0xffe0) { return rom0[addr-0xc000]; }
	return 0x7f;
}

void sam_store_byte(uint_least16_t addr, unsigned int octet) {
	addr &= 0xffff;
	if (addr < 0x8000) { addrptr_low[addr] = octet; return; }
	if (addr < 0xff00) {
		if (mapped_ram) ram1[addr-0x8000] = octet;
		return;
	}
	if (addr < 0xff20) {
		if (IS_COCO) {
			if ((addr & 3) == 0) PIA_WRITE_P0DA(octet);
			if ((addr & 3) == 1) PIA_CONTROL_WRITE(PIA_0A, octet);
			if ((addr & 3) == 2) PIA_WRITE_P0DB(octet);
			if ((addr & 3) == 3) PIA_CONTROL_WRITE(PIA_0B, octet);
		} else {
			if ((addr & 7) == 0) PIA_WRITE_P0DA(octet);
			if ((addr & 7) == 1) PIA_CONTROL_WRITE(PIA_0A, octet);
			if ((addr & 7) == 2) PIA_WRITE_P0DB(octet);
			if ((addr & 7) == 3) PIA_CONTROL_WRITE(PIA_0B, octet);
			/* Not yet implemented:
			if ((addr & 7) == 4) serial_stuff;
			if ((addr & 7) == 5) serial_stuff;
			if ((addr & 7) == 6) serial_stuff;
			if ((addr & 7) == 7) serial_stuff;
			*/
		}
		return;
	}
	if (addr < 0xff40) {
		if ((addr & 3) == 0) PIA_WRITE_P1DA(octet);
		if ((addr & 3) == 1) PIA_CONTROL_WRITE(PIA_1A, octet);
		if ((addr & 3) == 2) PIA_WRITE_P1DB(octet);
		if ((addr & 3) == 3) PIA_CONTROL_WRITE(PIA_1B, octet);
		return;
	}
	if (addr < 0xff60 && dragondos_enabled) {
		/* WD2797 Floppy Disk Controller */
		if ((addr & 15) == 0) wd2797_command_write(octet);
		if ((addr & 15) == 1) wd2797_track_register_write(octet);
		if ((addr & 15) == 2) wd2797_sector_register_write(octet);
		if ((addr & 15) == 3) wd2797_data_register_write(octet);
		if (addr & 8) wd2797_ff48_write(octet);
		return;
	}
	if (addr >= 0xffc0 && addr < 0xffe0) {
		addr -= 0xffc0;
		if (addr & 1)
			sam_register |= 1 << (addr>>1);
		else
			sam_register &= ~(1 << (addr>>1));
		sam_update_from_register();
	}
}

void sam_update_from_register(void) {
	sam_vdg_mode = sam_register & 0x0007;
	sam_vdg_base = (sam_register & 0x03f8) << 6;
	sam_vdg_mod_xdiv = vdg_mod_xdiv[sam_vdg_mode];
	sam_vdg_mod_ydiv = vdg_mod_ydiv[sam_vdg_mode];
	sam_vdg_mod_clear = vdg_mod_clear[sam_vdg_mode];
	if ((sam_page1 = sam_register & 0x0400)) {
		addrptr_low = ram1;
		addrptr_high = rom0;
		mapped_ram = 0;
	} else {
		addrptr_low = ram0;
		if ((mapped_ram = sam_register & 0x8000))
			addrptr_high = ram1;
		else
			addrptr_high = rom0;
	}
	//addrptr_low = ram0;
	return;
}
