/** \file
 *
 *  \brief XRoar initialisation and top-level emulator functions.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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
 */

#ifndef XROAR_XROAR_H_
#define XROAR_XROAR_H_

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>

#include "ui.h"
#include "xconfig.h"

struct ao_interface;
struct cart;
struct event;
struct machine_config;
struct slist;
struct vdg_palette;
struct vdrive_interface;
struct vo_interface;
struct xroar_timeout;

// Convenient values for arguments to helper functions
#define XROAR_QUERY (-4)  // query current setting
#define XROAR_PREV  (-3)
#define XROAR_NEXT  (-2)  // cycle or toggle setting
#define XROAR_AUTO  (-1)  // default, possible based on other settings
#define XROAR_OFF    (0)
#define XROAR_ON     (1)

enum xroar_filetype {
	FILETYPE_UNKNOWN,

	// Disk types
	FILETYPE_VDK,  // Often used for Dragon disks
	FILETYPE_JVC,  // Basic, with optional headers
	FILETYPE_OS9,  // JVC, but inspected to reveal OS9 metadata
	FILETYPE_DMK,  // David Keil's format records more information

	// Binary types
	FILETYPE_BIN,  // Generic ".bin", needs analysing for subtype
	FILETYPE_HEX,  // Intel HEX format

	// Cassette types
	FILETYPE_CAS,  // Simple bit format with optional CUE data
	FILETYPE_ASC,  // ASCII text, converted on-the-fly to CAS
	FILETYPE_K7,   // Logical blocks with header metadata
	FILETYPE_WAV,  // Audio sample

	// ROM images
	FILETYPE_ROM,  // Binary dump with optional header ("DGN")

	// Snapshots
	FILETYPE_SNA,  // Machine state dump, V1 or V2
	FILETYPE_RAM,  // Simple RAM dump (write only!)

	// HD images
	FILETYPE_VHD,  // 256 byte-per-sector image
	FILETYPE_IDE,  // 512 byte-per-sector image with header
	FILETYPE_IMG,  // Generic, heuristic decides if it's VHD or IDE
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Command line arguments

struct xroar_cfg {

	// Audio
	struct {
		char *device;
		int format;
		int rate;
		int channels;
		int fragments;
		int fragment_ms;
		int fragment_nframes;
		int buffer_ms;
		int buffer_nframes;
	} ao;

	// Keyboard
	struct {
		_Bool translate;
		struct slist *bind_list;
	} kbd;

	// Becker port
	struct {
		_Bool prefer;
		char *ip;
		char *port;
	} becker;

	// Files
	struct {
		char *rompath;
		char *hd[2];
	} file;

	// Cassettes
	struct {
		double pan;
		double hysteresis;
		int rewrite_gap_ms;
		int rewrite_leader;
	} tape;

	// Disks
	struct {
		_Bool write_back;
		_Bool auto_os9;
		_Bool auto_sd;
	} disk;

	// XXX this might make more sense as a per-machine option
	_Bool force_crc_match;

	// Debugging
	struct {
		_Bool gdb;
		char *gdb_ip;
		char *gdb_port;
		char *timeout_motoroff;
		char *snap_motoroff;
	} debug;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Global emulator state

struct xroar {
	struct xroar_cfg cfg;

	struct event *ui_events;
	struct event *machine_events;

	struct ui_interface *ui_interface;
	struct vo_interface *vo_interface;
	struct ao_interface *ao_interface;

	struct machine_config *machine_config;
	struct machine *machine;
	struct keyboard_interface *keyboard_interface;
	struct tape_interface *tape_interface;
	struct printer_interface *printer_interface;
	struct vdrive_interface *vdrive_interface;
};

extern struct xroar xroar;

#define UI_EVENT_LIST xroar.ui_events
#define MACHINE_EVENT_LIST xroar.machine_events

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/// Configure XRoar, initialise modules and start machine.
struct ui_interface *xroar_init(int argc, char **argv);

/// Cleanly shut down before program exit.
void xroar_shutdown(void);

/// Process UI event queue and run emulated machine.
void xroar_run(int ncycles);

int xroar_filetype_by_ext(const char *filename);
void xroar_load_file_by_type(const char *filename, int autorun);
void xroar_load_disk(const char *filename, int drive, _Bool autorun);

/* Scheduled shutdown */
struct xroar_timeout *xroar_set_timeout(char const *timestring);
void xroar_cancel_timeout(struct xroar_timeout *);

/* Helper functions */
void xroar_set_trace(int mode);
void xroar_new_disk(int drive);
void xroar_insert_disk_file(int drive, const char *filename);
void xroar_insert_disk(int drive);
void xroar_eject_disk(int drive);
_Bool xroar_set_write_enable(_Bool notify, int drive, int action);
_Bool xroar_set_write_back(_Bool notify, int drive, int action);
void xroar_set_ccr(_Bool notify, int action);
void xroar_set_tv_input(_Bool notify, int action);
void xroar_set_vdg_inverted_text(_Bool notify, int action);
void xroar_set_picture(_Bool notify, int action);
void xroar_set_ratelimit(int action);
void xroar_set_ratelimit_latch(_Bool notify, int action);
void xroar_set_pause(_Bool notify, int action);
FUNC_ATTR_NORETURN void xroar_quit(void);
void xroar_set_fullscreen(_Bool notify, int action);
void xroar_set_menubar(int action);
void xroar_load_file(const char * const *exts);
void xroar_run_file(const char * const *exts);
void xroar_set_keyboard_type(_Bool notify, int action);
void xroar_set_kbd_translate(_Bool notify, int kbd_translate);
void xroar_set_joystick(_Bool notify, int port, const char *name);
void xroar_swap_joysticks(_Bool notify);
void xroar_cycle_joysticks(_Bool notify);
void xroar_connect_machine(void);
void xroar_configure_machine(struct machine_config *mc);
void xroar_set_machine(_Bool notify, int id);
void xroar_update_cartridge_menu(void);
void xroar_toggle_cart(void);
void xroar_connect_cart(void);
void xroar_set_cart(_Bool notify, const char *cc_name);
void xroar_set_cart_by_id(_Bool notify, int id);
void xroar_save_snapshot(void);
void xroar_insert_input_tape_file(const char *filename);
void xroar_insert_input_tape(void);
void xroar_eject_input_tape(void);
void xroar_insert_output_tape_file(const char *filename);
void xroar_insert_output_tape(void);
void xroar_eject_output_tape(void);
void xroar_set_tape_playing(_Bool notify, _Bool play);
void xroar_hard_reset(void);
void xroar_soft_reset(void);
void xroar_screenshot(void);

/* Helper functions for config printing */
void xroar_cfg_print_inc_indent(void);
void xroar_cfg_print_dec_indent(void);
void xroar_cfg_print_indent(FILE *f);
void xroar_cfg_print_bool(FILE *f, _Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int(FILE *f, _Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int_nz(FILE *f, _Bool all, char const *opt, int value);
void xroar_cfg_print_double(FILE *f, _Bool all, char const *opt, double value, double normal);
void xroar_cfg_print_flags(FILE *f, _Bool all, char const *opt, unsigned value);
void xroar_cfg_print_string(FILE *f, _Bool all, char const *opt, char const *value,
			    char const *normal);
void xroar_cfg_print_enum(FILE *f, _Bool all, char const *opt, int value, int normal,
			  struct xconfig_enum const *e);
void xroar_cfg_print_string_list(FILE *f, _Bool all, char const *opt, struct slist *l);

#endif
