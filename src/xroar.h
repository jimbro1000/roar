/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_XROAR_H_
#define XROAR_XROAR_H_

#include <stdint.h>

#include "config.h"

#include "xconfig.h"

struct cart;
struct event;
struct machine_config;
struct slist;
struct vdg_palette;
struct xroar_timeout;

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

enum xroar_filetype {
	FILETYPE_UNKNOWN,
	FILETYPE_VDK,
	FILETYPE_JVC,
	FILETYPE_OS9,  // special type of JVC
	FILETYPE_DMK,
	FILETYPE_BIN,
	FILETYPE_HEX,
	FILETYPE_CAS,
	FILETYPE_WAV,
	FILETYPE_SNA,
	FILETYPE_ROM,
	FILETYPE_ASC,
};

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
	int ao_format;
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
	// Cassettes
	int tape_channel_mode;
	// Disks
	_Bool disk_write_back;
	_Bool disk_auto_os9;
	_Bool disk_auto_sd;
	// CRC lists
	_Bool force_crc_match;
	// Debugging
	_Bool gdb;
	char *gdb_ip;
	char *gdb_port;
	unsigned debug_gdb;
	_Bool trace_enabled;
	unsigned debug_ui;
	unsigned debug_file;
	unsigned debug_fdc;
	char *timeout_motoroff;
	char *snap_motoroff;
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
extern struct vdg_palette *xroar_vdg_palette;

extern struct vdrive_interface *xroar_vdrive_interface;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debug flags

// UI: keyboard event debugging
#define XROAR_DEBUG_UI_KBD_EVENT (1 << 0)

// Files: binary files & hex record metadata
#define XROAR_DEBUG_FILE_BIN (1 << 0)
// Files: binary files & hex record data
#define XROAR_DEBUG_FILE_BIN_DATA (1 << 1)
// Files: tape autorun filename block metadata
#define XROAR_DEBUG_FILE_TAPE_FNBLOCK (1 << 2)

// FDC: state debug level mask (1 = commands, 2 = all)
#define XROAR_DEBUG_FDC_STATE (3 << 0)
// FDC: dump sector data flag
#define XROAR_DEBUG_FDC_DATA (1 << 2)
// FDC: dump becker data flag
#define XROAR_DEBUG_FDC_BECKER (1 << 3)

/**************************************************************************/

void xroar_getargs(int argc, char **argv);
_Bool xroar_init(int argc, char **argv);
void xroar_shutdown(void);

_Bool xroar_run(void);

int xroar_filetype_by_ext(const char *filename);
int xroar_load_file_by_type(const char *filename, int autorun);

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
void xroar_set_cross_colour(_Bool notify, int action);
void xroar_set_vdg_inverted_text(_Bool notify, int action);
void xroar_quit(void);
void xroar_set_fullscreen(_Bool notify, int action);
void xroar_load_file(const char * const *exts);
void xroar_run_file(const char * const *exts);
void xroar_set_keymap(_Bool notify, int map);
void xroar_set_kbd_translate(_Bool notify, int kbd_translate);
void xroar_set_joystick(_Bool notify, int port, const char *name);
void xroar_swap_joysticks(_Bool notify);
void xroar_cycle_joysticks(_Bool notify);
void xroar_set_machine(_Bool notify, int id);
void xroar_toggle_cart(void);
void xroar_set_cart(_Bool notify, const char *cc_name);
void xroar_set_dos(int dos_type);  /* for old snapshots only */
void xroar_save_snapshot(void);
void xroar_select_tape_input(void);
void xroar_eject_tape_input(void);
void xroar_select_tape_output(void);
void xroar_eject_tape_output(void);
void xroar_hard_reset(void);
void xroar_soft_reset(void);

/* Helper functions for config printing */
void xroar_cfg_print_inc_indent(void);
void xroar_cfg_print_dec_indent(void);
void xroar_cfg_print_indent(void);
void xroar_cfg_print_bool(_Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int(_Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int_nz(_Bool all, char const *opt, int value);
void xroar_cfg_print_flags(_Bool all, char const *opt, unsigned value);
void xroar_cfg_print_string(_Bool all, char const *opt, char const *value,
			    char const *normal);
void xroar_cfg_print_enum(_Bool all, char const *opt, int value, int normal,
			  struct xconfig_enum const *e);
void xroar_cfg_print_string_list(_Bool all, char const *opt, struct slist *l);

#endif  /* XROAR_XROAR_H_ */
