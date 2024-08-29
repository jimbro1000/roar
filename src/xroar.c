/** \file
 *
 *  \brief XRoar initialisation and top-level emulator functions.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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

#include "top-config.h"

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WANT_GDB_TARGET
#include <pthread.h>
#endif

#include "array.h"
#include "c-strcase.h"
#include "pl-string.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "ao.h"
#include "auto_kbd.h"
#include "becker.h"
#include "cart.h"
#include "crclist.h"
#include "dkbd.h"
#include "events.h"
#include "fs.h"
#include "gdb.h"
#include "hexs19.h"
#include "hkbd.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "part.h"
#include "path.h"
#include "printer.h"
#include "romlist.h"
#include "screenshot.h"
#include "snapshot.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "vdg_palette.h"
#include "vdisk.h"
#include "vdrive.h"
#include "vo.h"
#include "wasm/wasm.h"
#include "xconfig.h"
#include "xroar.h"

#ifdef WINDOWS32
#include "windows32/common_windows32.h"
#endif

// Global emulator state

struct xroar xroar = {
	// Configuration directives
	.cfg = {
		.ao.fragments = -1,
		.tape.pan = 0.5,
		.tape.hysteresis = 1.0,
		.tape.rewrite_gap_ms = 500,
		.tape.rewrite_leader = 256,
		.disk.write_back = 1,
		.disk.auto_os9 = 1,
		.disk.auto_sd = 1,
	},
};

// Private

struct private_cfg {
	// Machines
	char *default_machine;
	struct {
		char *description;
		char *arch;
		int keymap;
		int cpu;
		char *palette;
		_Bool bas_dfn;
		char *bas;
		_Bool extbas_dfn;
		char *extbas;
		_Bool altbas_dfn;
		char *altbas;
		_Bool ext_charset_dfn;
		char *ext_charset;
		int tv_type;
		int tv_input;
		int vdg_type;
		int ram_org;
		int ram_init;
		_Bool cart_dfn;
		char *cart;
		int ram;
		struct slist *opts;
	} machine;

	// Cartridges
	struct {
		char *description;
		char *type;
		char *rom;
		char *rom2;
		int becker;
		int autorun;
		struct {
			int initial_slot;
			char *slot_cart_name[4];
		} mpi;
		struct slist *opts;
	} cart;

	// Cassettes
	struct {
		int fast;
		int pad_auto;
		int rewrite;
		int ao_rate;
	} tape;

	// Keyboard
	struct {
		struct slist *type_list;
	} kbd;

	// Files; to attach on startup
	struct {
		char *fd[4];  // floppy disks, one per drive
		struct slist *binaries;  // loaded in order
		char *tape;  // input tape
		char *tape_write;  // output tape
		char *text;  // text typed from file
		char *snapshot;  // snapshot
	} file;

	// User interface
	char *ui_module;

	// Video
	struct {
		int frameskip;
		int ccr;
		_Bool vdg_inverted_text;
		int picture;
		_Bool ntsc_scaling;
		int brightness;
		int contrast;
		int saturation;
		int hue;
	} vo;

	// Audio
	char *ao_module;
	struct {
		int volume;
		double gain;
	} ao;

	// Joysticks
	struct {
		char *description;
		char *axis[JOYSTICK_NUM_AXES];
		char *button[JOYSTICK_NUM_BUTTONS];
		char *right;
		char *left;
		char *virtual;
	} joy;

	// Printing
	struct {
		char *file;
		char *pipe;
	} printer;

	// Debugging
	struct {
		_Bool ratelimit;
		char *timeout;
	} debug;

#ifndef HAVE_WASM
	// Other options
	struct {
		_Bool joystick_print_list;
		_Bool config_print;
		_Bool config_print_all;
	} help;
#endif
};

static struct private_cfg private_cfg = {
	.machine.keymap = ANY_AUTO,
	.machine.cpu = CPU_MC6809,
	.machine.tv_type = ANY_AUTO,
	.machine.tv_input = ANY_AUTO,
	.machine.vdg_type = -1,
	.machine.ram_org = ANY_AUTO,
	.machine.ram_init = ANY_AUTO,
	.cart.becker = ANY_AUTO,
	.cart.autorun = ANY_AUTO,
	.cart.mpi.initial_slot = ANY_AUTO,
	.tape.fast = 1,
	.tape.pad_auto = 1,
	.vo.picture = ANY_AUTO,
	.vo.ntsc_scaling = 1,
	.vo.ccr = VO_CMP_CCR_5BIT,
	.vo.brightness = 52,
	.vo.contrast = 52,
	.vo.saturation = 50,
	// if volume set >=0, use that, else use gain value in dB
	.ao.gain = -3.0,
	.ao.volume = -1,
	.debug.ratelimit = 1,
};

static struct ui_cfg xroar_ui_cfg = {
	.vo_cfg = {
		.gl_filter = UI_GL_FILTER_AUTO,
#if __BYTE_ORDER == __BIG_ENDIAN
		.pixel_fmt = VO_RENDER_FMT_RGBA32,
#else
		.pixel_fmt = VO_RENDER_FMT_BGRA32,
#endif
	},
};

enum media_slot {
	media_slot_none = 0,
	media_slot_fd0,
	media_slot_fd1,
	media_slot_fd2,
	media_slot_fd3,
	media_slot_binary,
	media_slot_tape,
	media_slot_text,
	media_slot_cartridge,
	media_slot_snapshot,
};

static int autorun_media_slot = media_slot_none;

/* Helper functions used by configuration */
static void set_default_machine(const char *name);
static void set_machine(const char *name);
static void set_cart(const char *name);
static void add_load(const char *arg);
static void add_run(const char *arg);
static void set_gain(double gain);
static void set_kbd_bind(const char *spec);
static void set_joystick(const char *name);
static void set_joystick_axis(const char *spec);
static void set_joystick_button(const char *spec);

/* Help texts */
#ifndef HAVE_WASM
static void helptext(void);
static void versiontext(void);
static void config_print_all(FILE *f, _Bool all);
#endif

static int load_disk_to_drive = 0;

static struct joystick_config *cur_joy_config = NULL;

static struct xconfig_option const xroar_options[];

/**************************************************************************/
/* Global flags */

struct xroar_state {
	_Bool noratelimit_latch;
};

static struct xroar_state xroar_state = {
	.noratelimit_latch = 0,
};

static struct cart_config *selected_cart_config;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Default configuration */

