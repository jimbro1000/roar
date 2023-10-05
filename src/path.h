/** \file
 *
 *  \brief File path searching.
 *
 *  \copyright Copyright 2009-2020 Ciaran Anscomb
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

#ifndef XROAR_PATH_H_
#define XROAR_PATH_H_

#include "sds.h"

// Interpolate variables at the beginning of a path element or filename.
//
// A leading "~/" is replaced with ${HOME}/ (${USERPROFILE}\ under Windows).
//
// Also under Windows, a leading "%varname%" is replaced with ${varname} EXCEPT
// for the following, which are looked up as "known folders":
//
//     LOCALAPPDATA -> FOLDERID_LocalAppData
//     PROFILE      -> FOLDERID_Profile
//     USERPROFILE  -> FOLDERID_Profile

sds path_interp_full(const char *filename, uint32_t flags);

// Same but assumes flags == 0.

sds path_interp(const char *filename);

// Try to find regular file within a (colon-separated) list of directories.
// Returns allocated memory containing full path to file if found, NULL
// otherwise.  Directory separator occuring within filename just causes that
// one file to be checked.

sds find_in_path(const char *path, const char *filename);

#endif
