/*  Copyright 2003-2012 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Sources:
 *     WD279x:
 *         http://www.swtpc.com/mholley/DC_5/TMS279X_DataSheet.pdf
 */

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "crc16.h"
#include "events.h"
#include "logging.h"
#include "machine.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

#define STATUS_NOT_READY     (1<<7)
#define STATUS_WRITE_PROTECT (1<<6)
#define STATUS_HEAD_LOADED   (1<<5)
#define STATUS_RECORD_TYPE   (1<<5)
#define STATUS_SEEK_ERROR    (1<<4)
#define STATUS_RNF           (1<<4)
#define STATUS_CRC_ERROR     (1<<3)
#define STATUS_TRACK_0       (1<<2)
#define STATUS_LOST_DATA     (1<<2)
#define STATUS_INDEX_PULSE   (1<<1)
#define STATUS_DRQ           (1<<1)
#define STATUS_BUSY          (1<<0)

#define W_MILLISEC(ms) ((OSCILLATOR_RATE/1000)*(ms))
#define W_MICROSEC(us) ((OSCILLATOR_RATE*(us))/1000000)

#define W_BYTE_TIME (OSCILLATOR_RATE / 31250)

#define SET_DRQ do { \
		status_register |= STATUS_DRQ; \
		if (wd279x_set_drq_handler) \
			wd279x_set_drq_handler(); \
	} while (0)
#define RESET_DRQ do { \
		status_register &= ~(STATUS_DRQ); \
		if (wd279x_reset_drq_handler) \
			wd279x_reset_drq_handler(); \
	} while (0)
#define SET_INTRQ do { \
		if (wd279x_set_intrq_handler) \
			wd279x_set_intrq_handler(); \
	} while (0)
#define RESET_INTRQ do { \
		if (wd279x_reset_intrq_handler) \
			wd279x_reset_intrq_handler(); \
	} while (0)

#define NEXT_STATE(f,t) do { \
		state = f; \
		state_event.at_cycle = current_cycle + t; \
		event_queue(&MACHINE_EVENT_LIST, &state_event); \
	} while (0)
#define GOTO_STATE(f) state = f; continue

#define IS_DOUBLE_DENSITY (density)
#define IS_SINGLE_DENSITY (!density)

#define SET_DIRECTION do { direction = 1; vdrive_set_direction(1); } while (0)
#define RESET_DIRECTION do { \
		direction = -1; vdrive_set_direction(-1); \
	} while (0)
#define SET_SIDE(s) do { \
		side = (s) ? 1 : 0; \
		if (HAS_SSO) \
			vdrive_set_side(side); \
	} while (0)

/* From enum WD279X_type */
int wd279x_type;

#define HAS_SSO (wd279x_type == WD2795 || wd279x_type == WD2797)
#define HAS_LENGTH_FLAG (wd279x_type == WD2795 || wd279x_type == WD2797)
#define INVERTED_DATA (wd279x_type == WD2791 || wd279x_type == WD2795)

/* Various signal handlers set by DOS cart reset code: */
void (*wd279x_set_drq_handler)(void);
void (*wd279x_reset_drq_handler)(void);
void (*wd279x_set_intrq_handler)(void);
void (*wd279x_reset_intrq_handler)(void);

/* FDC states: */
enum wd279x_state {
	accept_command,
	type_1_state_1,
	type_1_state_2,
	type_1_state_3,
	verify_track_state_1,
	verify_track_state_2,
	type_2_state_1,
	type_2_state_2,
	read_sector_state_1,
	read_sector_state_2,
	read_sector_state_3,
	write_sector_state_1,
	write_sector_state_2,
	write_sector_state_3,
	write_sector_state_4,
	write_sector_state_5,
	write_sector_state_6,
	type_3_state_1,
	read_address_state_1,
	read_address_state_2,
	read_address_state_3,
	write_track_state_1,
	write_track_state_2,
	write_track_state_2b,
	write_track_state_3
};

static void state_machine(void);
static event_t state_event;
static enum wd279x_state state;