static char const * const default_config[] = {
#ifdef WANT_MACHINE_ARCH_DRAGON
	// Dragon 32
	"machine dragon32",
	"machine-desc 'Dragon 32'",
	"machine-arch dragon32",
	"tv-type pal",
	"ram 32",
	// Dragon 64
	"machine dragon64",
	"machine-desc 'Dragon 64'",
	"machine-arch dragon64",
	"tv-type pal",
	"ram 64",
	// Tano Dragon
	"machine tano",
	"machine-desc 'Tano Dragon (NTSC)'",
	"machine-arch dragon64",
	"tv-type ntsc",
	"ram 64",
	// Dragon Professional (Alpha)
	"machine dragonpro",
	"machine-desc 'Dragon Professional (Alpha)'",
	"machine-arch dragonpro",
	"tv-type pal",
	"ram 64",
	// Dragon 200-E
	"machine dragon200e",
	"machine-desc 'Dragon 200-E'",
	"machine-arch dragon64",
	"machine-keyboard dragon200e",
	"extbas @dragon200e",
	"altbas @dragon200e_alt",
	"ext-charset @dragon200e_charset",
	"tv-type pal",
	"ram 64",
	// CoCo
	"machine coco",
	"machine-desc 'Tandy CoCo (PAL)'",
	"machine-arch coco",
	"tv-type pal",
	"ram 64",
	// CoCo (US)
	"machine cocous",
	"machine-desc 'Tandy CoCo (NTSC)'",
	"machine-arch coco",
	"tv-type ntsc",
	"ram 64",
	// CoCo 2B
	"machine coco2b",
	"machine-desc 'Tandy CoCo 2B (PAL,T1)'",
	"machine-arch coco",
	"tv-type pal",
	"vdg-type 6847t1",
	"ram 64",
	// CoCo 2B (US)
	"machine coco2bus",
	"machine-desc 'Tandy CoCo 2B (NTSC,T1)'",
	"machine-arch coco",
	"tv-type ntsc",
	"vdg-type 6847t1",
	"ram 64",
	// Deluxe CoCo
	"machine deluxecoco",
	"machine-desc 'Tandy Deluxe CoCo'",
	"machine-arch deluxecoco",
	"tv-type ntsc",
	"vdg-type 6847t1",
	"ram 64",
#endif

#ifdef WANT_MACHINE_ARCH_COCO3
	// CoCo 3
	"machine coco3",
	"machine-desc 'Tandy CoCo 3'",
	"machine-arch coco3",
	"tv-type ntsc",
	"vdg-type gime1986",
	"ram 512",
	// CoCo 3, 6309
	"machine coco3h",
	"machine-desc 'Tandy CoCo 3 (6309)'",
	"machine-arch coco3",
	"machine-cpu 6309",
	"tv-type ntsc",
	"vdg-type gime1986",
	"ram 512",
	// CoCo 3 PAL
	"machine coco3p",
	"machine-desc 'Tandy CoCo 3 (PAL)'",
	"machine-arch coco3",
	"tv-type pal",
	"vdg-type gime1986",
	"extbas @coco3p",
	"ram 512",
	// CoCo 3 PAL
	"machine coco3ph",
	"machine-desc 'Tandy CoCo 3 (PAL, 6309)'",
	"machine-arch coco3",
	"machine-cpu 6309",
	"tv-type pal",
	"vdg-type gime1986",
	"extbas @coco3p",
	"ram 512",
#endif

#ifdef WANT_MACHINE_ARCH_DRAGON
	// Dynacom MX-1600
	"machine mx1600",
	"machine-desc 'Dynacom MX-1600'",
	"machine-arch coco",
	"bas @mx1600",
	"extbas @mx1600ext",
	"tv-type pal-m",
	"ram 64",
#endif

#ifdef WANT_MACHINE_ARCH_MC10
	// MC-10
	"machine mc10",
	"machine-desc 'Tandy MC-10'",
	"machine-arch mc10",
	"tv-type ntsc",
	"bas @mc10",
	"ram 20",
	// Matra & Hachette Alice
	"machine alice",
	"machine-desc 'Matra & Hachette Alice'",
	"machine-arch mc10",
	"machine-keyboard alice",
	"tv-type pal",
	"bas @alice",
	"ram 20",
#endif

#ifdef WANT_CART_ARCH_DRAGON
	// DragonDOS
	"cart dragondos",
	"cart-desc DragonDOS",
	"cart-type dragondos",
	"cart-rom @dragondos_compat",
	// RSDOS
	"cart rsdos",
	"cart-desc RS-DOS",
	"cart-type rsdos",
	"cart-rom @rsdos",
	// Delta
	"cart delta",
	"cart-desc 'Delta System'",
	"cart-type delta",
	"cart-rom @delta",
#ifndef HAVE_WASM
	// RSDOS w/ Becker port
	"cart becker",
	"cart-desc 'RS-DOS with becker port'",
	"cart-type rsdos",
	"cart-rom @rsdos_becker",
	"cart-becker",
#endif
	// Games Master Cartridge
	"cart gmc",
	"cart-desc 'Games Master Cartridge'",
	"cart-type gmc",
	// Orchestra 90
	"cart orch90",
	"cart-desc 'Orchestra-90 CC'",
	"cart-type orch90",
	"cart-rom orch90",
	"cart-autorun",
#ifndef HAVE_WASM
	// Multi-Pak Interface
	"cart mpi",
	"cart-desc 'Multi-Pak Interface'",
	"cart-type mpi",
	// Multi-Pak Interface
	"cart mpi-race",
	"cart-desc 'RACE Computer Expansion Cage'",
	"cart-type mpi-race",
#endif
	// IDE Cartridge
	"cart ide",
	"cart-desc 'IDE Interface'",
	"cart-type ide",
	"cart-rom @glenside_ide",
	"cart-becker",
#ifndef HAVE_WASM
	// NX32 memory cartridge
	"cart nx32",
	"cart-desc 'NX32 memory cartridge'",
	"cart-type nx32",
	// MOOH memory cartridge
	"cart mooh",
	"cart-desc 'MOOH memory cartridge'",
	"cart-type mooh",
#endif
#endif

	// ROM lists

#ifdef WANT_MACHINE_ARCH_DRAGON
	// Fallback Dragon BASIC
	"romlist dragon=dragon",
	"romlist d64_1=d64_1,d64rom1,'Dragon Data Ltd - Dragon 64 - IC17','Dragon Data Ltd - TANO IC18','Eurohard S.A. - Dragon 200 IC18',dragrom",
	"romlist d64_2=d64_2,d64rom2,'Dragon Data Ltd - Dragon 64 - IC18','Dragon Data Ltd - TANO IC17','Eurohard S.A. - Dragon 200 IC17'",
	"romlist d32=d32,dragon32,d32rom,'Dragon Data Ltd - Dragon 32 - IC17'",
	"romlist d200e_1=d200e_1,d200e_rom1,ic18_v1.4e.ic34",
	"romlist d200e_2=d200e_2,d200e_rom2,ic17_v1.4e.ic37",
	// Specific Dragon BASIC
	"romlist dragon64=@d64_1,@dragon",
	"romlist dragon64_alt=@d64_2",
	"romlist dragon32=@d32,@dragon",
	"romlist dragon200e=@d200e_1,@d64_1,@dragon",
	"romlist dragon200e_alt=@d200e_2,@d64_2",
	"romlist dragon200e_charset=d200e_26,rom26.ic1",
	// Fallback CoCo BASIC
	"romlist coco=bas13,bas12,'Color Basic v1.2 (1982)(Tandy)',bas11,bas10",
	"romlist coco_ext=extbas11,extbas10,coco,COCO",
	// Specific CoCo BASIC
	"romlist coco1=bas10,@coco",
	"romlist coco1e=bas11,@coco",
	"romlist coco1e_ext=extbas10,@coco_ext",
	"romlist coco2=bas12,@coco",
	"romlist coco2_ext=extbas11,@coco_ext",
	"romlist coco2b=bas13,@coco",
	// MX-1600 and zephyr-patched version
	"romlist mx1600=mx1600bas,mx1600bas_zephyr",
	"romlist mx1600ext=mx1600extbas",
#endif

#ifdef WANT_MACHINE_ARCH_COCO3
	// CoCo 3 Super Extended Colour BASIC
	"romlist coco3=coco3",
	"romlist coco3p=coco3p",
	"romlist glenside_ide=yados,hdblba",
#endif

#ifdef WANT_MACHINE_ARCH_MC10
	// MC-10 BASIC
	"romlist mc10=mc10",
	// Alice BASIC
	"romlist alice=alice",
#endif

#ifdef WANT_CART_ARCH_DRAGON
	// DragonDOS
	"romlist dragondos=ddos12a,ddos12,ddos40,ddos15,ddos10,'Dragon Data Ltd - DragonDOS 1.0'",
	"romlist dosplus=dplus49b,dplus48,dosplus-4.8,DOSPLUS",
	"romlist superdos=sdose6,'PNP - SuperDOS E6',sdose5,sdose4",
	"romlist cumana=cdos20,CDOS20,'CumanaDOSv2.0'",
	"romlist dragondos_compat=@dosplus,@superdos,@dragondos,@cumana",
	// RSDOS
	"romlist rsdos=disk11,disk10",
	// Delta
	"romlist delta=delta2,delta1a,delta19,delta,deltados,'Premier Micros - DeltaDOS'",
#ifndef HAVE_WASM
	// RSDOS with becker port
	"romlist rsdos_becker=hdbdw3bck",
#endif
#endif

	// CRC lists

#ifdef WANT_MACHINE_ARCH_DRAGON
	// Dragon BASIC
	"crclist d64_1=0x84f68bf9,0x60a4634c,@woolham_d64_1",
	"crclist d64_2=0x17893a42,@woolham_d64_2",
	"crclist d32=0xe3879310,@woolham_d32",
	"crclist d200e_1=0x95af0a0a",
	"crclist dragon=@d64_1,@d32,@d200e_1",
	"crclist woolham_d64_1=0xee33ae92",
	"crclist woolham_d64_2=0x1660ae35",
	"crclist woolham_d32=0xff7bf41e,0x9c7eed69",
	// Dragon Pro
	"crclist dragonpro_boot=0xd6172b56,0xc3dab585",
	// CoCo BASIC
	"crclist bas10=0x00b50aaa",
	"crclist bas11=0x6270955a",
	"crclist bas12=0x54368805",
	"crclist bas13=0xd8f4d15e",
	"crclist mx1600=0xd918156e,0xd11b1c96",  // 2nd is zephyr-patched
	"crclist coco=@bas13,@bas12,@bas11,@bas10,@mx1600",
	"crclist extbas10=0xe031d076,0x6111a086",  // 2nd is corrupt dump
	"crclist extbas11=0xa82a6254",
	"crclist mx1600ext=0x322a3d58",
	"crclist cocoext=@extbas11,@extbas10,@mx1600ext",
	"crclist coco_combined=@mx1600",
	"crclist deluxecoco=0x1cce231e",
#endif

#ifdef WANT_MACHINE_ARCH_COCO3
	// CoCo 3 Super Extended Colour BASIC
	"crclist coco3=0xb4c88d6c,0xff050d80",
#endif

#ifdef WANT_MACHINE_ARCH_MC10
	// MC-10 BASIC
	"crclist mc10=0x11fda97e",
	// Alice BASIC
	"crclist alice=0xf876abe9",
	// Both
	"crclist mc10_compat=@mc10,@alice",
#endif

	// Joysticks
	"joy mjoy0",
	"joy-desc 'Mouse'",
	"joy-axis 0='mouse:'",
	"joy-axis 1='mouse:'",
	"joy-button 0='mouse:'",
	"joy-button 1='mouse:'",
	"joy kjoy0",
	"joy-desc 'Keyboard: Cursors+Alt_L,Super_L'",
	"joy-axis 0='keyboard:Left,Right'",
	"joy-axis 1='keyboard:Up,Down'",
	"joy-button 0='keyboard:Alt_L'",
	"joy-button 1='keyboard:Super_L'",
	"joy wasd",
	"joy-desc 'Keyboard: WASD+O,P'",
	"joy-axis 0='keyboard:a,d'",
	"joy-axis 1='keyboard:w,s'",
	"joy-button 0='keyboard:o'",
	"joy-button 1='keyboard:p'",
	"joy ijkl",
	"joy-desc 'Keyboard: IJKL+X,Z'",
	"joy-axis 0='keyboard:j,l'",
	"joy-axis 1='keyboard:i,k'",
	"joy-button 0='keyboard:x'",
	"joy-button 1='keyboard:z'",
	"joy qaop",
	"joy-desc 'Keyboard: QAOP+Space,['",
	"joy-axis 0='keyboard:o,p'",
	"joy-axis 1='keyboard:q,a'",
	"joy-button 0='keyboard:space'",
	"joy-button 1='keyboard:bracketleft'",
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void do_load_binaries(void *);

/*
// I will want these back in some form, but they've never been used yet, so
// they're commented out while I rejig how the file requesters work.

static char const * const xroar_disk_exts[] = { "DMK", "JVC", "OS9", "VDK", "DSK", NULL };
static char const * const xroar_tape_exts[] = { "CAS", "C10", "K7", NULL };
static char const * const xroar_snap_exts[] = { "SNA", NULL };
// static char const * const xroar_cart_exts[] = { "ROM", NULL };
#ifdef SCREENSHOT
static char const * const xroar_screenshot_exts[] = { "PNG", NULL };
#endif
*/

static struct {
	const char *ext;
	int filetype;
} const filetypes[] = {
	{ "VDK", FILETYPE_VDK },
	{ "JVC", FILETYPE_JVC },
	{ "DSK", FILETYPE_JVC },
	{ "OS9", FILETYPE_OS9 },
	{ "DMK", FILETYPE_DMK },
	{ "BIN", FILETYPE_BIN },
	{ "DGN", FILETYPE_BIN },
	{ "CCO", FILETYPE_BIN },
	{ "HEX", FILETYPE_HEX },
	{ "CAS", FILETYPE_CAS },
	{ "C10", FILETYPE_CAS },
	{ "K7",  FILETYPE_K7  },
	{ "WAV", FILETYPE_WAV },
	{ "SN",  FILETYPE_SNA },
	{ "RAM", FILETYPE_RAM },
	{ "ROM", FILETYPE_ROM },
	{ "CCC", FILETYPE_ROM },
	{ "BAS", FILETYPE_ASC },
	{ "ASC", FILETYPE_ASC },
	{ "VHD", FILETYPE_VHD },
	{ "IDE", FILETYPE_IDE },
	{ "IMG", FILETYPE_IMG },
	{ NULL, FILETYPE_UNKNOWN }
};

/**************************************************************************/

#ifndef ROMPATH
# define ROMPATH "."
#endif
#ifndef CONFPATH
# define CONFPATH "."
#endif

/** Processes options from a builtin list, a configuration file, and the
 * command line.  Determines which modules to use (see ui.h, vo.h, ao.h) and
 * initialises them.  Starts an emulated machine.
 *
 * Attaches any media images requested to the emulated machine and schedules
 * any deferred commands (e.g. autorunning a program, or user-specified "-type"
 * option).
 *
 * Returns the UI interface to the caller (probably main()).
 */

struct ui_interface *xroar_init(int argc, char **argv) {
	int argn = 1, ret;
	char *conffile = NULL;
	_Bool no_conffile = 0;
	_Bool no_builtin = 0;
#ifdef WINDOWS32
	_Bool alloc_console = 0;
#endif

	// Parse early options.  These affect how the rest of the config is
	// processed.  Also, for Windows, the -C option allocates a console so
	// that debug information can be seen, which we want to happen early.

	while (1) {
		if ((argn+1) < argc && 0 == strcmp(argv[argn], "-c")) {
			// -c, override conffile
			if (conffile)
				sdsfree(conffile);
			conffile = sdsnew(argv[argn+1]);
			argn += 2;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-c")) {
			// -no-c, disable conffile
			no_conffile = 1;
			argn++;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-builtin")) {
			// -no-builtin, disable builtin config
			no_builtin = 1;
			argn++;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-C")) {
#ifdef WINDOWS32
			// Windows allocate console option
			alloc_console = 1;
#endif
			argn++;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-C")) {
#ifdef WINDOWS32
			// Windows allocate console option
			alloc_console = 0;
#endif
			argn++;
		} else {
			break;
		}
	}

#ifdef WINDOWS32
	windows32_init(alloc_console);
#endif

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++)
		private_cfg.joy.axis[i] = NULL;
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++)
		private_cfg.joy.button[i] = NULL;

	// Parse default configuration.

	if (!no_builtin) {
		// Set a default ROM search path if required.
		char const *env = getenv("XROAR_ROM_PATH");
		if (!env)
			env = ROMPATH;
		if (env)
			xroar.cfg.file.rompath = xstrdup(env);
		// Process builtin directives
		for (unsigned i = 0; i < ARRAY_N_ELEMENTS(default_config); i++) {
			xconfig_parse_line(xroar_options, default_config[i]);
		}

		// Finish any machine or cart config in defaults.
		set_machine(NULL);
		set_cart(NULL);
		set_joystick(NULL);
	}
	// Don't auto-select last machine or cart in defaults.
	xroar.machine_config = NULL;
	selected_cart_config = NULL;
	cur_joy_config = NULL;

	// Finished processing default configuration.

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Parse config file, if found (and not disabled).

	if (!no_conffile) {
		const char *xroar_conf_path = getenv("XROAR_CONF_PATH");
		if (!xroar_conf_path) {
			xroar_conf_path = CONFPATH;
		}
		if (!conffile) {
			conffile = find_in_path(xroar_conf_path, "xroar.conf");
		}
		if (conffile) {
			(void)xconfig_parse_file(xroar_options, conffile);
			sdsfree(conffile);

			// Finish any machine or cart config in config file.
			set_machine(NULL);
			set_cart(NULL);
			set_joystick(NULL);
		}
	}
	// Don't auto-select last machine or cart in config file.
	xroar.machine_config = NULL;
	selected_cart_config = NULL;
	cur_joy_config = NULL;

	// Finished processing config file.

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Parse command line options.

	ret = xconfig_parse_cli(xroar_options, argc, argv, &argn);
	if (ret != XCONFIG_OK) {
		exit(EXIT_FAILURE);
	}

	// Unapplied machine options on the command line should apply to the
	// one we're going to pick to run, so decide that now.

	// If no machine specified on command line, get default.
	if (!xroar.machine_config && private_cfg.default_machine) {
		xroar.machine_config = machine_config_by_name(private_cfg.default_machine);
	}

	// If that didn't work, just find the first one that will work.
	if (!xroar.machine_config) {
		xroar.machine_config = machine_config_first_working();
	}

	// Otherwise, not much we can do, so exit.
	if (xroar.machine_config == NULL) {
		LOG_ERROR("Failed to start any machine.\n");
		exit(EXIT_FAILURE);
	}

	// Finish any machine or cart config on command line.
	set_machine(NULL);
	set_cart(NULL);
	set_joystick(NULL);

	// Remaining command line arguments are files, and we want to autorun
	// the first one if nothing already indicated to autorun.
	if (argn < argc) {
		if (autorun_media_slot == media_slot_none) {
			add_run(argv[argn++]);
		}
		while (argn < argc) {
			add_load(argv[argn++]);
		}
	}

	// Finished processing commmand line.

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Help text

	// Useful for -vo help to list the video modules within all available UIs
	if (xroar_ui_cfg.vo && 0 == strcmp(xroar_ui_cfg.vo, "help")) {
		ui_print_vo_help();
		exit(EXIT_SUCCESS);
	}
#ifndef HAVE_WASM
	if (private_cfg.help.config_print) {
		config_print_all(stdout, 0);
		exit(EXIT_SUCCESS);
	}
	if (private_cfg.help.config_print_all) {
		config_print_all(stdout, 1);
		exit(EXIT_SUCCESS);
	}
