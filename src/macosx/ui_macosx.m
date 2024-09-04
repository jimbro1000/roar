/** \file
 *
 *  \brief Mac OS X+ user-interface module.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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
 *  Based on SDLMain.m - main entry point for our Cocoa-ized SDL app
 *
 *  Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
 *
 *  Non-NIB-Code & other changes: Max Horn <max@quendi.de>
 *
 *  Feel free to customize this file to suit your needs.
 */

#include "top-config.h"

#include <SDL.h>
#include <sys/param.h> /* for MAXPATHLEN */
#include <unistd.h>

#import <Cocoa/Cocoa.h>
//#import <AppKit/AppKit.h>

#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "cart.h"
#include "joystick.h"
#include "keyboard.h"
#include "machine.h"
#include "module.h"
#include "printer.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"
#include "sdl2/common.h"

#define TAG(ui_tag,value) ((((ui_tag) & 0x7f) << 24) | ((value) & 0xffffff))
#define TAG_TYPE(t) (((t) >> 24) & 0x7f)
#define TAG_VALUE(t) ((t) & 0xffffff)

#define TAG_TYPE_MASK (0x7f << 24)
#define TAG_VALUE_MASK (0xffffff)

#define TAG_SIMPLE_ACTION (1 << 24)

#define TAG_MACHINE (2 << 24)

#define TAG_CARTRIDGE (3 << 24)

#define TAG_TAPE_FLAGS (4 << 24)
#define TAG_TAPE_PLAY_PAUSE (19 << 24)

#define TAG_INSERT_DISK (5 << 24)
#define TAG_NEW_DISK (6 << 24)
#define TAG_WRITE_ENABLE (7 << 24)
#define TAG_WRITE_BACK (8 << 24)
#define TAG_EJECT_DISK (17 << 24)

#define TAG_FULLSCREEN (9 << 24)
#define TAG_VDG_INVERSE (16 << 24)
#define TAG_TV_INPUT (10 << 24)
#define TAG_TV_PICTURE (24 << 24)
#define TAG_NTSC_SCALING (25 << 24)
#define TAG_CCR (18 << 24)
#define TAG_CMP_FS (20 << 24)
#define TAG_CMP_FSC (21 << 24)
#define TAG_CMP_SYSTEM (22 << 24)
#define TAG_CMP_COLOUR_KILLER (23 << 24)

#define TAG_KEYMAP (12 << 24)
#define TAG_KBD_TRANSLATE (13 << 24)
#define TAG_JOY_RIGHT (14 << 24)
#define TAG_JOY_LEFT (15 << 24)

#define TAG_PRINT (26 << 24)

enum {
	TAG_QUIT,
	TAG_RESET_SOFT,
	TAG_RESET_HARD,
	TAG_FILE_LOAD,
	TAG_FILE_RUN,
	TAG_FILE_SAVE_SNAPSHOT,
	TAG_FILE_SCREENSHOT,
	TAG_TAPE_INPUT,
	TAG_TAPE_OUTPUT,
	TAG_TAPE_INPUT_REWIND,
	TAG_PRINT_FLUSH,
	TAG_ZOOM_IN,
	TAG_ZOOM_OUT,
	TAG_ZOOM_RESET,
	TAG_JOY_SWAP,
};

@interface SDLMain : NSObject <NSApplicationDelegate>
@end

/* For some reaon, Apple removed setAppleMenu from the headers in 10.4, but the
 * method still is there and works.  To avoid warnings, we declare it ourselves
 * here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

static int gArgc;
static char **gArgv;
static BOOL gFinderLaunch;
static BOOL gCalledAppMainline = FALSE;

static NSString *get_application_name(void) {
	const NSDictionary *dict;
	NSString *app_name = 0;

	/* Determine the application name */
	dict = (const NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
	if (dict)
		app_name = [dict objectForKey: @"CFBundleName"];

	if (![app_name length])
		app_name = [[NSProcessInfo processInfo] processName];

	return app_name;
}

static int current_picture = 0;
static int current_cc = 0;
static int current_ccr = 0;
static int current_machine = 0;
static int current_cartridge = 0;
static int current_joy_right = 0;
static int current_joy_left = 0;
static int current_keymap = 0;
static int is_fullscreen = 0;
static int tape_is_playing = 0;
static int vdg_inverted = 0;
static int is_kbd_translate = 0;
static _Bool disk_write_enable[4] = { 1, 1, 1, 1 };
static _Bool disk_write_back[4] = { 0, 0, 0, 0 };
static int print_destination = 0;

/* Setting this to true is a massive hack so that cocoa file dialogues receive
 * keypresses.  Ideally, need to sort SDL out or turn this into a regular
 * OpenGL application.  SDL2 in 2019 note: not sure if this is needed now?  */
int cocoa_super_all_keys = 0;

@interface XRoarApplication : NSApplication
@end

@implementation XRoarApplication