/* WD279X registers */
static uint8_t status_register;
static uint8_t track_register;
static uint8_t sector_register;
static uint8_t data_register;

/* WD279X internal state */
static uint8_t command_register;
static _Bool is_step_cmd;
static int direction;
static int side;
static int step_delay;
static int index_holes_count;
static int bytes_left;
static int dam;
static uint8_t track_register_tmp;
static uint16_t crc;

static int stepping_rate[4] = { 6, 12, 20, 30 };
static int sector_size[2][4] = {
	{ 256, 512, 1024, 128 },
	{ 128, 256, 512, 1024 }
};

static _Bool density;

static uint8_t _vdrive_read(void) {
	uint8_t b = vdrive_read();
	crc = crc16_byte(crc, b);
	return b;
}

static void _vdrive_write(uint8_t b) {
	vdrive_write(b);
	crc = crc16_byte(crc, b);
}

#define VDRIVE_WRITE_CRC16 do { \
		uint16_t tmp = crc; \
		_vdrive_write(tmp >> 8); \
		_vdrive_write(tmp & 0xff); \
	} while (0)

void wd279x_init(void) {
	wd279x_type = WD2797;
	wd279x_set_drq_handler = NULL;
	wd279x_reset_drq_handler = NULL;
	wd279x_set_intrq_handler = NULL;
	wd279x_reset_intrq_handler = NULL;
	event_init(&state_event);
	state_event.dispatch = state_machine;
}

void wd279x_reset(void) {
	event_dequeue(&state_event);
	status_register = 0;
	track_register = 0;
	sector_register = 0;
	data_register = 0;
	command_register = 0;
	RESET_DIRECTION;
	SET_SIDE(0);
}

void wd279x_set_dden(_Bool dden) {
	density = dden;
	vdrive_set_dden(dden);
}

uint8_t wd279x_read(uint16_t A) {
	uint8_t D;
	switch (A & 3) {
		default:
		case 0:
			RESET_INTRQ;
			if (vdrive_ready)
				status_register &= ~STATUS_NOT_READY;
			else
				status_register |= STATUS_NOT_READY;
			if ((command_register & 0xf0) == 0xd0 || (command_register & 0x80) == 0x00) {
				if (vdrive_tr00)
					status_register |= STATUS_TRACK_0;
				else
					status_register &= ~STATUS_TRACK_0;
				if (vdrive_index_pulse)
					status_register |= STATUS_INDEX_PULSE;
				else
					status_register &= ~STATUS_INDEX_PULSE;
			}
			D = status_register;
			break;
		case 1:
			D = track_register;
			break;
		case 2:
			D = sector_register;
			break;
		case 3:
			RESET_DRQ;
			D = data_register;
			break;
	}
	if (INVERTED_DATA)
		return ~D;
	return D;
}

void wd279x_write(uint16_t A, uint8_t D) {
	if (INVERTED_DATA)
		D = ~D;
	switch (A & 3) {
		default:
		case 0:
			RESET_INTRQ;
			command_register = D;
			/* FORCE INTERRUPT */
			if ((command_register & 0xf0) == 0xd0) {
				LOG_DEBUG(4,"WD279X: CMD: Force interrupt (%01x)\n",command_register&0x0f);
				if ((command_register & 0x0f) == 0) {
					event_dequeue(&state_event);
					status_register &= ~(STATUS_BUSY);
					return;
				}
				if (command_register & 0x08) {
					event_dequeue(&state_event);
					status_register &= ~(STATUS_BUSY);
					SET_INTRQ;
					return;
				}
				return;
			}
			/* Ignore any other command if busy */
			if (status_register & STATUS_BUSY) {
				LOG_DEBUG(4,"WD279X: Command received while busy!\n");
				return;
			}
			state = accept_command;
			state_machine();
			break;
		case 1:
			track_register = D;
			break;
		case 2:
			sector_register = D;
			break;
		case 3:
			RESET_DRQ;
			data_register = D;
			break;
	}
}