#endif

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Always create a vdrive interface (XXX but why here?)
	xroar.vdrive_interface = vdrive_interface_new();

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Select a UI module.
	struct ui_module *ui_module = (struct ui_module *)module_select_by_arg((struct module * const *)ui_module_list, private_cfg.ui_module);
	if (ui_module == NULL) {
		if (ui_module_list) {
			ui_module = ui_module_list[0];
		}
		if (ui_module) {
			LOG_WARN("UI module `%s' not found: trying '%s'\n", private_cfg.ui_module, ui_module->common.name);
		} else {
			LOG_ERROR("UI module `%s' not found\n", private_cfg.ui_module);
			exit(EXIT_FAILURE);
		}
	}
	// Override other module lists if UI has an entry.
	if (ui_module->ao_module_list != NULL)
		ao_module_list = ui_module->ao_module_list;
	// Select audio module
	struct module *ao_module = module_select_by_arg((struct module * const *)ao_module_list, private_cfg.ao_module);
	ui_joystick_module_list = ui_module->joystick_module_list;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Sanitise other command-line options.

	if (private_cfg.vo.frameskip < 0)
		private_cfg.vo.frameskip = 0;

	private_cfg.tape.pad_auto = private_cfg.tape.pad_auto ? TAPE_PAD_AUTO : 0;
	private_cfg.tape.fast = private_cfg.tape.fast ? TAPE_FAST : 0;
	private_cfg.tape.rewrite = private_cfg.tape.rewrite ? TAPE_REWRITE : 0;
	if (xroar.cfg.tape.rewrite_gap_ms <= 0 || xroar.cfg.tape.rewrite_gap_ms > 5000) {
		xroar.cfg.tape.rewrite_gap_ms = 500;
	}
	if (xroar.cfg.tape.rewrite_leader <= 0 || xroar.cfg.tape.rewrite_leader > 2048) {
		xroar.cfg.tape.rewrite_leader = 256;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Default to enabling default_cart (typically a DOS cart)
	_Bool auto_dos = 1;

	// Attaching a tape generally means we don't want a DOS.
	if (!selected_cart_config && private_cfg.file.tape) {
		auto_dos = 0;
	}

	// Although any disk loaded means we _do_ want a DOS
	for (int i = 0; i < 4; i++) {
		if (private_cfg.file.fd[i]) {
			auto_dos = 1;
		}
	}

	// TODO: if user loaded an SD or HD image, there are specific carts for
	// those too.

	// But wait, if there's a cartridge selected already, can't have a DOS.
	// Also if we're going to load a snapshot, it's all irrelevant.
	if (selected_cart_config || private_cfg.file.snapshot) {
		auto_dos = 0;
	}

	// And if user explicitly said no-machine-cart for this machine, we
	// should assume they mean it.
	if (xroar.machine_config->default_cart_dfn && !xroar.machine_config->default_cart) {
		auto_dos = 0;
	}

	// Disable cart in machine if none selected and we're not going to try
	// and find one.
	if (!selected_cart_config && !auto_dos) {
		xroar.machine_config->cart_enabled = 0;
	}

	// If any cart still configured, make it default for machine.
	if (selected_cart_config) {
		if (xroar.machine_config->default_cart) {
			free(xroar.machine_config->default_cart);
		}
		xroar.machine_config->default_cart = xstrdup(selected_cart_config->name);
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Initialise everything

	event_current_tick = 0;

#ifdef LOGGING
#ifndef HAVE_WASM
	// Unfortunately to print the list of joysticks, first the UI has to be
	// initialised.  Therefore in this special case, inhibit printing
	// anything else!
	if (private_cfg.help.joystick_print_list) {
		logging.level = 0;
	}
#endif
#endif

	// UI module
	xroar.ui_interface = module_init((struct module *)ui_module, &xroar_ui_cfg);
	if (!xroar.ui_interface || !xroar.ui_interface->vo_interface) {
		LOG_ERROR("No UI module initialised.\n");
		return NULL;
	}
	xroar.vo_interface = xroar.ui_interface->vo_interface;

	// Joysticks
	joystick_init();

#ifdef LOGGING
#ifndef HAVE_WASM
	if (private_cfg.help.joystick_print_list) {
		struct slist *jcl = joystick_config_list();
		while (jcl) {
			struct joystick_config *jc = jcl->data;
			jcl = jcl->next;
			printf("\t%-10s %s\n", jc->name, jc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif
#endif

	if (xroar.ui_interface) {
		DELEGATE_SAFE_CALL(xroar.ui_interface->update_joystick_menus);
	}

	// Audio module

	if (!(xroar.ao_interface = module_init_from_list(ao_module_list, ao_module, NULL))) {
		LOG_ERROR("No audio module initialised.\n");
		return NULL;
	}
	if (private_cfg.ao.volume >= 0) {
		sound_set_volume(xroar.ao_interface->sound_interface, private_cfg.ao.volume);
	} else {
		sound_set_gain(xroar.ao_interface->sound_interface, private_cfg.ao.gain);
	}

	// Default joystick mapping
	if (private_cfg.joy.right) {
		xroar_set_joystick(1, 0, private_cfg.joy.right);
	} else {
		xroar_set_joystick(1, 0, "joy0");
	}
	if (private_cfg.joy.left) {
		xroar_set_joystick(1, 1, private_cfg.joy.left);
	} else {
		xroar_set_joystick(1, 1, "joy1");
	}
	if (private_cfg.joy.virtual) {
		joystick_set_virtual(joystick_config_by_name(private_cfg.joy.virtual));
	} else {
		joystick_set_virtual(joystick_config_by_name("kjoy0"));
	}

	// Notify UI of starting options:
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_fullscreen, xroar_ui_cfg.vo_cfg.fullscreen, NULL);
	xroar_set_kbd_translate(1, xroar.cfg.kbd.translate);

	xroar.tape_interface = tape_interface_new(xroar.ui_interface);
	if (private_cfg.tape.ao_rate > 0)
		tape_set_ao_rate(xroar.tape_interface, private_cfg.tape.ao_rate);

	vo_set_ntsc_scaling(xroar.vo_interface, 1, private_cfg.vo.ntsc_scaling);
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, private_cfg.vo.brightness);
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, private_cfg.vo.contrast);
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, private_cfg.vo.saturation);
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_hue, private_cfg.vo.hue);
	vo_set_cmp_colour_killer(xroar.vo_interface, 1, xroar_ui_cfg.vo_cfg.colour_killer);

	// Configure machine
	xroar_configure_machine(xroar.machine_config);
	if (xroar.machine_config->cart_enabled) {
		xroar_set_cart(1, xroar.machine_config->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}

	// Reset everything
	xroar_hard_reset();
	tape_select_state(xroar.tape_interface, private_cfg.tape.fast | private_cfg.tape.pad_auto | private_cfg.tape.rewrite);

	xroar_set_vdg_inverted_text(1, private_cfg.vo.vdg_inverted_text);
	xroar_set_ratelimit_latch(1, private_cfg.debug.ratelimit);

	// Load media images

	if (private_cfg.file.snapshot) {
		// If we're loading a snapshot, loading other media doesn't
		// make sense (as it'll be overridden by the snapshot).
		read_snapshot(private_cfg.file.snapshot);
	} else {
		// Otherwise, attach any other media images.

		// Floppy disks
		for (int i = 0; i < 4; i++) {
			if (private_cfg.file.fd[i]) {
				_Bool autorun = (autorun_media_slot == (media_slot_fd0 + i));
				xroar_load_disk(private_cfg.file.fd[i], i, autorun);
			}
		}

		// Tapes
		if (private_cfg.file.tape) {
			int r;
			if (autorun_media_slot == media_slot_tape) {
				r = tape_autorun(xroar.tape_interface, private_cfg.file.tape);
			} else {
				r = tape_open_reading(xroar.tape_interface, private_cfg.file.tape);
			}
			if (r != -1) {
				DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_input_filename, 0, private_cfg.file.tape);
			}
		}

		// Text (type ASCII BASIC)
		if (private_cfg.file.text) {
			ak_type_file(xroar.auto_kbd, private_cfg.file.text);
			ak_parse_type_string(xroar.auto_kbd, "\\r");
			if (autorun_media_slot == media_slot_text) {
				ak_parse_type_string(xroar.auto_kbd, "RUN\\r");
			}
		}

		if (private_cfg.file.tape_write) {
			// Only write to CAS or WAV
			int write_file_type = xroar_filetype_by_ext(private_cfg.file.tape_write);
			switch (write_file_type) {
			case FILETYPE_CAS:
			case FILETYPE_K7:
			case FILETYPE_WAV:
				tape_open_writing(xroar.tape_interface, private_cfg.file.tape_write);
				DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_output_filename, 0, private_cfg.file.tape_write);
				break;
			default:
				break;
			}
		}

		// Binaries - delay loading by 2s
		if (private_cfg.file.binaries) {
			event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_load_binaries, NULL), EVENT_MS(2000));
		}
	}

	// Timeout (quit after certain amount of time)
	if (private_cfg.debug.timeout) {
		(void)xroar_set_timeout(private_cfg.debug.timeout);
	}

	// Type strings into machine
	while (private_cfg.kbd.type_list) {
		sds data = private_cfg.kbd.type_list->data;
		ak_type_sds(xroar.auto_kbd, data);
		private_cfg.kbd.type_list = slist_remove(private_cfg.kbd.type_list, data);
		sdsfree(data);
	}

#ifdef HAVE_WASM
	if (xroar.machine_config) {
		xroar_set_machine(1, xroar.machine_config->id);
	}
#endif
	return xroar.ui_interface;
}

/** Generally set as an atexit() handler by main(), this function flushes any
 * output, shuts down the emulated machine and frees any other allocated
 * resources.
 */

void xroar_shutdown(void) {
	static _Bool shutting_down = 0;
	if (shutting_down)
		return;
	shutting_down = 1;
	if (xroar.auto_kbd) {
		auto_kbd_free(xroar.auto_kbd);
		xroar.auto_kbd = NULL;
	}
	if (xroar.machine) {
		part_free((struct part *)xroar.machine);
		xroar.machine = NULL;
	}
	joystick_shutdown();
	cart_config_remove_all();
	machine_config_remove_all();
	xroar.machine_config = NULL;
	if (xroar.ao_interface) {
		DELEGATE_SAFE_CALL(xroar.ao_interface->free);
	}
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->free);
	}
	romlist_shutdown();
	crclist_shutdown();
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
		if (private_cfg.joy.axis[i])
			free(private_cfg.joy.axis[i]);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
		if (private_cfg.joy.button[i])
			free(private_cfg.joy.button[i]);
	}
	vdrive_interface_free(xroar.vdrive_interface);
	tape_interface_free(xroar.tape_interface);
	hk_shutdown();
	xconfig_shutdown(xroar_options);
	if (xroar.ui_interface) {
		DELEGATE_SAFE_CALL(xroar.ui_interface->free);
	}
#ifdef WINDOWS32
	windows32_shutdown();
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \param ncycles number of cycles to run.
 *
 * Called either by main() in a loop, or by a UI module's run().
 */

void xroar_run(int ncycles) {
	event_run_queue(&UI_EVENT_LIST);
	if (!xroar.machine)
		return;
	switch (xroar.machine->run(xroar.machine, ncycles)) {
	case machine_run_state_stopped:
		vo_refresh(xroar.vo_interface);
		break;
	case machine_run_state_ok:
	default:
		break;
	}
}

int xroar_filetype_by_ext(const char *filename) {
	char *ext;
	int i;
	ext = strrchr(filename, '.');
	if (ext == NULL)
		return FILETYPE_UNKNOWN;
	ext++;
	for (i = 0; filetypes[i].ext; i++) {
		if (!c_strncasecmp(ext, filetypes[i].ext, strlen(filetypes[i].ext)))
			return filetypes[i].filetype;
	}
	return FILETYPE_UNKNOWN;
}

void xroar_load_file_by_type(const char *filename, int autorun) {
	if (filename == NULL)
		return;
	int filetype = xroar_filetype_by_ext(filename);

	switch (filetype) {
	case FILETYPE_VDK:
	case FILETYPE_JVC:
	case FILETYPE_OS9:
	case FILETYPE_DMK:
		xroar_load_disk(filename, load_disk_to_drive, autorun);
		return;

	case FILETYPE_BIN:
		bin_load(filename, autorun);
		return;

	case FILETYPE_HEX:
		intel_hex_read(filename, autorun);
		return;

	case FILETYPE_SNA:
		read_snapshot(filename);
		return;

	case FILETYPE_ROM:
		{
			struct cart_config *cc;
			xroar.machine->remove_cart(xroar.machine);
			cc = cart_config_by_name(filename);
			if (cc) {
				cc->autorun = autorun;
				xroar_set_cart(1, cc->name);
				if (autorun) {
					xroar_hard_reset();
				}
			}
		}
		break;

	case FILETYPE_CAS:
	case FILETYPE_K7:
	case FILETYPE_ASC:
	case FILETYPE_WAV:
	default:
		if (filetype == FILETYPE_ASC && part_is_a(&xroar.machine->part, "mc10")) {
			ak_type_file(xroar.auto_kbd, filename);
			ak_parse_type_string(xroar.auto_kbd, "\\r");
			if (autorun) {
				ak_parse_type_string(xroar.auto_kbd, "RUN\\r");
			}
		} else {
			int r;
			if (autorun) {
				r = tape_autorun(xroar.tape_interface, filename);
			} else {
				r = tape_open_reading(xroar.tape_interface, filename);
			}
			if (r != -1) {
				DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_input_filename, 0, filename);
			}
		}
		break;
	}
}

// Simple binary files (or hex representations) are the only media where it
// makes sense to load more than one of them, so we process these as a list
// after machine has had time to start up.

static void do_load_binaries(void *sptr) {
	(void)sptr;
	for (struct slist *iter = private_cfg.file.binaries; iter; iter = iter->next) {
		char *filename = iter->data;
		_Bool autorun = (autorun_media_slot == media_slot_binary) && !iter->next;
		xroar_load_file_by_type(filename, autorun);
	}
	slist_free_full(private_cfg.file.binaries, (slist_free_func)free);
	private_cfg.file.binaries = NULL;
}

void xroar_load_disk(const char *filename, int drive, _Bool autorun) {
	if (drive < 0 || drive >= 4)
		drive = 0;
	xroar_insert_disk_file(drive, filename);
	if (autorun && vdrive_disk_in_drive(xroar.vdrive_interface, 0)) {
		/* TODO: more intelligent recognition of the type of DOS
		 * we're talking to */
		if (strcmp(xroar.machine->config->architecture, "coco") == 0
		    || strcmp(xroar.machine->config->architecture, "coco3") == 0) {
			ak_parse_type_string(xroar.auto_kbd, "\\025DOS\\r");
		} else {
			ak_parse_type_string(xroar.auto_kbd, "\\025BOOT\\r");
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct xroar_timeout {
	int seconds;
	int cycles;
	struct event event;
};

static void handle_timeout_event(void *sptr) {
	struct xroar_timeout *timeout = sptr;
	if (timeout->seconds == 0) {
		free(timeout);
		xroar_quit();
	}
	timeout->seconds--;
	if (timeout->seconds) {
		timeout->event.at_tick = event_current_tick + EVENT_S(1);
	} else {
		if (timeout->cycles == 0) {
			free(timeout);
			xroar_quit();
		}
		timeout->event.at_tick = event_current_tick + timeout->cycles;
	}
	event_queue(&MACHINE_EVENT_LIST, &timeout->event);
}

/* Configure a timeout (period after which emulator will exit). */

struct xroar_timeout *xroar_set_timeout(char const *timestring) {
	struct xroar_timeout *timeout = NULL;
	double t = strtod(timestring, NULL);
	if (t >= 0.0) {
		timeout = xmalloc(sizeof(*timeout));
		timeout->seconds = (int)t;
		timeout->cycles = EVENT_S(t - timeout->seconds);
		event_init(&timeout->event, DELEGATE_AS0(void, handle_timeout_event, timeout));
		/* handler can set up the first call for us... */
		timeout->seconds++;
		handle_timeout_event(timeout);
	}
	return timeout;
}

void xroar_cancel_timeout(struct xroar_timeout *timeout) {
	event_dequeue(&timeout->event);
	free(timeout);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Helper functions */

void xroar_set_trace(int mode) {
	(void)mode;
#ifdef TRACE
	switch (mode) {
	case XROAR_OFF: default:
		logging.trace_cpu = 0;
		break;
	case XROAR_ON:
		logging.trace_cpu = 1;
		break;
	case XROAR_NEXT:
		logging.trace_cpu = !logging.trace_cpu;
		break;
	}
#endif
}

void xroar_new_disk(int drive) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Create floppy image");
	if (filename == NULL)
		return;
	int filetype = xroar_filetype_by_ext(filename);
	xroar_eject_disk(drive);

	struct vdisk *new_disk = vdisk_new(VDISK_TRACK_LENGTH_DD300);
	if (new_disk == NULL) {
		LOG_WARN("Failed to create new disk\n");
		return;
	}
	switch (filetype) {
		case FILETYPE_VDK:
		case FILETYPE_JVC:
		case FILETYPE_OS9:
		case FILETYPE_DMK:
			break;
		default:
			filetype = FILETYPE_DMK;
			break;
	}
	new_disk->filetype = filetype;
	new_disk->filename = xstrdup(filename);
	new_disk->write_back = 1;
	new_disk->new_disk = 1;  // no need to back up disk file
	new_disk->dirty = 1;  // always write empty disk
	vdrive_insert_disk(xroar.vdrive_interface, drive, new_disk);
	vdisk_unref(new_disk);
	if (xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_disk_data, drive, new_disk);
	}
	LOG_DEBUG(1, "New unformatted disk in drive %d: %s\n", 1+drive, filename);
}

void xroar_insert_disk_file(int drive, const char *filename) {
	if (!filename) return;
	struct vdisk *disk = vdisk_load(filename);
	vdrive_insert_disk(xroar.vdrive_interface, drive, disk);
	if (disk) {
		vdisk_unref(disk);
	}
	if (xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_disk_data, drive, disk);
	}
}

void xroar_insert_disk(int drive) {
	struct vdisk *old_disk = vdrive_disk_in_drive(xroar.vdrive_interface, drive);
	if (old_disk) {
		vdisk_save(old_disk);
	}
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Load floppy image");
	xroar_insert_disk_file(drive, filename);
}

void xroar_eject_disk(int drive) {
	vdrive_eject_disk(xroar.vdrive_interface, drive);
	if (xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_disk_data, drive, NULL);
	}
}