- (void)sendEvent:(NSEvent *)anEvent {
	switch ([anEvent type]) {
		case NSEventTypeKeyDown:
		case NSEventTypeKeyUp:
			if (cocoa_super_all_keys || ([anEvent modifierFlags] & NSEventModifierFlagCommand)) {
				[super sendEvent:anEvent];
			}
			break;
		default:
			[super sendEvent:anEvent];
			break;
	}
}

+ (void)registerUserDefaults {
	NSDictionary *appDefaults = [[NSDictionary alloc] initWithObjectsAndKeys:
		[NSNumber numberWithBool:NO], @"AppleMomentumScrollSupported",
		[NSNumber numberWithBool:NO], @"ApplePressAndHoldEnabled",
		[NSNumber numberWithBool:YES], @"ApplePersistenceIgnoreState",
		nil];
	[[NSUserDefaults standardUserDefaults] registerDefaults:appDefaults];
	[appDefaults release];
}

- (void)do_set_state:(id)sender {
	int tag = [sender tag];
	int tag_type = tag & TAG_TYPE_MASK;
	int tag_value = tag & TAG_VALUE_MASK;

	// Try and ensure that the keydown event that (maybe) caused this
	// menuitem dispatch is not then handled by the main loop as well.
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);

	switch (tag_type) {

	/* Simple actions: */
	case TAG_SIMPLE_ACTION:
		switch (tag_value) {
		case TAG_QUIT:
			{
				SDL_Event event;
				event.type = SDL_QUIT;
				SDL_PushEvent(&event);
			}
			break;
		case TAG_RESET_SOFT:
			xroar_soft_reset();
			break;
		case TAG_RESET_HARD:
			xroar_hard_reset();
			break;
		case TAG_FILE_RUN:
			xroar_run_file();
			break;
		case TAG_FILE_LOAD:
			xroar_load_file();
			break;
		case TAG_FILE_SAVE_SNAPSHOT:
			xroar_save_snapshot();
			break;
		case TAG_FILE_SCREENSHOT:
			xroar_screenshot();
			break;
		case TAG_TAPE_INPUT:
			xroar_insert_input_tape();
			break;
		case TAG_TAPE_OUTPUT:
			xroar_insert_output_tape();
			break;
		case TAG_TAPE_INPUT_REWIND:
			if (xroar.tape_interface->tape_input) {
				tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
			}
			break;
		case TAG_PRINT_FLUSH:
			xroar_flush_printer();
			break;
		case TAG_ZOOM_IN:
			vo_zoom_in(xroar.vo_interface);
			break;
		case TAG_ZOOM_OUT:
			vo_zoom_out(xroar.vo_interface);
			break;
		case TAG_ZOOM_RESET:
			vo_zoom_reset(xroar.vo_interface);
			break;
		case TAG_JOY_SWAP:
			joystick_swap();
			break;
		default:
			break;
		}
		break;

	/* Machines: */
	case TAG_MACHINE:
		current_machine = tag;
		xroar_set_machine(0, tag_value);
		break;

	/* Cartridges: */
	case TAG_CARTRIDGE:
		{
			if (tag_value & (1 << 23))
				tag_value = -1;
			struct cart_config *cc = cart_config_by_id(tag_value);
			xroar_set_cart(0, cc ? cc->name : NULL);
		}
		break;

	/* Cassettes: */
	case TAG_TAPE_FLAGS:
		tape_set_state(xroar.tape_interface, tape_get_state(xroar.tape_interface) ^ tag_value);
		break;
	case TAG_TAPE_PLAY_PAUSE:
		tape_is_playing = !tape_is_playing;
		tape_set_playing(xroar.tape_interface, tape_is_playing, 0);
		break;

	/* Disks: */
	case TAG_INSERT_DISK:
		xroar_insert_disk(tag_value);
		break;
	case TAG_NEW_DISK:
		xroar_new_disk(tag_value);
		break;
	case TAG_WRITE_ENABLE:
		disk_write_enable[tag_value] = !disk_write_enable[tag_value];
		xroar_set_write_enable(1, tag_value, disk_write_enable[tag_value]);
		break;
	case TAG_WRITE_BACK:
		disk_write_back[tag_value] = !disk_write_back[tag_value];
		xroar_set_write_back(1, tag_value, disk_write_back[tag_value]);
		break;
	case TAG_EJECT_DISK:
		xroar_eject_disk(tag_value);
		break;

	// Printers:
	case TAG_PRINT:
		if (tag_value == PRINTER_DESTINATION_FILE) {
			char *filename = DELEGATE_CALL(global_uisdl2->ui_interface.filereq_interface->save_filename, "Print to file");
			if (filename) {
				xroar_set_printer_file(1, filename);
			}
		}
		xroar_set_printer_destination(1, tag_value);
		break;

	/* Video: */
	case TAG_FULLSCREEN:
		is_fullscreen = !is_fullscreen;
		xroar_set_fullscreen(0, is_fullscreen);
		break;
	case TAG_TV_INPUT:
		current_cc = tag;
		xroar_set_tv_input(0, tag_value);
		break;
	case TAG_TV_PICTURE:
		current_picture = tag;
		xroar_set_picture(0, tag_value);
		break;
	case TAG_NTSC_SCALING:
		vo_set_ntsc_scaling(xroar.vo_interface, 0, !xroar.vo_interface->renderer->ntsc_scaling);
		break;
	case TAG_CCR:
		current_ccr = tag;
		xroar_set_ccr(0, tag_value);
		break;
	case TAG_CMP_FS:
		vo_set_cmp_fs(xroar.vo_interface, 0, tag_value);
		break;
	case TAG_CMP_FSC:
		vo_set_cmp_fsc(xroar.vo_interface, 0, tag_value);
		break;
	case TAG_CMP_SYSTEM:
		vo_set_cmp_system(xroar.vo_interface, 0, tag_value);
		break;
	case TAG_CMP_COLOUR_KILLER:
		vo_set_cmp_colour_killer(xroar.vo_interface, 0, !xroar.vo_interface->renderer->cmp.colour_killer);
		break;
	case TAG_VDG_INVERSE:
		vdg_inverted = !vdg_inverted;
		xroar_set_vdg_inverted_text(0, vdg_inverted);
		break;

	/* Keyboard: */
	case TAG_KEYMAP:
		current_keymap = tag;
		xroar_set_keyboard_type(0, tag_value);
		break;
	case TAG_KBD_TRANSLATE:
		is_kbd_translate = !is_kbd_translate;
		xroar_set_kbd_translate(1, is_kbd_translate);
		break;

	/* Joysticks: */
	case TAG_JOY_RIGHT:
		{
			struct joystick_config *jc = joystick_config_by_id(tag_value);
			xroar_set_joystick(1, 0, jc ? jc->name : NULL);
		}
		break;
	case TAG_JOY_LEFT:
		{
			struct joystick_config *jc = joystick_config_by_id(tag_value);
			xroar_set_joystick(1, 1, jc ? jc->name : NULL);
		}
		break;

	default:
		break;
	}
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
	int tag = [item tag];
	int tag_type = tag & TAG_TYPE_MASK;
	int tag_value = tag & TAG_VALUE_MASK;

	struct vo_render *vr = xroar.vo_interface ? xroar.vo_interface->renderer : NULL;

	switch (tag_type) {

	case TAG_MACHINE:
		[item setState:((tag == current_machine) ? NSOnState : NSOffState)];
		break;

	case TAG_CARTRIDGE:
		[item setState:((tag == current_cartridge) ? NSOnState : NSOffState)];
		break;

	case TAG_TAPE_FLAGS:
		[item setState:((tape_get_state(xroar.tape_interface) & tag_value) ? NSOnState : NSOffState)];
		break;
	case TAG_TAPE_PLAY_PAUSE:
		[item setState:(tape_is_playing ? NSOnState : NSOffState)];
		break;

	case TAG_WRITE_ENABLE:
		[item setState:(disk_write_enable[tag_value] ? NSOnState : NSOffState)];
		break;
	case TAG_WRITE_BACK:
		[item setState:(disk_write_back[tag_value] ? NSOnState : NSOffState)];
		break;

	case TAG_PRINT:
		[item setState:((tag_value == print_destination) ? NSOnState : NSOffState)];
		break;

	case TAG_FULLSCREEN:
		[item setState:(is_fullscreen ? NSOnState : NSOffState)];
		break;
	case TAG_VDG_INVERSE:
		[item setState:(vdg_inverted ? NSOnState : NSOffState)];
		break;
	case TAG_TV_INPUT:
		[item setState:((tag == current_cc) ? NSOnState : NSOffState)];
		break;
	case TAG_TV_PICTURE:
		[item setState:((tag == current_picture) ? NSOnState : NSOffState)];
		break;
	case TAG_NTSC_SCALING:
		[item setState:((vr && vr->ntsc_scaling) ? NSOnState : NSOffState)];
		break;
	case TAG_CCR:
		[item setState:((tag == current_ccr) ? NSOnState : NSOffState)];
		break;
	case TAG_CMP_FS:
		[item setState:((vr && vr->cmp.fs == tag_value) ? NSOnState : NSOffState)];
		break;
	case TAG_CMP_FSC:
		[item setState:((vr && vr->cmp.fsc == tag_value) ? NSOnState : NSOffState)];
		break;
	case TAG_CMP_SYSTEM:
		[item setState:((vr && vr->cmp.system == tag_value) ? NSOnState : NSOffState)];
		break;
	case TAG_CMP_COLOUR_KILLER:
		[item setState:((vr && vr->cmp.colour_killer) ? NSOnState : NSOffState)];
		break;

	case TAG_KEYMAP:
		[item setState:((tag == current_keymap) ? NSOnState : NSOffState)];
		break;
	case TAG_KBD_TRANSLATE:
		[item setState:(is_kbd_translate ? NSOnState : NSOffState)];
		break;

	case TAG_JOY_RIGHT:
		[item setState:((tag == current_joy_right) ? NSOnState : NSOffState)];
		break;
	case TAG_JOY_LEFT:
		[item setState:((tag == current_joy_left) ? NSOnState : NSOffState)];
		break;

	}
	return YES;
}

