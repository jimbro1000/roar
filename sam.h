/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2009  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef __SAM_H__
#define __SAM_H__

#include "types.h"

extern unsigned int sam_vdg_address;
extern unsigned int sam_vdg_mod_clear;

static inline void sam_vdg_hsync(void) {
	sam_vdg_address &= sam_vdg_mod_clear;
}

void sam_init(void);
void sam_reset(void);
unsigned int sam_read_byte(unsigned int addr);
void sam_store_byte(unsigned int addr, unsigned int octet);
void sam_nvma_cycles(int cycles);
void sam_vdg_fsync(void);
uint8_t *sam_vdg_bytes(int number);
void sam_set_register(unsigned int value);
unsigned int sam_get_register(void);

#endif  /* __SAM_H__ */