_Bool xroar_set_write_enable(_Bool notify, int drive, int action) {
	assert(drive >= 0 && drive < 4);
	struct vdisk *vd = vdrive_disk_in_drive(xroar.vdrive_interface, drive);
	if (!vd)
		return 0;
	_Bool new_we = !vd->write_protect;
	switch (action) {
	case XROAR_NEXT:
		new_we = !new_we;
		break;
	default:
		new_we = action;
		break;
	}
	vd->write_protect = !new_we;
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_disk_write_enable, drive, (void *)(uintptr_t)new_we);
	}
	return new_we;
}

_Bool xroar_set_write_back(_Bool notify, int drive, int action) {
	assert(drive >= 0 && drive < 4);
	struct vdisk *vd = vdrive_disk_in_drive(xroar.vdrive_interface, drive);
	if (!vd)
		return 0;
	_Bool new_wb = vd->write_back;
	switch (action) {
	case XROAR_NEXT:
		new_wb = !new_wb;
		break;
	default:
		new_wb = action;
		break;
	}
	vd->write_back = new_wb;
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_disk_write_back, drive, (void *)(uintptr_t)new_wb);
	}
	return new_wb;
}

void xroar_insert_hd_file(int drive, const char *filename) {
	if (!filename)
		return;
	if (drive < 0 || drive > 1)
		return;
	if (xroar.cfg.file.hd[drive])
		free(xroar.cfg.file.hd[drive]);
	fprintf(stderr, "xroar.cfg.file.hd[%d] = '%s'\n", drive, filename);
	xroar.cfg.file.hd[drive] = xstrdup(filename);
}

void xroar_set_ccr(_Bool notify, int action) {
	if (action < 0 || action >= NUM_VO_CMP_CCR) {
		action = VO_CMP_CCR_PALETTE;
	}
	private_cfg.vo.ccr = action;
	vo_set_cmp_ccr(xroar.vo_interface, notify, private_cfg.vo.ccr);
}

void xroar_set_tv_input(_Bool notify, int action) {
	_Bool is_coco3 = (strcmp(xroar.machine_config->architecture, "coco3") == 0);

	if (action == XROAR_NEXT) {
		action = xroar.machine_config->tv_input + 1;
	}

	if (action < 0 ||
	    (!is_coco3 && action >= NUM_TV_INPUTS_DRAGON) ||
	    (is_coco3 && action >= NUM_TV_INPUTS_COCO3)) {
		action = TV_INPUT_SVIDEO;
		notify = 1;
	}

	xroar.machine_config->tv_input = action;

	switch (action) {
	default:
	case TV_INPUT_SVIDEO:
		vo_set_signal(xroar.vo_interface, VO_SIGNAL_SVIDEO);
		if (xroar.machine->set_composite)
			xroar.machine->set_composite(xroar.machine, 1);
		break;

	case TV_INPUT_CMP_KBRW:
		vo_set_signal(xroar.vo_interface, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_cmp_phase, 180);
		if (xroar.machine->set_composite)
			xroar.machine->set_composite(xroar.machine, 1);
		break;

	case TV_INPUT_CMP_KRBW:
		vo_set_signal(xroar.vo_interface, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_cmp_phase, 0);
		if (xroar.machine->set_composite)
			xroar.machine->set_composite(xroar.machine, 1);
		break;

	case TV_INPUT_RGB:
		vo_set_signal(xroar.vo_interface, VO_SIGNAL_RGB);
		if (xroar.machine->set_composite)
			xroar.machine->set_composite(xroar.machine, 0);
		break;
	}

	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tv_input, xroar.machine_config->tv_input, NULL);
	}
}

void xroar_set_vdg_inverted_text(_Bool notify, int action) {
	if (!xroar.machine->set_inverted_text)
		return;
	_Bool state = xroar.machine->set_inverted_text(xroar.machine, action);
	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_vdg_inverse, state, NULL);
	}
}

void xroar_set_picture(_Bool notify, int action) {
	if (!xroar.vo_interface)
		return;

	int picture = xroar.vo_interface->picture;
	switch (action) {
	case XROAR_PREV:
		picture--;
		break;

	case XROAR_NEXT:
		picture++;
		break;

	default:
		picture = action;
		break;
	}

	if (picture < 0)
		picture = 0;
	if (picture >= NUM_VO_PICTURE)
		picture = NUM_VO_PICTURE - 1;

	private_cfg.vo.picture = picture;

	vo_set_viewport(xroar.vo_interface, picture);

	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_picture, picture, NULL);
	}
}

void xroar_set_ratelimit(int action) {
	if (!xroar.machine->set_frameskip || !xroar.machine->set_ratelimit)
		return;
	if (xroar_state.noratelimit_latch)
		return;
	if (action) {
		xroar.machine->set_frameskip(xroar.machine, private_cfg.vo.frameskip);
		xroar.machine->set_ratelimit(xroar.machine, 1);
	} else {
		xroar.machine->set_frameskip(xroar.machine, 10);
		xroar.machine->set_ratelimit(xroar.machine, 0);
	}
}

void xroar_set_ratelimit_latch(_Bool notify, int action) {
	if (!xroar.machine->set_frameskip || !xroar.machine->set_ratelimit)
		return;
	_Bool state = !xroar_state.noratelimit_latch;
	switch (action) {
	case XROAR_ON:
	default:
		state = 1;
		break;
	case XROAR_OFF:
		state = 0;
		break;
	case XROAR_NEXT:
		state = !state;
		break;
	}
	xroar_state.noratelimit_latch = !state;
	if (state) {
		xroar.machine->set_frameskip(xroar.machine, private_cfg.vo.frameskip);
		xroar.machine->set_ratelimit(xroar.machine, 1);
	} else {
		xroar.machine->set_frameskip(xroar.machine, 10);
		xroar.machine->set_ratelimit(xroar.machine, 0);
	}
	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_ratelimit, state, NULL);
	}
}

void xroar_set_pause(_Bool notify, int action) {
	if (xroar.machine->set_pause) {
		_Bool state = xroar.machine->set_pause(xroar.machine, action);
		// TODO: UI indication of paused state
		(void)state;
		(void)notify;
	}
}

/** Quit the emulator.
 */

void xroar_quit(void) {
	exit(EXIT_SUCCESS);
}

void xroar_set_fullscreen(_Bool notify, int action) {
	_Bool set_to;
	switch (action) {
		case XROAR_OFF:
			set_to = 0;
			break;
		case XROAR_ON:
			set_to = 1;
			break;
		case XROAR_NEXT:
		default:
			set_to = !xroar.vo_interface->is_fullscreen;
			break;
	}
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_fullscreen, set_to);
	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_fullscreen, set_to, NULL);
	}
}

void xroar_set_menubar(int action) {
	_Bool set_to;
	switch (action) {
		case XROAR_OFF:
			set_to = 0;
			break;
		case XROAR_ON:
			set_to = 1;
			break;
		case XROAR_NEXT:
		default:
			set_to = !xroar.vo_interface->show_menubar;
			break;
	}
	DELEGATE_SAFE_CALL(xroar.vo_interface->set_menubar, set_to);
}

void xroar_load_file(void) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Load file");
	xroar_load_file_by_type(filename, 0);
}

void xroar_run_file(void) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Run file");
	xroar_load_file_by_type(filename, 1);
}

// Printer interface

void xroar_set_printer_destination(_Bool notify, int dest) {
	if (!xroar.printer_interface)
		return;
	printer_set_destination(xroar.printer_interface, dest);
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_print_destination, dest, NULL);
	}
}

void xroar_set_printer_file(_Bool notify, const char *filename) {
	if (!xroar.printer_interface)
		return;
	printer_set_file(xroar.printer_interface, filename);
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_print_file, 0, filename);
	}
}

void xroar_set_printer_pipe(_Bool notify, const char *pipe) {
	if (!xroar.printer_interface)
		return;
	printer_set_pipe(xroar.printer_interface, pipe);
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_print_pipe, 0, pipe);
	}
}

void xroar_flush_printer(void) {
	if (!xroar.printer_interface)
		return;
	printer_flush(xroar.printer_interface);
}

void xroar_set_keyboard_type(_Bool notify, int action) {
	int type = xroar.machine_config->keymap;
	if (xroar.machine->set_keyboard_type) {
		type = xroar.machine->set_keyboard_type(xroar.machine, action);
	}
	if (notify && xroar.ui_interface) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_keymap, type, NULL);
	}
}

void xroar_set_kbd_translate(_Bool notify, int kbd_translate) {
	switch (kbd_translate) {
		case XROAR_NEXT:
			xroar.cfg.kbd.translate = !xroar.cfg.kbd.translate;
			break;
		default:
			xroar.cfg.kbd.translate = kbd_translate;
			break;
	}
	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_kbd_translate, xroar.cfg.kbd.translate, NULL);
	}
}

static void update_ui_joysticks(int port) {
	const char *name = NULL;
	if (joystick_port_config[port] && joystick_port_config[port]->name) {
		name = joystick_port_config[port]->name;
	}
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_joy_right + port, 0, name);
}

void xroar_set_joystick(_Bool notify, int port, const char *name) {
	if (port < 0 || port > 1)
		return;
	if (name && *name) {
		joystick_map(joystick_config_by_name(name), port);
	} else {
		joystick_unmap(port);
	}
	if (notify)
		update_ui_joysticks(port);
}

void xroar_swap_joysticks(_Bool notify) {
	joystick_swap();
	if (notify) {
		update_ui_joysticks(0);
		update_ui_joysticks(1);
	}
}

void xroar_cycle_joysticks(_Bool notify) {
	joystick_cycle();
	if (notify) {
		update_ui_joysticks(0);
		update_ui_joysticks(1);
	}
}

void xroar_remove_joystick_config(const char *name) {
	for (int i = 0; i <= 1; i++) {
		if (!joystick_port_config[i])
			continue;
		if (0 == strcmp(joystick_port_config[i]->name, name)) {
			joystick_unmap(i);
			update_ui_joysticks(i);
		}
	}
	joystick_config_remove(name);
}

// Connect various external interfaces to the machine.  May well end up
// delegated to a sub-part of the machine.  Called during
// xroar_connect_machine(), and when cartridge is changed with
// xroar_set_cart().

static void connect_interfaces(void) {
	struct machine *m = xroar.machine;
	if (!m)
		return;
	struct part *p = &m->part;

	if (xroar.machine->has_interface) {
		if (xroar.machine->has_interface(p, "floppy")) {
			xroar.machine->attach_interface(p, "floppy", xroar.vdrive_interface);
		}
		if (xroar.machine->has_interface(p, "sound")) {
			xroar.machine->attach_interface(p, "sound", xroar.ao_interface->sound_interface);
		}
	}
}

/** \brief Connect UI to machine.
 */

void xroar_connect_machine(void) {
	assert(xroar.machine_config != NULL);
	assert(xroar.machine != NULL);
	tape_interface_connect_machine(xroar.tape_interface, xroar.machine);
	xroar.auto_kbd = auto_kbd_new(xroar.machine);
	xroar.keyboard_interface = xroar.machine->get_interface(xroar.machine, "keyboard");

	// Printing
	xroar.printer_interface = xroar.machine->get_interface(xroar.machine, "printer");
	xroar_set_printer_file(1, private_cfg.printer.file);
	xroar_set_printer_pipe(1, private_cfg.printer.pipe);
	if (private_cfg.printer.file) {
		xroar_set_printer_destination(1, PRINTER_DESTINATION_FILE);
	} else if (private_cfg.printer.pipe) {
		xroar_set_printer_destination(1, PRINTER_DESTINATION_PIPE);
	} else {
		xroar_set_printer_destination(1, PRINTER_DESTINATION_NONE);
	}

	struct cart *c = (struct cart *)part_component_by_id(&xroar.machine->part, "cart");
	if (c && !part_is_a((struct part *)c, "cart")) {
		part_free((struct part *)c);
		c = NULL;
	}

	if (xroar.ui_interface) {
		int mcid = xroar.machine_config->id;
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_machine, mcid, NULL);
		int ccid = (c && c->config) ? c->config->id : -1;
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_cartridge, ccid, NULL);
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_keymap, xroar.machine->keyboard.type, NULL);
	}

	connect_interfaces();

	_Bool is_coco3 = strcmp(xroar.machine_config->architecture, "coco3") == 0;
	_Bool is_coco = is_coco3 || strcmp(xroar.machine_config->architecture, "coco") == 0;

	if (is_coco) {
		vdisk_set_interleave(VDISK_SINGLE_DENSITY, 5);
		vdisk_set_interleave(VDISK_DOUBLE_DENSITY, 5);
	} else {
		vdisk_set_interleave(VDISK_SINGLE_DENSITY, 2);
		vdisk_set_interleave(VDISK_DOUBLE_DENSITY, 2);
	}
	xroar_set_ccr(1, private_cfg.vo.ccr);
	xroar_set_tv_input(1, xroar.machine_config->tv_input);

	int old_picture = private_cfg.vo.picture;
	int picture = old_picture;
	if (picture == ANY_AUTO) {
		picture = is_coco3 ? VO_PICTURE_ACTION : VO_PICTURE_TITLE;
	}
	xroar_set_picture(1, picture);
	private_cfg.vo.picture = old_picture;
}

void xroar_configure_machine(struct machine_config *mc) {
	if (xroar.auto_kbd) {
		auto_kbd_free(xroar.auto_kbd);
		xroar.auto_kbd = NULL;
	}
	if (xroar.machine) {
		part_free((struct part *)xroar.machine);
	}
	xroar.machine_config = mc;
	xroar.machine = machine_new(mc);
	xroar_update_cartridge_menu();  // XXX why here?
	xroar_connect_machine();
}

void xroar_set_machine(_Bool notify, int id) {
	struct slist *mcl, *mcc;
	int new;
	switch (id) {
		case XROAR_NEXT:
			mcl = machine_config_list();
			mcc = slist_find(mcl, xroar.machine_config);
			if (mcc && mcc->next) {
				new = ((struct machine_config *)mcc->next->data)->id;
			} else {
				new = ((struct machine_config *)mcl->data)->id;
			}
			break;
		default:
			new = (id >= 0 ? id : 0);
			break;
	}
	struct machine_config *mc = machine_config_by_id(new);
	machine_config_complete(mc);
#ifdef HAVE_WASM
	_Bool waiting = !wasm_ui_prepare_machine(mc);;
	if (mc->default_cart) {
		struct cart_config *cc = cart_config_by_name(mc->default_cart);
		waiting |= !wasm_ui_prepare_cartridge(cc);
	}
	if (waiting)
		return;
#endif
	xroar_configure_machine(mc);
	if (mc->cart_enabled) {
		xroar_set_cart(1, mc->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}
	xroar_hard_reset();
	if (notify) {
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_machine, new, NULL);
	}
}