@end

/* The main class of the application, the application's delegate */
@implementation SDLMain

/* Set the working directory to the .app's parent directory */
- (void)setupWorkingDirectory:(BOOL)shouldChdir {
	if (shouldChdir) {
		char parentdir[MAXPATHLEN];
		CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
		if (CFURLGetFileSystemRepresentation(url2, 1, (UInt8 *)parentdir, MAXPATHLEN)) {
			chdir(parentdir);   /* chdir to the binary app's parent */
		}
		CFRelease(url);
		CFRelease(url2);
	}
}

static void setApplicationMenu(void) {
	/* warning: this code is very odd */
	NSMenu *apple_menu;
	NSMenuItem *item;
	NSString *title;
	NSString *app_name;

	app_name = get_application_name();
	apple_menu = [[NSMenu alloc] initWithTitle:@""];

	/* Add menu items */
	title = [@"About " stringByAppendingString:app_name];
	[apple_menu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

	[apple_menu addItem:[NSMenuItem separatorItem]];

	title = [@"Hide " stringByAppendingString:app_name];
	[apple_menu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

	item = (NSMenuItem *)[apple_menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	[item setKeyEquivalentModifierMask:(NSEventModifierFlagOption|NSEventModifierFlagCommand)];

	[apple_menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

	[apple_menu addItem:[NSMenuItem separatorItem]];

	title = [@"Quit " stringByAppendingString:app_name];
	item = [[NSMenuItem alloc] initWithTitle:title action:@selector(do_set_state:) keyEquivalent:@"q"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_QUIT)];
	[apple_menu addItem:item];

	/* Put menu into the menubar */
	item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
	[item setSubmenu:apple_menu];
	[[NSApp mainMenu] addItem:item];

	/* Tell the application object that this is now the application menu */
	[NSApp setAppleMenu:apple_menu];

	/* Finally give up our references to the objects */
	[apple_menu release];
	[item release];
}

/* Create File menu */
static void setup_file_menu(void) {
	NSMenu *file_menu;
	NSMenuItem *file_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;
	NSString *tmp;

	file_menu = [[NSMenu alloc] initWithTitle:@"File"];

	tmp = [NSString stringWithFormat:@"Run%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"L"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_FILE_RUN)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

	tmp = [NSString stringWithFormat:@"Load%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"l"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_FILE_LOAD)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Cassette"];

	tmp = [NSString stringWithFormat:@"Input Tape%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_TAPE_INPUT)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	tmp = [NSString stringWithFormat:@"Output Tape%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"w"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_TAPE_OUTPUT)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Rewind Input Tape" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_TAPE_INPUT_REWIND)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Play" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:TAG_TAPE_PLAY_PAUSE];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Fast Loading" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_TAPE_FLAGS | TAPE_FAST)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"CAS Padding" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_TAPE_FLAGS | TAPE_PAD_AUTO)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Rewrite" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_TAPE_FLAGS | TAPE_REWRITE)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Cassette" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[file_menu addItem:item];
	[item release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	int drive;
	for (drive = 0; drive < 4; drive++) {
		NSString *title = [NSString stringWithFormat:@"Drive %d", drive+1];
		NSString *key1 = [NSString stringWithFormat:@"%d", drive+1];
		NSString *key2 = [NSString stringWithFormat:@"%d", drive+5];
		NSString *tmp;

		submenu = [[NSMenu alloc] initWithTitle:title];

		tmp = [NSString stringWithFormat:@"Insert Disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:key1];
		[item setTag:(TAG_INSERT_DISK | drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		tmp = [NSString stringWithFormat:@"New Disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:key1];
		[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
		[item setTag:(TAG_NEW_DISK | drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		[submenu addItem:[NSMenuItem separatorItem]];

		item = [[NSMenuItem alloc] initWithTitle:@"Write Enable" action:@selector(do_set_state:) keyEquivalent:key2];
		[item setTag:(TAG_WRITE_ENABLE | drive)];
		[submenu addItem:item];
		[item release];

		item = [[NSMenuItem alloc] initWithTitle:@"Write Back" action:@selector(do_set_state:) keyEquivalent:key2];
		[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
		[item setTag:(TAG_WRITE_BACK | drive)];
		[submenu addItem:item];
		[item release];

		[submenu addItem:[NSMenuItem separatorItem]];

		tmp = [NSString stringWithFormat:@"Eject Disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_EJECT_DISK | drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
		[item setSubmenu:submenu];
		[file_menu addItem:item];
		[item release];

		[key2 release];
		[key1 release];
		[title release];
	}

	[file_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Printer"];

	item = [[NSMenuItem alloc] initWithTitle:@"No printer" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_PRINT | PRINTER_DESTINATION_NONE)];
	[submenu addItem:item];
	[item release];

	tmp = [NSString stringWithFormat:@"Print to file%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_PRINT | PRINTER_DESTINATION_FILE)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	item = [[NSMenuItem alloc] initWithTitle:@"Print to pipe" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_PRINT | PRINTER_DESTINATION_PIPE)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Flush" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_PRINT_FLUSH)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Printer" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[file_menu addItem:item];
	[item release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	tmp = [NSString stringWithFormat:@"Save Snapshot%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"s"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_FILE_SAVE_SNAPSHOT)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

#ifdef HAVE_PNG
	[file_menu addItem:[NSMenuItem separatorItem]];

	tmp = [NSString stringWithFormat:@"Screenshot to PNG%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_FILE_SCREENSHOT)];
	[file_menu addItem:item];
	[item release];
	[tmp release];
#endif

	file_menu_item = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
	[file_menu_item setSubmenu:file_menu];
	[[NSApp mainMenu] addItem:file_menu_item];
	[file_menu_item release];
}

static NSMenu *view_menu;

/* Create View menu */
static void setup_view_menu(void) {
	NSMenuItem *view_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;
	int i;

	view_menu = [[NSMenu alloc] initWithTitle:@"View"];

	submenu = [[NSMenu alloc] initWithTitle:@"TV Input"];

	for (i = 0; machine_tv_input_list[i].name; i++) {
		if (!machine_tv_input_list[i].description)
			continue;
		NSString *s = [[NSString alloc] initWithUTF8String:machine_tv_input_list[i].description];
		item = [[NSMenuItem alloc] initWithTitle:s action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_TV_INPUT | machine_tv_input_list[i].value)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[submenu addItem:item];
		[item release];
		[s release];
	}

	item = [[NSMenuItem alloc] initWithTitle:@"TV Input" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Picture Area"];

	for (i = 0; i < NUM_VO_PICTURE; i++) {
		NSString *s = [[NSString alloc] initWithUTF8String:vo_picture_name[i]];
		item = [[NSMenuItem alloc] initWithTitle:s action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_TV_PICTURE | i)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[submenu addItem:item];
		[item release];
		[s release];
	}

	item = [[NSMenuItem alloc] initWithTitle:@"Picture Area" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"60Hz Scaling" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:TAG_NTSC_SCALING];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite Rendering"];

	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CCR | VO_CMP_CCR_PALETTE)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Simple (2-bit LUT)" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CCR | VO_CMP_CCR_2BIT)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"5-bit LUT" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CCR | VO_CMP_CCR_5BIT)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Partial NTSC" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CCR | VO_CMP_CCR_PARTIAL)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Simulated" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CCR | VO_CMP_CCR_SIMULATED)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Composite Rendering" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite Options"];

	item = [[NSMenuItem alloc] initWithTitle:@"F(s) = 14.31818 MHz" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_FS | VO_RENDER_FS_14_31818)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"F(s) = 14.218 MHz" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_FS | VO_RENDER_FS_14_218)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"F(s) = 14.23753 MHz" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_FS | VO_RENDER_FS_14_23753)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"F(sc) = 4.43361875 MHz" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_FSC | VO_RENDER_FSC_4_43361875)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"F(sc) = 3.579545 MHz" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_FSC | VO_RENDER_FSC_3_579545)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"PAL-I" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_SYSTEM | VO_RENDER_SYSTEM_PAL_I)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"PAL-M" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_SYSTEM | VO_RENDER_SYSTEM_PAL_M)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"NTSC" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CMP_SYSTEM | VO_RENDER_SYSTEM_NTSC)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Colour Killer" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:TAG_CMP_COLOUR_KILLER];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Composite Options" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Inverse Text" action:@selector(do_set_state:) keyEquivalent:@"i"];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:TAG_VDG_INVERSE];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Zoom"];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom In" action:@selector(do_set_state:) keyEquivalent:@"+"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_ZOOM_IN)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom Out" action:@selector(do_set_state:) keyEquivalent:@"-"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_ZOOM_OUT)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Reset" action:@selector(do_set_state:) keyEquivalent:@"0"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_ZOOM_RESET)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Full Screen" action:@selector(do_set_state:) keyEquivalent:@"f"];
	[item setTag:TAG_FULLSCREEN];
	[view_menu addItem:item];
	[item release];

	view_menu_item = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
	[view_menu_item setSubmenu:view_menu];
	[[NSApp mainMenu] addItem:view_menu_item];
	[view_menu_item release];
}

static NSMenu *machine_menu;
static NSMenu *cartridge_menu;
static NSMenu *joy_right_menu;
static NSMenu *joy_left_menu;

/* Create Machine menu */
static void setup_hardware_menu(void) {
	NSMenu *hardware_menu;
	NSMenuItem *hardware_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;

	hardware_menu = [[NSMenu alloc] initWithTitle:@"Hardware"];

	machine_menu = [[NSMenu alloc] initWithTitle:@"Machine"];
	item = [[NSMenuItem alloc] initWithTitle:@"Machine" action:nil keyEquivalent:@""];
	[item setSubmenu:machine_menu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	cartridge_menu = [[NSMenu alloc] initWithTitle:@"Cartridge"];
	item = [[NSMenuItem alloc] initWithTitle:@"Cartridge" action:nil keyEquivalent:@""];
	[item setSubmenu:cartridge_menu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Keyboard Map"];

	item = [[NSMenuItem alloc] initWithTitle:@"Dragon Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_dragon)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Dragon 200-E Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_dragon200e)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"CoCo Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_coco)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"CoCo 3 Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_coco3)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"MC-10 Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_mc10)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Alice Layout" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_KEYMAP | dkbd_layout_alice)];
	[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard Map" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	joy_right_menu = [[NSMenu alloc] initWithTitle:@"Right Joystick"];
	item = [[NSMenuItem alloc] initWithTitle:@"Right Joystick" action:nil keyEquivalent:@""];
	[item setSubmenu:joy_right_menu];
	[hardware_menu addItem:item];
	[item release];

	joy_left_menu = [[NSMenu alloc] initWithTitle:@"Left Joystick"];
	item = [[NSMenuItem alloc] initWithTitle:@"Left Joystick" action:nil keyEquivalent:@""];
	[item setSubmenu:joy_left_menu];
	[hardware_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Swap Joysticks" action:@selector(do_set_state:) keyEquivalent:@"J"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_JOY_SWAP)];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Soft Reset" action:@selector(do_set_state:) keyEquivalent:@"r"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_RESET_SOFT)];
	[hardware_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Hard Reset" action:@selector(do_set_state:) keyEquivalent:@"R"];
	[item setTag:(TAG_SIMPLE_ACTION | TAG_RESET_HARD)];
	[hardware_menu addItem:item];
	[item release];

	hardware_menu_item = [[NSMenuItem alloc] initWithTitle:@"Machine" action:nil keyEquivalent:@""];
	[hardware_menu_item setSubmenu:hardware_menu];
	[[NSApp mainMenu] addItem:hardware_menu_item];
	[hardware_menu_item release];
}

/* Create Tool menu */
static void setup_tool_menu(void) {
	NSMenu *tool_menu;
	NSMenuItem *tool_menu_item;
	NSMenuItem *item;

	tool_menu = [[NSMenu alloc] initWithTitle:@"Tool"];

	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard Translation" action:@selector(do_set_state:) keyEquivalent:@"z"];
	[item setTag:TAG_KBD_TRANSLATE];
	[tool_menu addItem:item];
	[item release];

	tool_menu_item = [[NSMenuItem alloc] initWithTitle:@"Tool" action:nil keyEquivalent:@""];
	[tool_menu_item setSubmenu:tool_menu];
	[[NSApp mainMenu] addItem:tool_menu_item];
	[tool_menu_item release];
}

/* Create a window menu */
static void setup_window_menu(void) {
	NSMenu *window_menu;
	NSMenuItem *window_menu_item;
	NSMenuItem *item;

	window_menu = [[NSMenu alloc] initWithTitle:@"Window"];

	/* "Minimize" item */
	item = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
	[window_menu addItem:item];
	[item release];

	/* Put menu into the menubar */
	window_menu_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
	[window_menu_item setSubmenu:window_menu];
	[[NSApp mainMenu] addItem:window_menu_item];

	/* Tell the application object that this is now the window menu */
	[NSApp setWindowsMenu:window_menu];

	/* Finally give up our references to the objects */
	[window_menu release];
	[window_menu_item release];
}

#if 0
/* Replacement for NSApplicationMain */
static void CustomApplicationMain(int argc, char **argv) {
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	SDLMain *sdlMain;

	(void)argc;
	(void)argv;

	/* Create SDLMain and make it the app delegate */
	sdlMain = [[SDLMain alloc] init];
	[NSApp setDelegate:sdlMain];

	/* Start the main event loop */
	[NSApp run];

	[sdlMain release];
	[pool release];
}
#endif

/*
 * Catch document open requests...this lets us notice files when the app was
 * launched by double-clicking a document, or when a document was
 * dragged/dropped on the app's icon. You need to have a CFBundleDocumentsType
 * section in your Info.plist to get this message, apparently.
 *
 * Files are added to gArgv, so to the app, they'll look like command line
 * arguments. Previously, apps launched from the finder had nothing but an
 * argv[0].
 *
 * This message may be received multiple times to open several docs on launch.
 *
 * This message is ignored once the app's mainline has been called.
 */
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename {
	const char *temparg;
	size_t arglen;
	char *arg;
	char **newargv;

	(void)theApplication;

	if (!gFinderLaunch)  /* MacOS is passing command line args. */
		return FALSE;

	if (gCalledAppMainline)  /* app has started, ignore this document. */
		return FALSE;

	temparg = [filename UTF8String];
	arglen = SDL_strlen(temparg) + 1;
	arg = (char *)SDL_malloc(arglen);
	if (arg == NULL)
		return FALSE;

	newargv = (char **)realloc(gArgv, sizeof(char *) * (gArgc + 2));
	if (newargv == NULL) {
		SDL_free(arg);
		return FALSE;
	}
	gArgv = newargv;

	SDL_strlcpy(arg, temparg, arglen);
	gArgv[gArgc++] = arg;
	gArgv[gArgc] = NULL;
	return TRUE;
}


/* Called when the internal event loop has just started running */
- (void)applicationDidFinishLaunching: (NSNotification *)note {
	(void)note;
	// Set the working directory to the .app's parent directory
	[self setupWorkingDirectory:gFinderLaunch];
	/* Doesn't seem present in SDL2:
	// Pass keypresses etc. to Cocoa
	setenv("SDL_ENABLEAPPEVENTS", "1", 1); */
	// Hand off to main application code
	gCalledAppMainline = TRUE;

	// Seems to be necessary to make the menu bar work (without it, you
	// have to focus something else then return).  I'm sure there's a good
	// reason for that.
	[NSApp activateIgnoringOtherApps:YES];
	[XRoarApplication registerUserDefaults];
}

@end

/* Called from ui_sdl_new() _before_ initialising SDL video. */
void cocoa_register_app(void) {

	// Ensure the application object is initialised
	[XRoarApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

	// Set up the menubar
	[NSApp setMainMenu:[[NSMenu alloc] init]];
	setApplicationMenu();
	setup_file_menu();
	setup_view_menu();
	setup_hardware_menu();
	setup_tool_menu();
	setup_window_menu();

	[NSApp finishLaunching];

	// More recent macosx adds some weird tab bar stuff to the view menu by
	// default.  I'm sure they had a good reason...
	[NSWindow setAllowsAutomaticWindowTabbing: NO];

	SDLMain *appDelegate = [[SDLMain alloc] init];
	[(NSApplication *)NSApp setDelegate:appDelegate];
}

#if 0
#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main(int argc, char **argv) {
	/* Copy the arguments into a global variable */
	/* This is passed if we are launched by double-clicking */
	if (argc >= 2 && strncmp(argv[1], "-psn", 4) == 0) {
		gArgv = (char **)SDL_malloc(sizeof(char *) * 2);
		gArgv[0] = argv[0];
		gArgv[1] = NULL;
		gArgc = 1;
		gFinderLaunch = YES;
	} else {
		int i;
		gArgc = argc;
		gArgv = (char **)SDL_malloc(sizeof(char *) * (argc+1));
		for (i = 0; i <= argc; i++)
			gArgv[i] = argv[i];
		gFinderLaunch = NO;
	}

	CustomApplicationMain(argc, argv);
	return 0;
}
#endif

// -------------------------------------------------------------------------

// XRoar UI definition

static void *ui_cocoa_new(void *cfg);
static void cocoa_ui_run(void *);

struct ui_module ui_cocoa_module = {
	.common = { .name = "macosx", .description = "Mac OS X+ SDL2 UI",
		.new = ui_cocoa_new,
	},
	.joystick_module_list = sdl_js_modlist,
};

static void cocoa_update_machine_menu(void *);
static void cocoa_update_cartridge_menu(void *);
static void cocoa_update_joystick_menus(void *);
static void cocoa_ui_update_state(void *sptr, int tag, int value, const void *data);

static void *ui_cocoa_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	cocoa_register_app();

	struct ui_sdl2_interface *uisdl2 = ui_sdl_allocate(sizeof(*uisdl2));
	if (!uisdl2) {
		return NULL;
	}
	*uisdl2 = (struct ui_sdl2_interface){0};
	ui_sdl_init(uisdl2, ui_cfg);
	struct ui_interface *ui = &uisdl2->ui_interface;

	ui->update_state = DELEGATE_AS3(void, int, int, cvoidp, cocoa_ui_update_state, uisdl2);
	ui->update_machine_menu = DELEGATE_AS0(void, cocoa_update_machine_menu, uisdl2);
	ui->update_cartridge_menu = DELEGATE_AS0(void, cocoa_update_cartridge_menu, uisdl2);
	ui->update_joystick_menus = DELEGATE_AS0(void, cocoa_update_joystick_menus, uisdl2);
	cocoa_update_machine_menu(uisdl2);
	cocoa_update_cartridge_menu(uisdl2);
	cocoa_update_joystick_menus(uisdl2);

	if (!sdl_vo_init(uisdl2)) {
		free(uisdl2);
		return NULL;
	}

	return uisdl2;
}

static void cocoa_update_machine_menu(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;

	// Get list of machine configs
	struct slist *mcl = slist_reverse(slist_copy(machine_config_list()));

	// Remove old entries
	while ([machine_menu numberOfItems] > 0)
		[machine_menu removeItem:[machine_menu itemAtIndex:0]];

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	NSMenuItem *item;
	struct slist *iter;
	for (iter = mcl; iter; iter = iter->next) {
		struct machine_config *mc = iter->data;
		if (mc == xroar.machine_config)
			current_machine = TAG(ui_tag_machine, mc->id);
		NSString *description = [[NSString alloc] initWithUTF8String:mc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG(ui_tag_machine, mc->id))];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[machine_menu insertItem:item atIndex:0];
		[item release];
	}
	slist_free(mcl);
}

static void cocoa_update_cartridge_menu(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;

	// Get list of cart configs
	struct slist *ccl = NULL;
	struct cart *cart = NULL;
	if (xroar.machine) {
		const struct machine_partdb_extra *mpe = xroar.machine->part.partdb->extra[0];
		const char *cart_arch = mpe->cart_arch;
		ccl = slist_reverse(cart_config_list_is_a(cart_arch));
		cart = (struct cart *)part_component_by_id(&xroar.machine->part, "cart");
	}

	// Remove old entries
	while ([cartridge_menu numberOfItems] > 0)
		[cartridge_menu removeItem:[cartridge_menu itemAtIndex:0]];

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	NSMenuItem *item;
	struct slist *iter;
	for (iter = ccl; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		if (cart && cc == cart->config)
			current_cartridge = TAG_CARTRIDGE | cc->id;
		NSString *description = [[NSString alloc] initWithUTF8String:cc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_CARTRIDGE | cc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[cartridge_menu insertItem:item atIndex:0];
		[item release];
	}
	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_CARTRIDGE | (-1 & TAG_VALUE_MASK))];
	[cartridge_menu insertItem:item atIndex:0];
	[item release];
	slist_free(ccl);
}

static void cocoa_update_joystick_menus(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;

	// Get list of joystick configs
	struct slist *jcl = slist_reverse(slist_copy(joystick_config_list()));

	// Remove old entries
	while ([joy_right_menu numberOfItems] > 0)
		[joy_right_menu removeItem:[joy_right_menu itemAtIndex:0]];
	while ([joy_left_menu numberOfItems] > 0)
		[joy_left_menu removeItem:[joy_left_menu itemAtIndex:0]];

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	NSMenuItem *item;
	struct slist *iter;
	for (iter = jcl; iter; iter = iter->next) {
		struct joystick_config *jc = iter->data;
		NSString *description = [[NSString alloc] initWithUTF8String:jc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_JOY_RIGHT | jc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[joy_right_menu insertItem:item atIndex:0];
		[item release];

		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:(TAG_JOY_LEFT | jc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[joy_left_menu insertItem:item atIndex:0];
		[item release];
	}

	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_JOY_RIGHT | (-1 & TAG_VALUE_MASK))];
	[joy_right_menu insertItem:item atIndex:0];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:(TAG_JOY_LEFT | (-1 & TAG_VALUE_MASK))];
	[joy_left_menu insertItem:item atIndex:0];
	[item release];

	slist_free(jcl);
}

static void cocoa_ui_update_state(void *sptr, int tag, int value, const void *data) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;

	switch (tag) {

	/* Hardware */

	case ui_tag_machine:
		current_machine = TAG_MACHINE | value;
		break;

	case ui_tag_cartridge:
		current_cartridge = TAG_CARTRIDGE | value;
		break;

	/* Cassettes */

	case ui_tag_tape_playing:
		tape_is_playing = value ? 1 : 0;
		break;

	/* Disk */

	case ui_tag_disk_data:
		{
			const struct vdisk *disk = data;
			int we = 1, wb = 0;
			if (disk) {
				we = !disk->write_protect;
				wb = disk->write_back;
			}
			cocoa_ui_update_state(uisdl2, ui_tag_disk_write_enable, value, (void *)(intptr_t)we);
			cocoa_ui_update_state(uisdl2, ui_tag_disk_write_back, value, (void *)(intptr_t)wb);
		}
		break;

	case ui_tag_disk_write_enable:
		disk_write_enable[value] = data ? 1 : 0;
		break;

	case ui_tag_disk_write_back:
		disk_write_back[value] = data ? 1 : 0;
		break;

	// Printer

	case ui_tag_print_destination:
		print_destination = value;
		break;

	/* Video */

	case ui_tag_fullscreen:
		is_fullscreen = value ? 1 : 0;
		break;

	case ui_tag_vdg_inverse:
		vdg_inverted = value ? 1 : 0;
		break;

	case ui_tag_tv_input:
		current_cc = TAG_TV_INPUT | value;
		break;

	case ui_tag_picture:
		current_picture = TAG_TV_PICTURE | value;
		break;

	case ui_tag_ccr:
		current_ccr = TAG_CCR | value;
		break;

	/* Keyboard */

	case ui_tag_keymap:
		current_keymap = TAG_KEYMAP | value;
		break;

	/* Joystick */

	case ui_tag_joy_right:
		{
			struct joystick_config *jc = joystick_config_by_name(data);
			current_joy_right = jc ? jc->id : (-1 & TAG_VALUE_MASK);
			current_joy_right |= TAG_JOY_RIGHT;
		}
		break;

	case ui_tag_joy_left:
		{
			struct joystick_config *jc = joystick_config_by_name(data);
			current_joy_left = jc ? jc->id : (-1 & TAG_VALUE_MASK);
			current_joy_left |= TAG_JOY_LEFT;
		}
		break;

	default:
		break;

	}
}