/* One big state machine.  This is called from an event dispatch and from the
 * write command function. */

static void state_machine(void) {
	uint8_t *idam;
	uint8_t data;
	int i;
	for (;;) {
		switch (state) {

		case accept_command:
			/* 0xxxxxxx = RESTORE / SEEK / STEP / STEP-IN / STEP-OUT */
			if ((command_register & 0x80) == 0x00) {
				status_register |= STATUS_BUSY;
				status_register &= ~(STATUS_CRC_ERROR|STATUS_SEEK_ERROR);
				RESET_DRQ;
				step_delay = stepping_rate[command_register & 3];
				is_step_cmd = 0;
				if ((command_register & 0xe0) == 0x20) {
					LOG_DEBUG(4, "WD279X: CMD: Step\n");
					is_step_cmd = 1;
				} else if ((command_register & 0xe0) == 0x40) {
					LOG_DEBUG(4, "WD279X: CMD: Step-in\n");
					is_step_cmd = 1;
					SET_DIRECTION;
				} else if ((command_register & 0xe0) == 0x60) {
					LOG_DEBUG(4, "WD279X: CMD: Step-out\n");
					is_step_cmd = 1;
					RESET_DIRECTION;
				}
				if (is_step_cmd) {
					if (command_register & 0x10) {
						GOTO_STATE(type_1_state_2);
					}
					GOTO_STATE(type_1_state_3);
				}
				if ((command_register & 0xf0) == 0x00) {
					track_register = 0xff;
					data_register = 0x00;
					LOG_DEBUG(4, "WD279X: CMD: Restore\n");
				} else {
					LOG_DEBUG(4, "WD279X: CMD: Seek (TR=%d)\n", data_register);
				}
				GOTO_STATE(type_1_state_1);
			}

			/* 10xxxxxx = READ/WRITE SECTOR */
			if ((command_register & 0xc0) == 0x80) {
				if ((command_register & 0xe0) == 0x80) {
					LOG_DEBUG(4, "WD279X: CMD: Read sector (Tr %d, Side %d, Sector %d)\n", track_register, side, sector_register);
				} else {
					LOG_DEBUG(4, "WD279X: CMD: Write sector\n");
				}
				status_register |= STATUS_BUSY;
				status_register &= ~(STATUS_LOST_DATA|STATUS_RNF|(1<<5)|(1<<6));
				RESET_DRQ;
				if (!vdrive_ready) {
					status_register &= ~(STATUS_BUSY);
					SET_INTRQ;
					return;
				}
				if (HAS_SSO)
					SET_SIDE(command_register & 0x02);  /* 'U' */
				else
					SET_SIDE(command_register & 0x08);  /* 'S' */
				if (command_register & 0x04) {  /* 'E' set */
					NEXT_STATE(type_2_state_1, W_MILLISEC(30));
					return;
				}
				GOTO_STATE(type_2_state_1);
			}

			/* 11000xx0 = READ ADDRESS */
			/* 11100xx0 = READ TRACK */
			/* 11110xx0 = WRITE TRACK */
			if (((command_register & 0xf9) == 0xc0)
					|| ((command_register & 0xf9) == 0xe0)
					|| ((command_register & 0xf9) == 0xf0)) {
				status_register |= STATUS_BUSY;
				status_register &= ~(STATUS_LOST_DATA|(1<<4)|(1<<5));
				if ((command_register & 0xf0) == 0xf0)
					RESET_DRQ;
				if (!vdrive_ready) {
					status_register &= ~(STATUS_BUSY);
					SET_INTRQ;
					return;
				}
				if (HAS_SSO)
					SET_SIDE(command_register & 0x02);  /* 'U' */
				else
					SET_SIDE(command_register & 0x08);  /* 'S' */
				if (command_register & 0x04) {  /* 'E' set */
					NEXT_STATE(type_3_state_1, W_MILLISEC(30));
					return;
				}
				GOTO_STATE(type_3_state_1);
			}
			LOG_WARN("WD279X: CMD: Unknown command %02x\n", command_register);
			return;


		case type_1_state_1:
			LOG_DEBUG(5, " type_1_state_1()\n");
			if (data_register == track_register) {
				GOTO_STATE(verify_track_state_1);
			}
			if (data_register > track_register)
				SET_DIRECTION;
			else
				RESET_DIRECTION;
			/* GOTO_STATE(type_1_state_2); */
			/* fall through */


		case type_1_state_2:
			LOG_DEBUG(5, " type_1_state_2()\n");
			track_register += direction;
			/* GOTO_STATE(type_1_state_3); */
			/* fall through */


		case type_1_state_3:
			LOG_DEBUG(5, " type_1_state_3()\n");
			if (vdrive_tr00 && direction == -1) {
				LOG_DEBUG(4,"WD279X: TR00!\n");
				track_register = 0;
				GOTO_STATE(verify_track_state_1);
			}
			vdrive_step();
			if (is_step_cmd) {
				NEXT_STATE(verify_track_state_1, W_MILLISEC(step_delay));
				return;
			}
			NEXT_STATE(type_1_state_1, W_MILLISEC(step_delay));
			return;


		case verify_track_state_1:
			LOG_DEBUG(5, " verify_track_state_1()\n");
			if (!(command_register & 0x04)) {
				status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			index_holes_count = 0;
			NEXT_STATE(verify_track_state_2, vdrive_time_to_next_idam());
			return;


		case verify_track_state_2:
			LOG_DEBUG(5, " verify_track_state_2()\n");
			idam = vdrive_next_idam();
			if (vdrive_new_index_pulse()) {
				index_holes_count++;
				if (index_holes_count >= 5) {
					LOG_DEBUG(5, "index_holes_count >= 5: seek error\n");
					status_register &= ~(STATUS_BUSY);
					status_register |= STATUS_SEEK_ERROR;
					SET_INTRQ;
					return;
				}
			}
			if (idam == NULL) {
				LOG_DEBUG(5, "null IDAM: -> verify_track_state_2\n");
				NEXT_STATE(verify_track_state_2, vdrive_time_to_next_idam());
				return;
			}
			crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
			}
			(void)_vdrive_read();  /* Include IDAM in CRC */
			if (track_register != _vdrive_read()) {
				LOG_DEBUG(5, "track_register != idam[1]: -> verify_track_state_2\n");
				NEXT_STATE(verify_track_state_2, vdrive_time_to_next_idam());
				return;
			}
			/* Include rest of ID field - should result in computed CRC = 0 */
			for (i = 0; i < 5; i++)
				(void)_vdrive_read();
			if (crc != 0) {
				LOG_DEBUG(3, "Verify track %d CRC16 error: $%04x != 0\n", track_register, crc);
				status_register |= STATUS_CRC_ERROR;
				NEXT_STATE(verify_track_state_2, vdrive_time_to_next_idam());
				return;
			}
			LOG_DEBUG(5, "finished.\n");
			status_register &= ~(STATUS_CRC_ERROR|STATUS_BUSY);
			SET_INTRQ;
			return;


		case type_2_state_1:
			LOG_DEBUG(5, " type_2_state_1()\n");
			if ((command_register & 0x20) && vdrive_write_protect) {
				status_register &= ~(STATUS_BUSY);
				status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ;
				return;
			}
			index_holes_count = 0;
			NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
			return;


		case type_2_state_2:
			LOG_DEBUG(5, " type_2_state_2()\n");
			idam = vdrive_next_idam();
			if (vdrive_new_index_pulse()) {
				index_holes_count++;
				if (index_holes_count >= 5) {
					status_register &= ~(STATUS_BUSY);
					status_register |= STATUS_RNF;
					SET_INTRQ;
					return;
				}
			}
			if (idam == NULL) {
				NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
				return;
			}
			crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
			}
			(void)_vdrive_read();  /* Include IDAM in CRC */
			if (track_register != _vdrive_read()) {
				NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
				return;
			}
			if (side != (int)_vdrive_read()) {
				/* No error if no SSO or 'C' not set */
				if (HAS_SSO || command_register & 0x02) {
					NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
					return;
				}
			}
			if (sector_register != _vdrive_read()) {
				NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
				return;
			}
			i = _vdrive_read();
			if (HAS_LENGTH_FLAG)
				bytes_left = sector_size[(command_register & 0x08)?1:0][i&3];
			else
				bytes_left = sector_size[1][i&3];
			/* Including CRC bytes should result in computed CRC = 0 */
			(void)_vdrive_read();
			(void)_vdrive_read();
			if (crc != 0) {
				status_register |= STATUS_CRC_ERROR;
				LOG_DEBUG(3, "Type 2 tr %d se %d CRC16 error: $%04x != 0\n", track_register, sector_register, crc);
				NEXT_STATE(type_2_state_2, vdrive_time_to_next_idam());
				return;
			}

			if ((command_register & 0x20) == 0) {
				int bytes_to_scan, j, tmp;
				if (IS_SINGLE_DENSITY)
					bytes_to_scan = 30;
				else
					bytes_to_scan = 43;
				j = 0;
				dam = 0;
				do {
					crc = CRC16_RESET;
					if (IS_DOUBLE_DENSITY) {
						crc = crc16_byte(crc, 0xa1);
						crc = crc16_byte(crc, 0xa1);
						crc = crc16_byte(crc, 0xa1);
					}
					tmp = _vdrive_read();
					if (tmp == 0xfb || tmp == 0xf8)
						dam = tmp;
					j++;
				} while (j < bytes_to_scan && dam == 0);
				if (dam == 0) {
					NEXT_STATE(type_2_state_2, vdrive_time_to_next_byte());
					return;
				}
				NEXT_STATE(read_sector_state_1, vdrive_time_to_next_byte());
				return;
			}
			vdrive_skip();
			vdrive_skip();
			NEXT_STATE(write_sector_state_1, vdrive_time_to_next_byte());
			return;


		case read_sector_state_1:
			LOG_DEBUG(5, " read_sector_state_1()\n");
			LOG_DEBUG(4,"Reading %d-byte sector (Tr %d, Se %d) from head_pos=%04x\n", bytes_left, track_register, sector_register, vdrive_head_pos());
			status_register |= ((~dam & 1) << 5);
			data_register = _vdrive_read();
			bytes_left--;
			SET_DRQ;
			NEXT_STATE(read_sector_state_2, vdrive_time_to_next_byte());
			return;


		case read_sector_state_2:
			LOG_DEBUG(5, " read_sector_state_2()\n");
			if (status_register & STATUS_DRQ) {
				status_register |= STATUS_LOST_DATA;
				/* RESET_DRQ;  XXX */
			}
			if (bytes_left > 0) {
				data_register = _vdrive_read();
				bytes_left--;
				SET_DRQ;
				NEXT_STATE(read_sector_state_2, vdrive_time_to_next_byte());
				return;
			}
			/* Including CRC bytes should result in computed CRC = 0 */
			(void)_vdrive_read();
			(void)_vdrive_read();
			NEXT_STATE(read_sector_state_3, vdrive_time_to_next_byte());
			return;


		case read_sector_state_3:
			LOG_DEBUG(5, " read_sector_state_3()\n");
			if (crc != 0) {
				LOG_DEBUG(3, "Read sector data tr %d se %d CRC16 error: $%04x != 0\n", track_register, sector_register, crc);
				status_register |= STATUS_CRC_ERROR;
			}
			/* TODO: M == 1 */
			if (command_register & 0x10) {
				LOG_DEBUG(2, "WD279X: TODO: multi-sector read will fail.\n");
			}
			status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case write_sector_state_1:
			LOG_DEBUG(5, " write_sector_state_1()\n");
			SET_DRQ;
			for (i = 0; i < 8; i++)
				vdrive_skip();
			NEXT_STATE(write_sector_state_2, vdrive_time_to_next_byte());
			return;


		case write_sector_state_2:
			LOG_DEBUG(5, " write_sector_state_2()\n");
			if (status_register & STATUS_DRQ) {
				status_register &= ~(STATUS_BUSY);
				RESET_DRQ;  /* XXX */
				status_register |= STATUS_LOST_DATA;
				SET_INTRQ;
				return;
			}
			vdrive_skip();
			NEXT_STATE(write_sector_state_3, vdrive_time_to_next_byte());
			return;


		case write_sector_state_3:
			LOG_DEBUG(5, " write_sector_state_3()\n");
			if (IS_DOUBLE_DENSITY) {
				for (i = 0; i < 11; i++)
					vdrive_skip();
				for (i = 0; i < 12; i++)
					_vdrive_write(0);
				NEXT_STATE(write_sector_state_4, vdrive_time_to_next_byte());
				return;
			}
			for (i = 0; i < 6; i++)
				_vdrive_write(0);
			NEXT_STATE(write_sector_state_4, vdrive_time_to_next_byte());
			return;


		case write_sector_state_4:
			LOG_DEBUG(5, " write_sector_state_4()\n");
			crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
			}
			if (command_register & 1)
				_vdrive_write(0xf8);
			else
				_vdrive_write(0xfb);
			NEXT_STATE(write_sector_state_5, vdrive_time_to_next_byte());
			return;


		case write_sector_state_5:
			data = data_register;
			LOG_DEBUG(5, " write_sector_state_5()\n");
			if (status_register & STATUS_DRQ) {
				data = 0;
				status_register |= STATUS_LOST_DATA;
				RESET_DRQ;  /* XXX */
			}
			_vdrive_write(data);
			bytes_left--;
			if (bytes_left > 0) {
				SET_DRQ;
				NEXT_STATE(write_sector_state_5, vdrive_time_to_next_byte());
				return;
			}
			VDRIVE_WRITE_CRC16;
			NEXT_STATE(write_sector_state_6, vdrive_time_to_next_byte() + W_MICROSEC(20));
			return;


		case write_sector_state_6:
			LOG_DEBUG(5, " write_sector_state_6()\n");
			_vdrive_write(0xfe);
			/* TODO: M = 1 */
			status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case type_3_state_1:
			LOG_DEBUG(5, " type_3_state_1()\n");
			switch (command_register & 0xf0) {
				case 0xc0:
					index_holes_count = 0;
					NEXT_STATE(read_address_state_1, vdrive_time_to_next_idam());
					return;
				case 0xe0:
					LOG_WARN("WD279X: CMD: Read track not implemented\n");
					SET_INTRQ;
					break;
				case 0xf0:
					LOG_DEBUG(4, "WD279X: CMD: Write track (Tr %d)\n", track_register);
					GOTO_STATE(write_track_state_1);
				default:
					LOG_WARN("WD279X: CMD: Unknown command %02x\n", command_register);
					break;
			}
			return;


		case read_address_state_1:
			LOG_DEBUG(5, " read_address_state_1()\n");
			idam = vdrive_next_idam();
			if (vdrive_new_index_pulse()) {
				index_holes_count++;
				if (index_holes_count >= 6) {
					status_register &= ~(STATUS_BUSY);
					status_register |= STATUS_RNF;
					SET_INTRQ;
					return;
				}
			}
			if (idam == NULL) {
				NEXT_STATE(read_address_state_1, vdrive_time_to_next_idam());
				return;
			}
			crc = CRC16_RESET;
			if (IS_DOUBLE_DENSITY) {
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
			}
			(void)_vdrive_read();
			NEXT_STATE(read_address_state_2, vdrive_time_to_next_byte());
			return;


		case read_address_state_2:
			bytes_left = 5;
			data_register = _vdrive_read();
			/* At end of command, this is transferred to the sector register: */
			track_register_tmp = data_register;
			SET_DRQ;
			NEXT_STATE(read_address_state_3, vdrive_time_to_next_byte());
			return;


		case read_address_state_3:
			/* Lost data not mentioned in data sheet, so not checking
			   for now */
			if (bytes_left > 0) {
				data_register = _vdrive_read();
				bytes_left--;
				SET_DRQ;
				NEXT_STATE(read_address_state_3, vdrive_time_to_next_byte());
				return;
			}
			sector_register = track_register_tmp;
			if (crc != 0) {
				status_register |= STATUS_CRC_ERROR;
			}
			status_register &= ~(STATUS_BUSY);
			SET_INTRQ;
			return;


		case write_track_state_1:
			LOG_DEBUG(5, " write_track_state_1()\n");
			if (vdrive_write_protect) {
				status_register &= ~(STATUS_BUSY);
				status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ;
				return;
			}
			SET_DRQ;
			/* Data sheet says 3 byte times, but CoCo NitrOS9 fails unless I set
			 * this delay higher. */
			NEXT_STATE(write_track_state_2, 6 * W_BYTE_TIME);
			return;


		case write_track_state_2:
			LOG_DEBUG(5, " write_track_state_2()\n");
			if (status_register & STATUS_DRQ) {
				RESET_DRQ;  /* XXX */
				status_register |= STATUS_LOST_DATA;
				status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			NEXT_STATE(write_track_state_2b, vdrive_time_to_next_idam());
			return;


		case write_track_state_2b:
			LOG_DEBUG(5, " write_track_state_2b()\n");
			if (!vdrive_new_index_pulse()) {
				LOG_DEBUG(4,"Waiting for index pulse, head_pos=%04x\n", vdrive_head_pos());
				NEXT_STATE(write_track_state_2b, vdrive_time_to_next_idam());
				return;
			}
			LOG_DEBUG(4,"Writing track from head_pos=%04x\n", vdrive_head_pos());
			/* GOTO_STATE(write_track_state_3); */
			/* fall through */


		case write_track_state_3:
			data = data_register;
			LOG_DEBUG(5, " write_track_state_3()\n");
			if (vdrive_new_index_pulse()) {
				LOG_DEBUG(4,"Finished writing track at head_pos=%04x\n", vdrive_head_pos());
				RESET_DRQ;  /* XXX */
				status_register &= ~(STATUS_BUSY);
				SET_INTRQ;
				return;
			}
			if (status_register & STATUS_DRQ) {
				data = 0;
				status_register |= STATUS_LOST_DATA;
			}
			SET_DRQ;
			if (IS_SINGLE_DENSITY) {
				/* Single density */
				if (data == 0xf5 || data == 0xf6) {
					LOG_DEBUG(4, "Illegal value in single-density track write: %02x\n", data);
				}
				if (data == 0xf7) {
					VDRIVE_WRITE_CRC16;
					NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
					return;
				}
				if (data >= 0xf8 && data <= 0xfb) {
					crc = CRC16_RESET;
					_vdrive_write(data);
					NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
					return;
				}
				if (data == 0xfe) {
					LOG_DEBUG(4,"IDAM at head_pos=%04x\n", vdrive_head_pos());
					crc = CRC16_RESET;
					vdrive_write_idam();
					crc = crc16_byte(crc, 0xfe);
					NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
					return;
				}
				_vdrive_write(data);
				NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
				return;
			}
			/* Double density */
			if (data == 0xf7) {
				VDRIVE_WRITE_CRC16;
				NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
				return;
			}
			if (data == 0xfe) {
				LOG_DEBUG(4,"IDAM at head_pos=%04x\n", vdrive_head_pos());
				vdrive_write_idam();
				crc = crc16_byte(crc, 0xfe);
				NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
				return;
			}
			if (data == 0xf5) {
				crc = CRC16_RESET;
				crc = crc16_byte(crc, 0xa1);
				crc = crc16_byte(crc, 0xa1);
				_vdrive_write(0xa1);
				NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
				return;
			}
			if (data == 0xf6) {
				data = 0xc2;
			}
			_vdrive_write(data);
			NEXT_STATE(write_track_state_3, vdrive_time_to_next_byte());
			return;

		}
	}
}