void xroar_update_cartridge_menu(void) {
	if (xroar.ui_interface) {
		DELEGATE_SAFE_CALL(xroar.ui_interface->update_cartridge_menu);
	}
}

void xroar_toggle_cart(void) {
	assert(xroar.machine_config != NULL);
	xroar.machine_config->cart_enabled = !xroar.machine_config->cart_enabled;
	if (xroar.machine_config->cart_enabled) {
		xroar_set_cart(1, xroar.machine_config->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}
}

void xroar_set_cart_by_id(_Bool notify, int id) {
	struct cart_config *cc = cart_config_by_id(id);
	const char *name = cc ? cc->name : NULL;
#ifdef HAVE_WASM
	if (!wasm_ui_prepare_cartridge(cc)) {
		return;
	}
#endif
	xroar_set_cart(notify, name);
}

void xroar_set_cart(_Bool notify, const char *cc_name) {
	assert(xroar.machine_config != NULL);

	struct cart *old_cart = xroar.machine->get_interface(xroar.machine, "cart");
	if (!old_cart && !cc_name)
		return;
	// This trips GCC-10's static analyser at the moment, as it doesn't
	// seem to account for the short-circuit "&&".
	if (old_cart && cc_name && 0 == strcmp(cc_name, old_cart->config->name))
		return;

	// Some machines don't actually support carts yet
	if (!xroar.machine->insert_cart) {
		if (notify) {
			DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_cartridge, -1, NULL);
		}
		return;
	}

	xroar.machine->remove_cart(xroar.machine);

	struct cart *new_cart = NULL;
	if (!cc_name) {
		xroar.machine_config->cart_enabled = 0;
	} else {
		if (xroar.machine_config->default_cart != cc_name) {
			free(xroar.machine_config->default_cart);
			xroar.machine_config->default_cart = xstrdup(cc_name);
		}
		xroar.machine_config->cart_enabled = 1;
		new_cart = cart_create(cc_name);
		if (new_cart) {
			xroar.machine->insert_cart(xroar.machine, new_cart);
			connect_interfaces();
			// Reset the cart once all interfaces are attached
			if (new_cart->reset)
				new_cart->reset(new_cart, 1);
		}
	}

	if (notify) {
		int id = new_cart ? new_cart->config->id : -1;
		DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_cartridge, id, NULL);
	}
}

void xroar_save_snapshot(void) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Save snapshot");
	if (filename) {
		write_snapshot(filename);
	}
}

void xroar_insert_input_tape_file(const char *filename) {
	if (!filename) return;
	tape_open_reading(xroar.tape_interface, filename);
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_input_filename, 0, filename);
}

void xroar_insert_input_tape(void) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Select input tape");
	xroar_insert_input_tape_file(filename);
}

void xroar_eject_input_tape(void) {
	tape_close_reading(xroar.tape_interface);
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_input_filename, 0, NULL);
}

void xroar_insert_output_tape_file(const char *filename) {
	if (!filename) return;
	tape_open_writing(xroar.tape_interface, filename);
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_output_filename, 0, filename);
}

void xroar_insert_output_tape(void) {
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Select output tape");
	xroar_insert_output_tape_file(filename);
}

void xroar_eject_output_tape(void) {
	tape_close_writing(xroar.tape_interface);
	DELEGATE_CALL(xroar.ui_interface->update_state, ui_tag_tape_output_filename, 0, NULL);
}

void xroar_set_tape_playing(_Bool notify, _Bool play) {
	tape_set_playing(xroar.tape_interface, play, notify);
}

void xroar_soft_reset(void) {
	xroar.machine->reset(xroar.machine, RESET_SOFT);
}

void xroar_hard_reset(void) {
	xroar.machine->reset(xroar.machine, RESET_HARD);
}

