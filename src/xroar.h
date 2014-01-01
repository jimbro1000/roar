/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2014  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_XROAR_H_
#define XROAR_XROAR_H_

#include <stdint.h>

#include "config.h"

#include "xconfig.h"

struct event;
struct machine_config;
struct cart;
struct vdg_palette;

/* Convenient values for arguments to helper functions */
#define XROAR_OFF    (0)
#define XROAR_ON     (1)
#define XROAR_CLEAR  (0)
#define XROAR_SET    (1)
#define XROAR_FALSE  (0)
#define XROAR_TRUE   (1)
#define XROAR_AUTO   (-1)
#define XROAR_TOGGLE (-2)
#define XROAR_CYCLE  (-3)

#define FILETYPE_UNKNOWN (0)
#define FILETYPE_VDK (1)
#define FILETYPE_JVC (2)
#define FILETYPE_DMK (3)
#define FILETYPE_BIN (4)
#define FILETYPE_HEX (5)
#define FILETYPE_CAS (6)
#define FILETYPE_WAV (7)
#define FILETYPE_SNA (8)
#define FILETYPE_ROM (9)
#define FILETYPE_ASC (10)

extern const char *xroar_disk_exts[];
extern const char *xroar_tape_exts[];
extern const char *xroar_snap_exts[];
extern const char *xroar_cart_exts[];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

enum xroar_run_state {
	xroar_run_state_stopped = 0,
	xroar_run_state_single_step,
	xroar_run_state_running
};

extern enum xroar_run_state xroar_run_state;

#define XROAR_SIGINT (2)
#define XROAR_SIGILL (4)
#define XROAR_SIGTRAP (5)
#define XROAR_SIGFPE (8)

extern int xroar_last_signal;

/**************************************************************************/
/* Command line arguments */

#define XROAR_GL_FILTER_NEAREST (0)
#define XROAR_GL_FILTER_LINEAR  (1)

struct xroar_cfg {
	/* Emulator interface */
	// Video
	char *geometry;
	int gl_filter;
	_Bool fullscreen;
	int frameskip;
	int ccr;
	_Bool vdg_inverted_text;
	// Audio
	char *ao_device;
	int ao_rate;
	int ao_channels;
	int ao_fragments;
	int ao_fragment_ms;
	int ao_fragment_nframes;
	int ao_buffer_ms;
	int ao_buffer_nframes;
#ifndef FAST_SOUND
	_Bool fast_sound;
#endif
	// Keyboard
	char *keymap;
	_Bool kbd_translate;
	// Cartridges
	_Bool becker;
	char *becker_ip;
	char *becker_port;
	// Disks
	_Bool disk_write_back;
	_Bool disk_jvc_hack;
	// CRC lists
	_Bool force_crc_match;
	// GDB target
	char *gdb_ip;
	char *gdb_port;
	// Debugging
	int trace_enabled;
	unsigned debug_ui;
	unsigned debug_file;
	unsigned debug_fdc;
	unsigned debug_gdb;
};

extern struct xroar_cfg xroar_cfg;

/* Emulator interface */

extern struct xconfig_enum xroar_cross_colour_list[];

/**************************************************************************/
/* Global flags */

/* NTSC cross-colour can either be rendered as a simple four colour palette,
 * or with a 5-bit lookup table */
#define CROSS_COLOUR_SIMPLE (0)
#define CROSS_COLOUR_5BIT   (1)

extern _Bool xroar_noratelimit;
extern int xroar_frameskip;

extern const char *xroar_rom_path;

#define UI_EVENT_LIST xroar_ui_events
#define MACHINE_EVENT_LIST xroar_machine_events
extern struct event *xroar_ui_events;
extern struct event *xroar_machine_events;

extern struct machine_config *xroar_machine_config;
extern struct cart *xroar_cart;
extern struct vdg_palette *xroar_vdg_palette;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debug flags

// UI: keyboard event debugging
#define XROAR_DEBUG_UI_KBD_EVENT (1 << 0)

// Files: binary files & hex record metadata
#define XROAR_DEBUG_FILE_BIN (1 << 0)
// Files: binary files & hex record data
#define XROAR_DEBUG_FILE_BIN_DATA (1 << 1)

// FDC: state debug level mask (1 = commands, 2 = all)
#define XROAR_DEBUG_FDC_STATE (3 << 0)
// FDC: dump sector data flag
#define XROAR_DEBUG_FDC_DATA (1 << 2)
// FDC: dump becker data flag
#define XROAR_DEBUG_FDC_BECKER (1 << 3)

// GDB: connections
#define XROAR_DEBUG_GDB_CONNECT (1 << 0)
// GDB: packets
#define XROAR_DEBUG_GDB_PACKET (1 << 1)
// GDB: report bad checksums
#define XROAR_DEBUG_GDB_CHECKSUM (1 << 2)
// GDB: queries and sets
#define XROAR_DEBUG_GDB_QUERY (1 << 3)

/**************************************************************************/

void xroar_getargs(int argc, char **argv);
_Bool xroar_init(int argc, char **argv);
void xroar_shutdown(void);

_Bool xroar_run(void);
void xroar_machine_continue(void);
void xroar_machine_signal(int sig);
void xroar_machine_single_step(void);
void xroar_machine_trap(void *data);

int xroar_filetype_by_ext(const char *filename);
int xroar_load_file_by_type(const char *filename, int autorun);

/* Helper functions */
void xroar_set_trace(int mode);
void xroar_new_disk(int drive);
void xroar_insert_disk_file(int drive, const char *filename);
void xroar_insert_disk(int drive);
void xroar_eject_disk(int drive);
_Bool xroar_set_write_enable(_Bool notify, int drive, int action);
_Bool xroar_set_write_back(_Bool notify, int drive, int action);
void xroar_set_cross_colour(_Bool notify, int action);
void xroar_set_vdg_inverted_text(_Bool notify, int action);
void xroar_quit(void);
void xroar_set_fullscreen(_Bool notify, int action);
void xroar_load_file(const char **exts);
void xroar_run_file(const char **exts);
void xroar_set_keymap(int map);
void xroar_set_kbd_translate(_Bool notify, int kbd_translate);
void xroar_set_machine(int machine);
void xroar_toggle_cart(void);
void xroar_set_cart(const char *cc_name);
void xroar_set_dos(int dos_type);  /* for old snapshots only */
void xroar_save_snapshot(void);
void xroar_select_tape_input(void);
void xroar_eject_tape_input(void);
void xroar_select_tape_output(void);
void xroar_eject_tape_output(void);
void xroar_hard_reset(void);
void xroar_soft_reset(void);

#endif  /* XROAR_XROAR_H_ */