#ifdef SCREENSHOT
void xroar_screenshot(void) {
#ifdef HAVE_PNG
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Save screenshot");
	if (!filename)
		return;

	int r = screenshot_write_png(filename, xroar.vo_interface);
	if (r != 0) {
		if (r == -1) {
			perror("screenshot");
		} else {
			LOG_WARN("screenshot: error writing file\n");
		}
	}
#endif
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Helper functions used by configuration */

static void set_default_machine(const char *name) {
	private_cfg.default_machine = xstrdup(name);
	// If no machine specified on command line, get default.
	if (!xroar.machine_config && private_cfg.default_machine) {
		xroar.machine_config = machine_config_by_name(private_cfg.default_machine);
	}
}

/* Called when a "-machine" option is encountered.  If an existing machine
 * config was in progress, copies any machine-related options into it and
 * clears those options.  Starts a new config. */
static void set_machine(const char *name) {
#ifdef LOGGING
	if (name && 0 == strcmp(name, "help")) {
		struct slist *mcl = machine_config_list();
		while (mcl) {
			struct machine_config *mc = mcl->data;
			mcl = mcl->next;
			printf("\t%-10s %s\n", mc->name, mc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif

	if (xroar.machine_config) {
		if (private_cfg.machine.arch) {
			if (xroar.machine_config->architecture) {
				free(xroar.machine_config->architecture);
			}
			xroar.machine_config->architecture = private_cfg.machine.arch;
			private_cfg.machine.arch = NULL;
		}
		if (private_cfg.machine.keymap != ANY_AUTO) {
			xroar.machine_config->keymap = private_cfg.machine.keymap;
			private_cfg.machine.keymap = ANY_AUTO;
		}
		xroar.machine_config->cpu = private_cfg.machine.cpu;
		private_cfg.machine.cpu = CPU_MC6809;
		if (private_cfg.machine.description) {
			if (xroar.machine_config->description) {
				free(xroar.machine_config->description);
			}
			xroar.machine_config->description = private_cfg.machine.description;
			private_cfg.machine.description = NULL;
		}
#ifdef LOGGING
		if (private_cfg.machine.palette && 0 == strcmp(private_cfg.machine.palette, "help")) {
			int count = vdg_palette_count();
			int i;
			for (i = 0; i < count; i++) {
				struct vdg_palette *vp = vdg_palette_index(i);
				printf("\t%-10s %s\n", vp->name, vp->description);
			}
			exit(EXIT_SUCCESS);
		}
#endif
		if (private_cfg.machine.palette) {
			if (xroar.machine_config->vdg_palette) {
				free(xroar.machine_config->vdg_palette);
			}
			xroar.machine_config->vdg_palette = private_cfg.machine.palette;
			private_cfg.machine.palette = NULL;
		}
		if (private_cfg.machine.tv_type != ANY_AUTO) {
			xroar.machine_config->tv_standard = private_cfg.machine.tv_type;
			private_cfg.machine.tv_type = ANY_AUTO;
		}
		if (private_cfg.machine.tv_input != ANY_AUTO) {
			xroar.machine_config->tv_input = private_cfg.machine.tv_input;
			private_cfg.machine.tv_input = ANY_AUTO;
		}
		if (private_cfg.machine.vdg_type != -1) {
			xroar.machine_config->vdg_type = private_cfg.machine.vdg_type;
			private_cfg.machine.vdg_type = -1;
		}
		if (private_cfg.machine.ram_org != ANY_AUTO) {
			xroar.machine_config->ram_org = private_cfg.machine.ram_org;
			private_cfg.machine.ram_org = ANY_AUTO;
		}
		if (private_cfg.machine.ram > 0) {
			xroar.machine_config->ram = private_cfg.machine.ram;
			private_cfg.machine.ram = 0;
		}
		if (private_cfg.machine.ram_init != ANY_AUTO) {
			xroar.machine_config->ram_init = private_cfg.machine.ram_init;
			private_cfg.machine.ram_init = ANY_AUTO;
		}
		if (private_cfg.machine.bas_dfn) {
			private_cfg.machine.bas_dfn = 0;
			xroar.machine_config->bas_dfn = 1;
			if (xroar.machine_config->bas_rom) {
				free(xroar.machine_config->bas_rom);
			}
			xroar.machine_config->bas_rom = private_cfg.machine.bas;
			private_cfg.machine.bas = NULL;
		}
		if (private_cfg.machine.extbas_dfn) {
			private_cfg.machine.extbas_dfn = 0;
			xroar.machine_config->extbas_dfn = 1;
			if (xroar.machine_config->extbas_rom) {
				free(xroar.machine_config->extbas_rom);
			}
			xroar.machine_config->extbas_rom = private_cfg.machine.extbas;
			private_cfg.machine.extbas = NULL;
		}
		if (private_cfg.machine.altbas_dfn) {
			private_cfg.machine.altbas_dfn = 0;
			xroar.machine_config->altbas_dfn = 1;
			if (xroar.machine_config->altbas_rom) {
				free(xroar.machine_config->altbas_rom);
			}
			xroar.machine_config->altbas_rom = private_cfg.machine.altbas;
			private_cfg.machine.altbas = NULL;
		}
		if (private_cfg.machine.ext_charset_dfn) {
			private_cfg.machine.ext_charset_dfn = 0;
			if (xroar.machine_config->ext_charset_rom) {
				free(xroar.machine_config->ext_charset_rom);
			}
			xroar.machine_config->ext_charset_rom = private_cfg.machine.ext_charset;
			private_cfg.machine.ext_charset = NULL;
		}
		if (private_cfg.machine.cart_dfn) {
			private_cfg.machine.cart_dfn = 0;
			xroar.machine_config->default_cart_dfn = 1;
			if (xroar.machine_config->default_cart) {
				free(xroar.machine_config->default_cart);
			}
			xroar.machine_config->default_cart = private_cfg.machine.cart;
			private_cfg.machine.cart = NULL;
		}
		if (private_cfg.machine.opts) {
			xroar.machine_config->opts = slist_concat(xroar.machine_config->opts, private_cfg.machine.opts);
			private_cfg.machine.opts = NULL;
		}
		machine_config_complete(xroar.machine_config);
	}
	if (name) {
		xroar.machine_config = machine_config_by_name(name);
		if (!xroar.machine_config) {
			xroar.machine_config = machine_config_new();
			xroar.machine_config->name = xstrdup(name);
		}
	}
}

/* Called when a "-cart" option is encountered.  If an existing cart config was
* in progress, copies any cart-related options into it and clears those
* options.  Starts a new config.  */
static void set_cart(const char *name) {
#ifdef LOGGING
	if (name && 0 == strcmp(name, "help")) {
		struct slist *ccl = cart_config_list();
		while (ccl) {
			struct cart_config *cc = ccl->data;
			ccl = ccl->next;
			printf("\t%-10s %s\n", cc->name, cc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif
	// Apply any unassigned config to either the current cart config or the
	// current machine's default cart config.
	struct cart_config *cc = NULL;
	if (selected_cart_config) {
		cc = selected_cart_config;
	} else if (xroar.machine_config) {
		cc = cart_config_by_name(xroar.machine_config->default_cart);
	}
	if (cc) {
		if (private_cfg.cart.description) {
			if (cc->description) {
				free(cc->description);
			}
			cc->description = private_cfg.cart.description;
			private_cfg.cart.description = NULL;
		}
		if (private_cfg.cart.type) {
			if (cc->type) {
				free(cc->type);
			}
			cc->type = private_cfg.cart.type;
			private_cfg.cart.type = NULL;
		}
		if (private_cfg.cart.rom) {
			if (cc->rom) {
				free(cc->rom);
			}
			cc->rom = private_cfg.cart.rom;
			private_cfg.cart.rom = NULL;
		}
		if (private_cfg.cart.rom2) {
			if (cc->rom2) {
				free(cc->rom2);
			}
			cc->rom2 = private_cfg.cart.rom2;
			private_cfg.cart.rom2 = NULL;
		}
		if (private_cfg.cart.becker != ANY_AUTO) {
			cc->becker_port = private_cfg.cart.becker;
			private_cfg.cart.becker = ANY_AUTO;
		}
		if (private_cfg.cart.autorun != ANY_AUTO) {
			cc->autorun = private_cfg.cart.autorun;
			private_cfg.cart.autorun = ANY_AUTO;
		}
		if (private_cfg.cart.mpi.initial_slot != ANY_AUTO) {
			cc->mpi.initial_slot = private_cfg.cart.mpi.initial_slot;
			private_cfg.cart.mpi.initial_slot = ANY_AUTO;
		}
		for (int i = 0; i < 4; i++) {
			if (private_cfg.cart.mpi.slot_cart_name[i]) {
				if (cc->mpi.slot_cart_name[i]) {
					free(cc->mpi.slot_cart_name[i]);
				}
				cc->mpi.slot_cart_name[i] = private_cfg.cart.mpi.slot_cart_name[i];
				private_cfg.cart.mpi.slot_cart_name[i] = NULL;
			}
		}
		if (private_cfg.cart.opts) {
			cc->opts = slist_concat(cc->opts, private_cfg.cart.opts);
			private_cfg.cart.opts = NULL;
		}
		cart_config_complete(cc);
	}
	if (name) {
		selected_cart_config = cart_config_by_name(name);
		if (!selected_cart_config) {
			selected_cart_config = cart_config_new();
			selected_cart_config->name = xstrdup(name);
		}
	}
}

// Populate appropriate config option with file to load based on its type.
// Returns which autorun slot it would be.

static enum media_slot add_load_file(const char *filename) {
	enum media_slot slot = media_slot_none;

	if (!filename) {
		return slot;
	}

	int filetype = xroar_filetype_by_ext(filename);
	switch (filetype) {

	case FILETYPE_VDK:
	case FILETYPE_JVC:
	case FILETYPE_OS9:
	case FILETYPE_DMK:
		for (int i = 0; i < 4; i++) {
			if (!private_cfg.file.fd[i]) {
				private_cfg.file.fd[i] = xstrdup(filename);
				slot = media_slot_fd0 + i;
				break;
			}
			if (i == 3) {
				LOG_WARN("No empty floppy drive for '%s': ignoring\n", filename);
			}
		}
		break;

	case FILETYPE_BIN:
		private_cfg.file.binaries = slist_append(private_cfg.file.binaries, xstrdup(filename));
		slot = media_slot_binary;
		break;

	case FILETYPE_ASC:
		if (xroar.machine_config && strcmp(xroar.machine_config->architecture, "mc10") == 0) {
			private_cfg.file.text = xstrdup(filename);
			slot = media_slot_text;
			break;
		}
		private_cfg.file.tape = xstrdup(filename);
		slot = media_slot_tape;
		break;

	case FILETYPE_CAS:
	case FILETYPE_K7:
	case FILETYPE_WAV:
	case FILETYPE_UNKNOWN:
		private_cfg.file.tape = xstrdup(filename);
		slot = media_slot_tape;
		break;

	case FILETYPE_ROM:
		selected_cart_config = cart_config_by_name(filename);
		slot = media_slot_cartridge;
		break;

	case FILETYPE_VHD:
	case FILETYPE_IDE:
	case FILETYPE_IMG:
		// TODO: recognise media type and select cartridge accordingly
		for (int i = 0; i < 2; i++) {
			if (!xroar.cfg.file.hd[i]) {
				xroar.cfg.file.hd[i] = xstrdup(filename);
				break;
			}
			if (i == 1) {
				LOG_WARN("No unused hard drive slot for '%s': ignoring\n", filename);
			}
		}
		break;

	case FILETYPE_SNA:
		private_cfg.file.snapshot = xstrdup(filename);
		slot = media_slot_snapshot;
		break;

	}

	return slot;
}

// Add a file to load.

static void add_load(const char *arg) {
	enum media_slot s = add_load_file(arg);
	// loading a snapshot _is_ autorunning, so record that
	if (s == media_slot_snapshot) {
		autorun_media_slot = media_slot_snapshot;
	}
}

// Add a file to load and mark its slot to autorun.

static void add_run(const char *arg) {
	enum media_slot s = add_load_file(arg);
	// if we already have a snapshot to load, whether or not we autorun
	// this is irrelevant
	if (autorun_media_slot == media_slot_none || s == media_slot_snapshot) {
		autorun_media_slot = s;
	}
}

static void set_gain(double gain) {
	private_cfg.ao.gain = gain;
	private_cfg.ao.volume = -1;
}

static void cfg_mpi_load_cart(const char *arg) {
	(void)arg;
#ifdef WANT_CART_ARCH_DRAGON
	char *arg_copy = xstrdup(arg);
	char *carg = arg_copy;
	char *tmp = strsep(&carg, "=");
	static int slot = 0;
	if (carg) {
		slot = strtol(tmp, NULL, 0);
		tmp = carg;
	}
	if (slot < 0 || slot > 3) {
		LOG_WARN("MPI: Invalid slot '%d'\n", slot);
	} else {
		if (private_cfg.cart.mpi.slot_cart_name[slot]) {
			free(private_cfg.cart.mpi.slot_cart_name[slot]);
		}
		private_cfg.cart.mpi.slot_cart_name[slot] = xstrdup(tmp);
	}
	slot++;
	free(arg_copy);
#endif
}

static void set_kbd_bind(const char *spec) {
	char *spec_copy = xstrdup(spec);
	char *cspec = spec_copy;
	char *hkey = strsep(&cspec, "=");
	if (cspec) {
		char *tmp = strsep(&cspec, ":");
		char *flag = NULL;
		char *dkey;
		if (cspec) {
			flag = tmp;
			dkey = cspec;
		} else {
			dkey = tmp;
		}
		_Bool preempt = 0;
		if (flag && c_strncasecmp(flag, "pre", 3) == 0) {
			preempt = 1;
		}
		int8_t dk_key = dk_key_by_name(dkey);
		if (dk_key >= 0) {
			struct dkbd_bind *bind = xmalloc(sizeof(*bind));
			bind->hostkey = xstrdup(hkey);
			bind->dk_key = dk_key;
			bind->preempt = preempt;
			xroar.cfg.kbd.bind_list = slist_append(xroar.cfg.kbd.bind_list, bind);
		}
	}
	free(spec_copy);
}

/* Called when a "-joystick" option is encountered. */
static void set_joystick(const char *name) {
	// Apply any config to the current joystick config.
	if (cur_joy_config) {
		if (private_cfg.joy.description) {
			cur_joy_config->description = private_cfg.joy.description;
			private_cfg.joy.description = NULL;
		}
		for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
			if (private_cfg.joy.axis[i]) {
				if (cur_joy_config->axis_specs[i])
					free(cur_joy_config->axis_specs[i]);
				cur_joy_config->axis_specs[i] = private_cfg.joy.axis[i];
				private_cfg.joy.axis[i] = NULL;
			}
		}
		for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
			if (private_cfg.joy.button[i]) {
				if (cur_joy_config->button_specs[i])
					free(cur_joy_config->button_specs[i]);
				cur_joy_config->button_specs[i] = private_cfg.joy.button[i];
				private_cfg.joy.button[i] = NULL;
			}
		}
	}
#ifdef LOGGING
#ifndef HAVE_WASM
	if (name && 0 == strcmp(name, "help")) {
		private_cfg.help.joystick_print_list = 1;
		return;
	}
#endif
#endif
	if (name) {
		cur_joy_config = joystick_config_by_name(name);
		if (!cur_joy_config) {
			cur_joy_config = joystick_config_new();
			cur_joy_config->name = xstrdup(name);
		}
	}
}

static void set_joystick_axis(const char *spec) {
	char *spec_copy = xstrdup(spec);
	char *cspec = spec_copy;
	unsigned axis = 0;
	char *tmp = strsep(&cspec, "=");
	if (cspec) {
		if (toupper(*tmp) == 'X') {
			axis = 0;
		} else if (toupper(*tmp) == 'Y') {
			axis = 1;
		} else {
			axis = strtol(tmp, NULL, 0);
		}
		if (axis > JOYSTICK_NUM_AXES) {
			LOG_WARN("Invalid axis number '%u'\n", axis);
			axis = 0;
		}
		tmp = cspec;
	}
	private_cfg.joy.axis[axis] = xstrdup(tmp);
	free(spec_copy);
}

static void set_joystick_button(const char *spec) {
	char *spec_copy = xstrdup(spec);
	char *cspec = spec_copy;
	unsigned button = 0;
	char *tmp = strsep(&cspec, "=");
	if (cspec) {
		button = strtol(tmp, NULL, 0);
		if (button > JOYSTICK_NUM_AXES) {
			LOG_WARN("Invalid button number '%u'\n", button);
			button = 0;
		}
		tmp = cspec;
	}
	private_cfg.joy.button[button] = xstrdup(tmp);
	free(spec_copy);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Enumeration lists used by configuration directives */

static struct xconfig_enum ao_format_list[] = {
	{ XC_ENUM_INT("u8", SOUND_FMT_U8, "8-bit unsigned") },
	{ XC_ENUM_INT("s8", SOUND_FMT_S8, "8-bit signed") },
	{ XC_ENUM_INT("s16", SOUND_FMT_S16_HE, "16-bit signed host-endian") },
	{ XC_ENUM_INT("s16se", SOUND_FMT_S16_SE, "16-bit signed swapped-endian") },
	{ XC_ENUM_INT("s16be", SOUND_FMT_S16_BE, "16-bit signed big-endian") },
	{ XC_ENUM_INT("s16le", SOUND_FMT_S16_LE, "16-bit signed little-endian") },
	{ XC_ENUM_INT("float", SOUND_FMT_FLOAT, "Floating point") },
	{ XC_ENUM_END() }
};

// XXX make a proper enum for these magic numbers
static struct xconfig_enum vo_viewport_list[] = {
	{ XC_ENUM_INT("zoomed", 0, "512x384 (zoomed)") },
	{ XC_ENUM_INT("title", 1, "640x480 (title safe)") },
	{ XC_ENUM_INT("action", 2, "720x540 (action safe)") },
	{ XC_ENUM_INT("underscan", 3, "736x552 (underscan)") },
	{ XC_ENUM_END() }
};

/* Configuration directives */

static union {
	_Bool v_bool;
	int v_int;
} dummy_value;

static struct xconfig_option const xroar_options[] = {
	/* Machines: */
	{ XC_CALL_STRING("default-machine", &set_default_machine) },
	{ XC_CALL_STRING("m", &set_machine) },
	{ XC_CALL_STRING("machine", &set_machine) },
	{ XC_SET_STRING("machine-desc", &private_cfg.machine.description) },
	{ XC_SET_PART("machine-arch", &private_cfg.machine.arch, "machine") },
	{ XC_SET_ENUM("machine-keyboard", &private_cfg.machine.keymap, machine_keyboard_list) },
	{ XC_SET_ENUM("machine-cpu", &private_cfg.machine.cpu, machine_cpu_list) },
	{ XC_SET_STRING_NE("bas", &private_cfg.machine.bas), .defined = &private_cfg.machine.bas_dfn },
	{ XC_SET_STRING_NE("extbas", &private_cfg.machine.extbas), .defined = &private_cfg.machine.extbas_dfn },
	{ XC_SET_STRING_NE("altbas", &private_cfg.machine.altbas), .defined = &private_cfg.machine.altbas_dfn },
	{ XC_SET_STRING_NE("ext-charset", &private_cfg.machine.ext_charset), .defined = &private_cfg.machine.ext_charset_dfn },
	{ XC_SET_ENUM("tv-type", &private_cfg.machine.tv_type, machine_tv_type_list) },
	{ XC_SET_ENUM("tv-input", &private_cfg.machine.tv_input, machine_tv_input_list) },
	{ XC_SET_ENUM("vdg-type", &private_cfg.machine.vdg_type, machine_vdg_type_list) },
	{ XC_SET_ENUM("ram-org", &private_cfg.machine.ram_org, machine_ram_org_list) },
	{ XC_SET_INT("ram", &private_cfg.machine.ram) },
	{ XC_SET_ENUM("ram-init", &private_cfg.machine.ram_init, machine_ram_init_list) },
	{ XC_SET_STRING("machine-cart", &private_cfg.machine.cart), .defined = &private_cfg.machine.cart_dfn },
	{ XC_SET_STRING_LIST_NE("machine-opt", &private_cfg.machine.opts) },
	// Shorthand:
	{ XC_ALIAS_ARG("pal", "tv-type", "pal") },
	{ XC_ALIAS_ARG("ntsc", "tv-type", "ntsc") },
	// Deliberately undocumented:
	{ XC_SET_STRING("machine-palette", &private_cfg.machine.palette) },
	{ XC_ALIAS_NOARG("nodos", "no-machine-cart") },
	// Backwards compatibility:
	{ XC_ALIAS_NOARG("nobas", "no-bas"), .deprecated = 1 },
	{ XC_ALIAS_NOARG("noextbas", "no-extbas"), .deprecated = 1 },
	{ XC_ALIAS_NOARG("noaltbas", "no-altbas"), .deprecated = 1 },

	/* Cartridges: */
	{ XC_CALL_STRING("cart", &set_cart) },
	{ XC_SET_STRING("cart-desc", &private_cfg.cart.description) },
	{ XC_SET_PART("cart-type", &private_cfg.cart.type, "cart") },
	{ XC_SET_STRING_NE("cart-rom", &private_cfg.cart.rom) },
	{ XC_SET_STRING_NE("cart-rom2", &private_cfg.cart.rom2) },
	{ XC_SET_INT1("cart-autorun", &private_cfg.cart.autorun) },
	{ XC_SET_INT1("cart-becker", &private_cfg.cart.becker) },
	{ XC_SET_STRING_LIST_NE("cart-opt", &private_cfg.cart.opts) },

	/* Multi-Pak Interface: */
	{ XC_SET_INT("mpi-slot", &private_cfg.cart.mpi.initial_slot) },
	{ XC_CALL_STRING("mpi-load-cart", &cfg_mpi_load_cart) },

	/* Becker port: */
	{ XC_SET_BOOL("becker", &xroar.cfg.becker.prefer) },
	{ XC_SET_STRING("becker-ip", &xroar.cfg.becker.ip) },
	{ XC_SET_STRING("becker-port", &xroar.cfg.becker.port) },

	/* Files: */
	{ XC_CALL_STRING_NE("load", &add_load) },
	{ XC_CALL_STRING_NE("run", &add_run) },
	{ XC_SET_STRING_NE("load-fd0", &private_cfg.file.fd[0]) },
	{ XC_SET_STRING_NE("load-fd1", &private_cfg.file.fd[1]) },
	{ XC_SET_STRING_NE("load-fd2", &private_cfg.file.fd[2]) },
	{ XC_SET_STRING_NE("load-fd3", &private_cfg.file.fd[3]) },
	{ XC_SET_STRING_NE("load-hd0", &xroar.cfg.file.hd[0]) },
	{ XC_SET_STRING_NE("load-hd1", &xroar.cfg.file.hd[1]) },
	{ XC_ALIAS_UARG("load-sd", "load-hd0"), .deprecated = 1 },
	{ XC_SET_STRING_NE("load-tape", &private_cfg.file.tape) },
	{ XC_SET_STRING_NE("load-text", &private_cfg.file.text) },

	/* Cassettes: */
	{ XC_SET_STRING_NE("tape-write", &private_cfg.file.tape_write) },
	{ XC_SET_DOUBLE("tape-pan", &xroar.cfg.tape.pan) },
	{ XC_SET_DOUBLE("tape-hysteresis", &xroar.cfg.tape.hysteresis) },
	{ XC_SET_INT1("tape-fast", &private_cfg.tape.fast) },
	{ XC_SET_INT1("tape-pad-auto", &private_cfg.tape.pad_auto) },
	{ XC_SET_INT1("tape-rewrite", &private_cfg.tape.rewrite) },
	{ XC_SET_INT("tape-rewrite-gap-ms", &xroar.cfg.tape.rewrite_gap_ms) },
	{ XC_SET_INT("tape-rewrite-leader", &xroar.cfg.tape.rewrite_leader) },
	{ XC_SET_INT("tape-ao-rate", &private_cfg.tape.ao_rate) },
	/* Backwards-compatibility: */
	{ XC_SET_INT1("tape-pad", &dummy_value.v_int), .deprecated = 1 },

	/* Floppy disks: */
	{ XC_SET_BOOL("disk-write-back", &xroar.cfg.disk.write_back) },
	{ XC_SET_BOOL("disk-auto-os9", &xroar.cfg.disk.auto_os9) },
	{ XC_SET_BOOL("disk-auto-sd", &xroar.cfg.disk.auto_sd) },

	/* Firmware ROM images: */
	{ XC_SET_STRING_NE("rompath", &xroar.cfg.file.rompath) },
	{ XC_CALL_ASSIGN_NE("romlist", &romlist_assign) },
	{ XC_CALL_NONE("romlist-print", &romlist_print) },
	{ XC_CALL_ASSIGN("crclist", &crclist_assign) },
	{ XC_CALL_NONE("crclist-print", &crclist_print) },
	{ XC_SET_BOOL("force-crc-match", &xroar.cfg.force_crc_match) },

	/* User interface: */
	{ XC_SET_STRING("ui", &private_cfg.ui_module) },
	/* Deliberately undocumented: */
	{ XC_SET_STRING("filereq", &xroar_ui_cfg.filereq) },

	/* Video: */
	{ XC_SET_BOOL("fs", &xroar_ui_cfg.vo_cfg.fullscreen) },
	{ XC_SET_INT("fskip", &private_cfg.vo.frameskip) },
	{ XC_SET_ENUM("ccr", &private_cfg.vo.ccr, vo_cmp_ccr_list) },
	{ XC_SET_ENUM("gl-filter", &xroar_ui_cfg.vo_cfg.gl_filter, ui_gl_filter_list) },
	{ XC_SET_ENUM("vo-pixel-fmt", &xroar_ui_cfg.vo_cfg.pixel_fmt, vo_pixel_fmt_list) },
	{ XC_SET_STRING("geometry", &xroar_ui_cfg.vo_cfg.geometry) },
	{ XC_SET_STRING("g", &xroar_ui_cfg.vo_cfg.geometry) },
	{ XC_SET_ENUM("vo-picture", &private_cfg.vo.picture, vo_viewport_list) },
	{ XC_SET_BOOL("vo-scale-60hz", &private_cfg.vo.ntsc_scaling) },
	{ XC_SET_BOOL("invert-text", &private_cfg.vo.vdg_inverted_text) },
	{ XC_SET_INT("vo-brightness", &private_cfg.vo.brightness) },
	{ XC_SET_INT("vo-contrast", &private_cfg.vo.contrast) },
	{ XC_SET_INT("vo-colour", &private_cfg.vo.saturation) },
	{ XC_SET_INT("vo-hue", &private_cfg.vo.hue) },
	{ XC_SET_BOOL("vo-colour-killer", &xroar_ui_cfg.vo_cfg.colour_killer) },
	/* Deliberately undocumented: */
	{ XC_SET_STRING("vo", &xroar_ui_cfg.vo) },

	/* Audio: */
	{ XC_SET_STRING("ao", &private_cfg.ao_module) },
	{ XC_SET_STRING("ao-device", &xroar.cfg.ao.device) },
	{ XC_SET_ENUM("ao-format", &xroar.cfg.ao.format, ao_format_list) },
	{ XC_SET_INT("ao-rate", &xroar.cfg.ao.rate) },
	{ XC_SET_INT("ao-channels", &xroar.cfg.ao.channels) },
	{ XC_SET_INT("ao-fragments", &xroar.cfg.ao.fragments) },
	{ XC_SET_INT("ao-fragment-ms", &xroar.cfg.ao.fragment_ms) },
	{ XC_SET_INT("ao-fragment-frames", &xroar.cfg.ao.fragment_nframes) },
	{ XC_SET_INT("ao-buffer-ms", &xroar.cfg.ao.buffer_ms) },
	{ XC_SET_INT("ao-buffer-frames", &xroar.cfg.ao.buffer_nframes) },
	{ XC_CALL_DOUBLE("ao-gain", &set_gain) },
	{ XC_SET_INT("ao-volume", &private_cfg.ao.volume) },
	/* Deliberately undocumented: */
	{ XC_SET_INT("volume", &private_cfg.ao.volume) },
	/* Backwards-compatibility: */
	{ XC_SET_INT("ao-buffer-samples", &xroar.cfg.ao.buffer_nframes), .deprecated = 1 },
	{ XC_SET_BOOL("fast-sound", &dummy_value.v_bool), .deprecated = 1 },

	/* Keyboard: */
	{ XC_SET_ENUM("kbd-layout", &xroar.cfg.kbd.layout, hkbd_layout_list) },
	{ XC_SET_ENUM("kbd-lang", &xroar.cfg.kbd.lang, hkbd_lang_list) },
	{ XC_SET_ENUM("keymap", &xroar.cfg.kbd.lang, hkbd_lang_list), .deprecated = 1 },
	{ XC_SET_BOOL("kbd-translate", &xroar.cfg.kbd.translate) },
	{ XC_CALL_STRING("kbd-bind", &set_kbd_bind) },

	/* Joysticks: */
	{ XC_CALL_STRING("joy", &set_joystick) },
	{ XC_SET_STRING("joy-desc", &private_cfg.joy.description) },
	{ XC_CALL_STRING("joy-axis", &set_joystick_axis) },
	{ XC_CALL_STRING("joy-button", &set_joystick_button) },
	{ XC_SET_STRING("joy-right", &private_cfg.joy.right) },
	{ XC_SET_STRING("joy-left", &private_cfg.joy.left) },
	{ XC_SET_STRING("joy-virtual", &private_cfg.joy.virtual) },

	/* Printing: */
	{ XC_SET_STRING_NE("lp-file", &private_cfg.printer.file) },
	{ XC_SET_STRING("lp-pipe", &private_cfg.printer.pipe) },

	/* Emulator actions: */
	{ XC_SET_BOOL("ratelimit", &private_cfg.debug.ratelimit) },
	{ XC_SET_STRING("snap-motoroff", &xroar.cfg.debug.snap_motoroff) },
	{ XC_SET_STRING("timeout", &private_cfg.debug.timeout) },
	{ XC_SET_STRING("timeout-motoroff", &xroar.cfg.debug.timeout_motoroff) },
	{ XC_SET_STRING_LIST("type", &private_cfg.kbd.type_list) },

	/* Debugging: */
	{ XC_SET_INT("debug-fdc", &logging.debug_fdc) },
	{ XC_SET_INT("debug-file", &logging.debug_file) },
	{ XC_SET_INT("debug-gdb", &logging.debug_gdb) },
	{ XC_SET_INT("debug-ui", &logging.debug_ui) },
	{ XC_SET_BOOL("gdb", &xroar.cfg.debug.gdb) },
	{ XC_SET_STRING("gdb-ip", &xroar.cfg.debug.gdb_ip) },
	{ XC_SET_STRING("gdb-port", &xroar.cfg.debug.gdb_port) },
	{ XC_SET_BOOL("trace", &logging.trace_cpu) },
	{ XC_SET_BOOL("trace-timing", &logging.trace_cpu_timing) },

	/* Other options: */
#ifndef HAVE_WASM
	{ XC_SET_BOOL("config-print", &private_cfg.help.config_print) },
	{ XC_SET_BOOL("config-print-all", &private_cfg.help.config_print_all) },
#endif
	{ XC_SET_INT0("quiet", &logging.level) },
	{ XC_SET_INT0("q", &logging.level) },
	{ XC_SET_INT("verbose", &logging.level) },
	{ XC_SET_INT("v", &logging.level) },
#ifndef HAVE_WASM
	{ XC_CALL_NONE("help", &helptext) },
	{ XC_CALL_NONE("h", &helptext) },
	{ XC_CALL_NONE("version", &versiontext) },
	{ XC_CALL_NONE("V", &versiontext) },
#endif
	{ XC_OPT_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Help texts */

#ifndef HAVE_WASM

static void helptext(void) {
#ifdef LOGGING
	puts(
"Usage: xroar [STARTUP-OPTION]... [OPTION]...\n"
"XRoar emulates the Dragon 32/64; Tandy Colour Computers 1, 2 and 3;\n"
"the Tandy MC-10; and some other similar machines or clones.\n"

"\n Startup options:\n"
#ifdef WINDOWS32
"  -C              allocate a console window\n"
#endif
"  -c CONFFILE     specify a configuration file\n"

"\n Machines:\n"
"  -default-machine NAME   default machine on startup\n"
"  -machine NAME           create or modify named machine profile\n"
"                          (-machine help for list)\n"
"    -machine-desc TEXT      machine description\n"
"    -machine-arch ARCH      machine architecture (-machine-arch help for list)\n"
"    -machine-keyboard LAYOUT\n"
"                            keyboard layout (-machine-keyboard help for list)\n"
"    -machine-cpu CPU        machine CPU (-machine-cpu help for list)\n"
"    -bas NAME               BASIC ROM to use (CoCo only)\n"
"    -extbas NAME            Extended BASIC ROM to use\n"
"    -altbas NAME            64K mode Extended BASIC ROM (Dragon 64)\n"
"    -no-bas                 disable BASIC\n"
"    -no-extbas              disable Extended BASIC\n"
"    -no-altbas              disable 64K mode Extended BASIC\n"
"    -ext-charset NAME       external character generator ROM to use\n"
"    -tv-type TYPE           TV type (-tv-type help for list)\n"
"    -tv-input INPUT         TV input (-tv-input help for list)\n"
"    -vdg-type TYPE          VDG type (6847 or 6847t1)\n"
"    -ram-org ORG            RAM organisation (-ram-org help for list)\n"
"    -ram KBYTES             amount of RAM in K\n"
"    -ram-init METHOD        RAM start pattern (-ram-init help for list)\n"
"    -machine-cart NAME      default cartridge for selected machine\n"

"\n Cartridges:\n"
"  -cart NAME              create or modify named cartridge profile\n"
"                          (-cart help for list)\n"
"    -cart-desc TEXT         cartridge description\n"
"    -cart-type TYPE         cartridge base type (-cart-type help for list)\n"
"    -cart-rom NAME          ROM image to load ($C000-)\n"
"    -cart-rom2 NAME         second ROM image to load ($E000-)\n"
"    -cart-autorun           autorun cartridge\n"
"    -cart-becker            enable becker port where supported\n"
"    -mpi-slot N             (MPI) initially select slot (0-3)\n"
"    -mpi-load-cart [N=]NAME\n"
"                            (MPI) insert cartridge into next or numbered slot\n"

"\n Becker port:\n"
"  -becker               prefer becker-enabled DOS (when picked automatically)\n"
"  -becker-ip ADDRESS    address or hostname of DriveWire server [" BECKER_IP_DEFAULT "]\n"
"  -becker-port PORT     port of DriveWire server [" BECKER_PORT_DEFAULT "]\n"

"\n Cassettes:\n"
"  -load-tape FILE           attach FILE as tape image for reading\n"
"  -tape-write FILE          open FILE for tape writing\n"
"  -tape-pan PANNING         pan stereo input (0.0 = left, 1.0 = right) [0.5]\n"
"  -tape-hysteresis H        read hysteresis as % of full scale [1]\n"
"  -no-tape-fast             disable fast tape loading\n"
"  -no-tape-pad-auto         disable CAS file short leader workaround\n"
"  -tape-rewrite             enable tape rewriting\n"
"  -tape-rewrite-gap-ms MS   gap length during tape rewriting (1-5000ms) [500]\n"
"  -tape-rewrite-leader B    rewrite leader length in bytes (1-2048) [256]\n"
"  -tape-ao-rate HZ          set tape writing frame rate\n"

"\n Floppy disks:\n"
"  -load-fdX FILE        insert disk image FILE into floppy drive X (0-3)\n"
"  -no-disk-write-back   don't default to enabling write-back for disk images\n"
"  -no-disk-auto-os9     don't try to detect headerless OS-9 JVC disk images\n"
"  -no-disk-auto-sd      don't assume single density for 10 sec/track disks\n"

"\n Hard disks:\n"
"  -load-hdX FILE        use hard disk image FILE as drive X (0-1, e.g. for ide)\n"
"  -load-sd FILE         use SD card image FILE (e.g. for mooh, nx32))\n"

"\n Keyboard:\n"
"  -kbd-layout LAYOUT      host keyboard layout (-kbd-layout help for list)\n"
"  -kbd-lang LANG          host keyboard language (-kbd-lang help for list)\n"
"  -kbd-bind HK=[pre:]DK   map host key to emulated key (pre = no translate)\n"
"  -kbd-translate          enable keyboard translation\n"
"  -type STRING            intercept ROM calls to type STRING into BASIC\n"
"  -load-text FILE         type FILE into BASIC\n"

"\n Joysticks:\n"
"  -joy NAME             configure named joystick profile (-joy help for list)\n"
"    -joy-desc TEXT        joystick description\n"
"    -joy-axis AXIS=SPEC   configure joystick axis\n"
"    -joy-button BTN=SPEC  configure joystick button\n"
"  -joy-right NAME       map right joystick\n"
"  -joy-left NAME        map left joystick\n"
"  -joy-virtual NAME     specify the 'virtual' joystick to cycle [kjoy0]\n"

"\n Printers:\n"
"  -lp-file FILE         append Dragon printer output to FILE\n"
#ifdef HAVE_POPEN
"  -lp-pipe COMMAND      pipe Dragon printer output to COMMAND\n"
#endif

"\n Files:\n"
"  -load FILE            load or attach FILE\n"
"  -run FILE             load or attach FILE and attempt autorun\n"
"  -load-fdX FILE        insert disk image FILE into floppy drive X (0-3)\n"
"  -load-hdX FILE        use hard disk image FILE as drive X (0-1, e.g. for ide)\n"
"  -load-sd FILE         use SD card image FILE (e.g. for mooh, nx32))\n"
"  -load-tape FILE       attach FILE as tape image for reading\n"
"  -tape-write FILE      open FILE for tape writing\n"
"  -load-text FILE       type FILE into BASIC\n"

"\n Firmware ROM images:\n"
"  -rompath PATH         ROM search path (colon-separated list)\n"
"  -romlist NAME=LIST    define a ROM list\n"
"  -romlist-print        print defined ROM lists\n"
"  -crclist NAME=LIST    define a ROM CRC list\n"
"  -crclist-print        print defined ROM CRC lists\n"
"  -force-crc-match      force per-architecture CRC matches\n"

"\n User interface:\n"
"  -ui MODULE            user-interface module (-ui help for list)\n"

"\n Video:\n"
"  -fs                   start emulator full-screen if possible\n"
"  -fskip FRAMES         frameskip (default: 0)\n"
"  -ccr RENDERER         cross-colour renderer (-ccr help for list)\n"
"  -gl-filter FILTER     OpenGL texture filter (-gl-filter help for list)\n"
"  -vo-pixel-fmt FMT     pixel format (-vo-pixel-fmt help for list)\n"
"  -geometry WxH+X+Y     initial emulator geometry\n"
"  -vo-picture P         initial picture area (-vo-picture help for list)\n"
"  -no-vo-scale-60hz     disable vertical scaling for 60Hz video\n"
"  -invert-text          start with text mode inverted\n"
"  -vo-brightness N      set TV brightness (0-100) [50]\n"
"  -vo-contrast N        set TV contrast (0-100) [50]\n"
"  -vo-colour N          set TV colour saturation (0-100) [50]\n"
"  -vo-hue N             set TV hue control (-179 to +180) [0]\n"
"  -vo-colour-killer     enable colour killer (disabled by default)\n"

"\n Audio:\n"
"  -ao MODULE            audio module (-ao help for list)\n"
"  -ao-device STRING     device to use for audio module\n"
"  -ao-format FMT        set audio sample format (-ao-format help for list)\n"
"  -ao-rate HZ           set audio frame rate (if supported by module)\n"
"  -ao-channels N        set number of audio channels, 1 or 2\n"
"  -ao-fragments N       set number of audio fragments\n"
"  -ao-fragment-ms MS    set audio fragment size in ms (if supported)\n"
"  -ao-fragment-frames N set audio fragment size in samples (if supported)\n"
"  -ao-buffer-ms MS      set total audio buffer size in ms (if supported)\n"
"  -ao-buffer-frames N   set total audio buffer size in samples (if supported)\n"
"  -ao-gain DB           audio gain in dB relative to 0 dBFS [-3.0]\n"
"  -ao-volume VOLUME     older way to specify audio volume, linear (0-100)\n"

"\n Debugging:\n"
#ifdef WANT_GDB_TARGET
"  -gdb                  enable GDB target\n"
"  -gdb-ip ADDRESS       address of interface for GDB target [" GDB_IP_DEFAULT "]\n"
"  -gdb-port PORT        port for GDB target to listen on [" GDB_PORT_DEFAULT "]\n"
#endif
"  -no-ratelimit         run cpu as fast as possible\n"
#ifdef TRACE
"  -trace                start with trace mode on\n"
"  -trace-timing         print timings in trace mode\n"
#endif
"  -debug-fdc FLAGS      FDC debugging (see manual, or -1 for all)\n"
"  -debug-file FLAGS     file debugging (see manual, or -1 for all)\n"
#ifdef WANT_GDB_TARGET
"  -debug-gdb FLAGS      GDB target debugging (see manual, or -1 for all)\n"
#endif
"  -debug-ui FLAGS       UI debugging (see manual, or -1 for all)\n"
"  -v, -verbose LEVEL    general debug verbosity (0-3) [1]\n"
"  -q, -quiet            equivalent to -verbose 0\n"
"  -timeout S            run for S seconds then quit\n"
"  -timeout-motoroff S   quit S seconds after tape motor switches off\n"
"  -snap-motoroff FILE   write a snapshot each time tape motor switches off\n"

"\n Other options:\n"
"  -config-print       print configuration to standard out\n"
"  -config-print-all   print configuration to standard out, including defaults\n"
"  -h, --help          display this help and exit\n"
"  -V, --version       output version information and exit\n"

"\nWhen configuring a Multi-Pak Interface (MPI), only the last configured DOS\n"
"cartridge will end up connected to the virtual drives.\n"

"\nJoystick SPECs are of the form [MODULE:][ARG[,ARG]...], from:\n"

"\nMODULE          Axis ARGs                       Button ARGs\n"
"physical        joystick-index,[-]axis-index    joystick-index,button-index\n"
"keyboard        key-name0,key-name1             key-name\n"
"mouse           screen-offset0,screen-offset1   button-number\n"

"\nFor physical joysticks a '-' before the axis index inverts the axis.  AXIS 0 is\n"
"the X-axis, and AXIS 1 the Y-axis.  BTN 0 is the only one used so far, but in\n"
"the future BTN 1 will be the second button on certain CoCo joysticks."
	);
#endif
	exit(EXIT_SUCCESS);
}

static void versiontext(void) {
#ifdef LOGGING
	printf("%s", PACKAGE_TEXT);
	puts(
"\nCopyright (C) " PACKAGE_YEAR " Ciaran Anscomb\n"
"License: GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl-3.0.html>.\n"
"This is free software: you are free to change and redistribute it.\n"
"There is NO WARRANTY, to the extent permitted by law."
	);
#endif
	exit(EXIT_SUCCESS);
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Dump all known config to stdout */

/*
 * The plan is to have proper introspection of the configuration, allowing
 * dynamic updates from a console or remotely.  Dumping of the current config
 * would then become pretty easy.
 *
 * Until then, this is a pretty awful stopgap measure.  It's liable to break if
 * a default changes or new options are added.  Be careful!
 */

#ifndef HAVE_WASM
static void config_print_all(FILE *f, _Bool all) {
	fputs("# Machines\n\n", f);
	xroar_cfg_print_string(f, all, "default-machine", private_cfg.default_machine, NULL);
	fputs("\n", f);
	machine_config_print_all(f, all);

	fputs("# Cartridges\n\n", f);
	cart_config_print_all(f, all);
	fputs("# Becker port\n", f);
	xroar_cfg_print_bool(f, all, "becker", xroar.cfg.becker.prefer, 0);
	xroar_cfg_print_string(f, all, "becker-ip", xroar.cfg.becker.ip, BECKER_IP_DEFAULT);
	xroar_cfg_print_string(f, all, "becker-port", xroar.cfg.becker.port, BECKER_PORT_DEFAULT);
	fputs("\n", f);

	fputs("# Files\n", f);
	xroar_cfg_print_string(f, all, "load-fd0", private_cfg.file.fd[0], NULL);
	xroar_cfg_print_string(f, all, "load-fd1", private_cfg.file.fd[1], NULL);
	xroar_cfg_print_string(f, all, "load-fd2", private_cfg.file.fd[2], NULL);
	xroar_cfg_print_string(f, all, "load-fd3", private_cfg.file.fd[3], NULL);
	xroar_cfg_print_string(f, all, "load-hd0", xroar.cfg.file.hd[0], NULL);
	xroar_cfg_print_string(f, all, "load-hd1", xroar.cfg.file.hd[1], NULL);
	xroar_cfg_print_string(f, all, "load-tape", private_cfg.file.tape, NULL);
	xroar_cfg_print_string(f, all, "tape-write", private_cfg.file.tape_write, NULL);
	xroar_cfg_print_string(f, all, "load-text", private_cfg.file.text, NULL);
	fputs("\n", f);

	fputs("# Cassettes\n", f);
	xroar_cfg_print_double(f, all, "tape-pan", xroar.cfg.tape.pan, 0.5);
	xroar_cfg_print_double(f, all, "tape-hysteresis", xroar.cfg.tape.hysteresis, 1.0);

	xroar_cfg_print_bool(f, all, "tape-fast", private_cfg.tape.fast, 1);
	xroar_cfg_print_bool(f, all, "tape-pad-auto", private_cfg.tape.pad_auto, 1);
	xroar_cfg_print_bool(f, all, "tape-rewrite", private_cfg.tape.rewrite, 0);
	xroar_cfg_print_int_nz(f, all, "tape-ao-rate", private_cfg.tape.ao_rate);
	fputs("\n", f);

	fputs("# Disks\n", f);
	xroar_cfg_print_bool(f, all, "disk-write-back", xroar.cfg.disk.write_back, 1);
	xroar_cfg_print_bool(f, all, "disk-auto-os9", xroar.cfg.disk.auto_os9, 1);
	xroar_cfg_print_bool(f, all, "disk-auto-sd", xroar.cfg.disk.auto_sd, 1);
	fputs("\n", f);

	fputs("# Firmware ROM images\n", f);
	xroar_cfg_print_string(f, all, "rompath", xroar.cfg.file.rompath, NULL);
	romlist_print_all(f);
	crclist_print_all(f);
	xroar_cfg_print_bool(f, all, "force-crc-match", xroar.cfg.force_crc_match, 0);
	fputs("\n", f);

	fputs("# User interface\n", f);
	xroar_cfg_print_string(f, all, "ui", private_cfg.ui_module, NULL);
	xroar_cfg_print_string(f, all, "filereq", xroar_ui_cfg.filereq, NULL);
	fputs("\n", f);

	fputs("# Video\n", f);
	xroar_cfg_print_string(f, all, "vo", xroar_ui_cfg.vo, NULL);
	xroar_cfg_print_bool(f, all, "fs", xroar_ui_cfg.vo_cfg.fullscreen, 0);
	xroar_cfg_print_int_nz(f, all, "fskip", private_cfg.vo.frameskip);
	xroar_cfg_print_enum(f, all, "ccr", private_cfg.vo.ccr, VO_CMP_CCR_5BIT, vo_cmp_ccr_list);
	xroar_cfg_print_enum(f, all, "gl-filter", xroar_ui_cfg.vo_cfg.gl_filter, ANY_AUTO, ui_gl_filter_list);
	xroar_cfg_print_enum(f, all, "vo-pixel-fmt", xroar_ui_cfg.vo_cfg.pixel_fmt, ANY_AUTO, vo_pixel_fmt_list);
	xroar_cfg_print_string(f, all, "geometry", xroar_ui_cfg.vo_cfg.geometry, NULL);
	xroar_cfg_print_enum(f, all, "vo-picture", private_cfg.vo.picture, 0, vo_viewport_list);
	xroar_cfg_print_bool(f, all, "vo-scale-60hz", private_cfg.vo.ntsc_scaling, 1);
	xroar_cfg_print_bool(f, all, "invert-text", private_cfg.vo.vdg_inverted_text, 0);
	xroar_cfg_print_int(f, all, "vo-brightness", private_cfg.vo.brightness, 50);
	xroar_cfg_print_int(f, all, "vo-contrast", private_cfg.vo.contrast, 50);
	xroar_cfg_print_int(f, all, "vo-colour", private_cfg.vo.saturation, 50);
	xroar_cfg_print_int(f, all, "vo-hue", private_cfg.vo.hue, 0);
	xroar_cfg_print_bool(f, all, "vo-colour-killer", xroar_ui_cfg.vo_cfg.colour_killer, 0);
	fputs("\n", f);

	fputs("# Audio\n", f);
	xroar_cfg_print_string(f, all, "ao", private_cfg.ao_module, NULL);
	xroar_cfg_print_string(f, all, "ao-device", xroar.cfg.ao.device, NULL);
	xroar_cfg_print_enum(f, all, "ao-format", xroar.cfg.ao.format, SOUND_FMT_NULL, ao_format_list);
	xroar_cfg_print_int_nz(f, all, "ao-rate", xroar.cfg.ao.rate);
	xroar_cfg_print_int_nz(f, all, "ao-channels", xroar.cfg.ao.channels);
	xroar_cfg_print_int_nz(f, all, "ao-fragments", xroar.cfg.ao.fragments);
	xroar_cfg_print_int_nz(f, all, "ao-fragment-ms", xroar.cfg.ao.fragment_ms);
	xroar_cfg_print_int_nz(f, all, "ao-fragment-frames", xroar.cfg.ao.fragment_nframes);
	xroar_cfg_print_int_nz(f, all, "ao-buffer-ms", xroar.cfg.ao.buffer_ms);
	xroar_cfg_print_int_nz(f, all, "ao-buffer-frames", xroar.cfg.ao.buffer_nframes);
	xroar_cfg_print_double(f, all, "ao-gain", private_cfg.ao.gain, -3.0);
	xroar_cfg_print_int(f, all, "ao-volume", private_cfg.ao.volume, -1);
	fputs("\n", f);

	fputs("# Keyboard\n", f);
	xroar_cfg_print_enum(f, all, "kbd-layout", xroar.cfg.kbd.layout, hk_layout_auto, hkbd_layout_list);
	xroar_cfg_print_enum(f, all, "kbd-lang", xroar.cfg.kbd.lang, hk_lang_auto, hkbd_lang_list);
	xroar_cfg_print_bool(f, all, "kbd-translate", xroar.cfg.kbd.translate, 0);
	for (struct slist *l = private_cfg.kbd.type_list; l; l = l->next) {
		sds s = sdsx_quote(l->data);
		fprintf(f, "type %s\n", s);
		sdsfree(s);
	}
	fputs("\n", f);

	fputs("# Joysticks\n", f);
	joystick_config_print_all(f, all);
	xroar_cfg_print_string(f, all, "joy-right", private_cfg.joy.right, "joy0");
	xroar_cfg_print_string(f, all, "joy-left", private_cfg.joy.left, "joy1");
	xroar_cfg_print_string(f, all, "joy-virtual", private_cfg.joy.virtual, "kjoy0");
	fputs("\n", f);

	fputs("# Printing\n", f);
	xroar_cfg_print_string(f, all, "lp-file", private_cfg.printer.file, NULL);
	xroar_cfg_print_string(f, all, "lp-pipe", private_cfg.printer.pipe, NULL);
	fputs("\n", f);

	fputs("# Debugging\n", f);
	xroar_cfg_print_bool(f, all, "gdb", xroar.cfg.debug.gdb, 0);
	xroar_cfg_print_string(f, all, "gdb-ip", xroar.cfg.debug.gdb_ip, GDB_IP_DEFAULT);
	xroar_cfg_print_string(f, all, "gdb-port", xroar.cfg.debug.gdb_port, GDB_PORT_DEFAULT);
	xroar_cfg_print_bool(f, all, "ratelimit", private_cfg.debug.ratelimit, 1);
	xroar_cfg_print_bool(f, all, "trace", logging.trace_cpu, 0);
	xroar_cfg_print_bool(f, all, "trace-timing", logging.trace_cpu_timing, 0);
	xroar_cfg_print_flags(f, all, "debug-fdc", logging.debug_fdc);
	xroar_cfg_print_flags(f, all, "debug-file", logging.debug_file);
	xroar_cfg_print_flags(f, all, "debug-gdb", logging.debug_gdb);
	xroar_cfg_print_flags(f, all, "debug-ui", logging.debug_ui);
	xroar_cfg_print_string(f, all, "timeout", private_cfg.debug.timeout, NULL);
	xroar_cfg_print_string(f, all, "timeout-motoroff", xroar.cfg.debug.timeout_motoroff, NULL);
	xroar_cfg_print_string(f, all, "snap-motoroff", xroar.cfg.debug.snap_motoroff, NULL);
	fputs("\n", f);
}
#endif

/* Helper functions for config printing */

static int cfg_print_indent_level = 0;

void xroar_cfg_print_inc_indent(void) {
	cfg_print_indent_level++;
}

void xroar_cfg_print_dec_indent(void) {
	assert(cfg_print_indent_level > 0);
	cfg_print_indent_level--;
}

void xroar_cfg_print_indent(FILE *f) {
	for (int i = 0; i < cfg_print_indent_level; i++)
		fprintf(f, "  ");
}

void xroar_cfg_print_bool(FILE *f, _Bool all, char const *opt, int value, int normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	if (value >= 0) {
		if (!value)
			fprintf(f, "no-");
		fprintf(f, "%s\n", opt);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_int(FILE *f, _Bool all, char const *opt, int value, int normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s %d\n", opt, value);
}

void xroar_cfg_print_int_nz(FILE *f, _Bool all, char const *opt, int value) {
	if (!all && value == 0)
		return;
	xroar_cfg_print_indent(f);
	if (value != 0) {
		fprintf(f, "%s %d\n", opt, value);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_double(FILE *f, _Bool all, char const *opt, double value, double normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s %.4f\n", opt, value);
}

void xroar_cfg_print_flags(FILE *f, _Bool all, char const *opt, unsigned value) {
	if (!all && value == 0)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s 0x%x\n", opt, value);
}

void xroar_cfg_print_string(FILE *f, _Bool all, char const *opt, char const *value, char const *normal) {
	if (!all && !value)
		return;
	xroar_cfg_print_indent(f);
	if (value || normal) {
		char const *tmp = value ? value : normal;
		sds str = sdsx_quote_str(tmp);
		fprintf(f, "%s %s\n", opt, str);
		sdsfree(str);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_enum(FILE *f, _Bool all, char const *opt, int value, int normal, struct xconfig_enum const *e) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	for (int i = 0; e[i].name; i++) {
		if (value == e[i].value) {
			fprintf(f, "%s %s\n", opt, e[i].name);
			return;
		}
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_string_list(FILE *f, _Bool all, char const *opt, struct slist *l) {
	if (!all  && !l)
		return;
	xroar_cfg_print_indent(f);
	if (l) {
		for (; l; l = l->next) {
			char const *s = l->data;
			fprintf(f, "%s %s\n", opt, s);
		}
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}
