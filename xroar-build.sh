#!/bin/bash

# 2024-04-10
#    Abort if libz.dll.a exists for target arch.  Windows builds of XRoar will
#    end up depending on a DLL if this exists.

cd $HOME/src/xroar || exit 1

# Note: for Windows MSI packaging, this script depends on 'xroar.wxs' existing
# in the same directory as the script.
scriptdir=$(dirname $0)
wxsfile="${scriptdir}/xroar.wxs"

# Ugh, MacOSX 'date' doesn't do all the useful GNUisms, so need to have
# coreutils installed with homebrew:
DATE="date"
type -P gdate >/dev/null 2>&1 && DATE=$(type -P gdate)

t_bold=""
t_sgr0=""
if test -t 1; then
	t_bold=$(tput bold)
	t_sgr0=$(tput sgr0)
fi

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# FUNCTIONS

# Print help text and exit.
do_helptext () {
	cat <<__END__
Usage: $myname [OPTION]... [VERSION] [REVISION] [-- CONFIGURE-OPTS]
Builds XRoar for Windows or Mac OS X.

 Architecture options:
   --w32      build for 32-bit Windows (i686-w64-mingw32) [default]
   --w64      build for 64-bit Windows (x86_64-w64-mingw32)
   --macosx   build for MacOSX [default for Darwin]

 Release type options:
   -s,--snapshot   snapshot build [default]
   -r,--release    release build

 Build options:
   --sdl-prefix PREFIX   override SDL prefix (Windows-only)

 Other options:
   -q, --quiet     less output
   -v, --verbose   more output
   -h, --help      display this help and exit

VERSION is disinguished from REVISION by the presence of decimal points
(a VERSION must have one).
__END__

	exit 0
}

# MacOSX helper copies a library into the appdir, updates the exe to refer to
# its relative position within the appdir, and strips the library itself.
add_dylib () {
	_appdir="$1"
	_exe="$2"
	_lib="$3"
	_libname=`basename $_lib`
	cp "${_lib}" "${_appdir}/Contents/Frameworks/${_libname}"
	chmod 0644 "${_appdir}/Contents/Frameworks/${_libname}"
	echo install_name_tool -change \""${_lib}"\" \""@executable_path/../Frameworks/${_libname}"\" \""${_appdir}/Contents/MacOS/${_exe}"\"
	install_name_tool -change "${_lib}" "@executable_path/../Frameworks/${_libname}" "${_appdir}/Contents/MacOS/${_exe}"
	strip -x "${_appdir}/Contents/Frameworks/${_libname}"
}

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# DEFAULTS
# Extract last released version
version=$(./configure -V | perl -ne 'if (/^xroar configure (\d+\.\d+)/) {print "$1\n";exit 0;}')
subversion=$(./configure -V | perl -ne 'if (/^xroar configure (\d+\.\d+(\.\d+)?)/) {print "$1\n";exit 0;}')

# MSI doesn't pay attention to the minor revision, so we only have 16 bits to
# encode a revision number.  

# Extract date of release version from ChangeLog:
releasedate=$(awk -F', *' "/^Version $version,/{print \$2}" ChangeLog)
# Calculate delta between now and then:
bases=$("$DATE" +%s -d "$releasedate")
nows=$("$DATE" +%s)
# Divide by 3600 (1 hour) allows approximately 7.4 years of revisions from the
# last release:
revision=$(((nows-bases)/3600))

myname=${0##*/}
sdlprefix=""
is_snapshot="yes"
branch=""
quiet=""
verbose=""

target="windows"
platform="x86"
test "$(uname -s)" = "Darwin" && target="macosx"

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# OPTIONS

while test -n "$1"; do
	case "$1" in
		--) shift; break; ;;
		-sdl-prefix|--sdl-prefix) shift; sdlprefix="$1"; ;;
		-macosx|--macosx) target="macosx"; ;;
		-w32|--w32) target="windows"; platform="x86"; ;;
		-w64|--w64) target="windows"; platform="x64"; ;;
		-snapshot|--snapshot|-s) is_snapshot="yes"; ;;
		-release|--release|-r) is_snapshot=""; ;;
		-branch|--branch|-b) shift; branch="-$1"; ;;
		-h|-help|--help) do_helptext; ;;
		-v|-verbose|--verbose) verbose="yes"; quiet=""; ;;
		-q|-quiet|--quiet) quiet="yes"; verbose="" ;;
		-*)
			echo "$myname: unrecognised option '$1'" >&2
			echo "Try '$myname --help' for more information." >&2
			exit 1
			;;
		*) break; ;;
	esac
	shift
done

if test -n "$1"; then
	done=""
	case "$1" in
		--) done=y; shift; ;;
		*.*) version="$1"; shift; ;;
		*) revision="$1"; shift; ;;
	esac

	if test -z "$done" -a -n "$1"; then
		if test "$1" != "--"; then
			revision="$1"
		fi
		shift
	fi
fi

#

if test "$target" = "windows"; then
	if test "$platform" = "x86"; then
		arch="i686-w64-mingw32"
		suffix="w32"
	elif test "$platform" = "x64"; then
		arch="x86_64-w64-mingw32"
		suffix="w64"
	fi

elif test "$target" = "macosx"; then
	suffix="macosx"

fi

test -z "$sdlprefix" && sdlprefix="/usr/${arch}"

if test -e "/usr/${arch}/lib/libz.dll.a"; then
	echo "ERROR: /usr/${arch}/lib/libz.dll.a EXISTS" >&2
	echo "Remove (or move) this file before continuing." >&2
	echo "XRoar will depend on a DLL otherwise." >&2
	exit 1
fi

package="xroar"
ProductVersion="$subversion"
configure_opts="$@"
make_opts=""
zip_opts=""
wixl_opts=""
appname="XRoar.app"
if test -n "$is_snapshot"; then
	while true; do
		case "$ProductVersion" in
			*.*.*) ProductVersion=${ProductVersion%.*}; ;;
			*.*) break; ;;
			*) ProductVersion="${ProductVersion}.0"; break; ;;
		esac
	done
	package="xroar-snap"
	ProductVersion="$ProductVersion.$revision"
	configure_opts="${configure_opts} --enable-experimental --enable-snapshot"
	appname="XRoar snapshot.app"
fi
distdir="${package}${branch}-${ProductVersion}"
pkgdir="${distdir}-macosx"
appdir="${pkgdir}/${appname}"

if test -n "${quiet}"; then
	configure_opts="${configure_opts} -q"
	make_opts="${make_opts} -s V=0"
	zip_opts="${zip_opts} -q"
elif test -n "${verbose}"; then
	wixl_opts="${wixl_opts} -v"
else
	make_opts="-s V=0"
	zip_opts="${zip_opts} -q"
fi

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# BUILD

umask 0022

if test "$target" = "windows"; then
	# Windows build...
	export RC_VER_MAJOR=$(echo "$ProductVersion"|cut -d. -f1)
	export RC_VER_MINOR=$(echo "$ProductVersion"|cut -d. -f2)
	export RC_REV_MAJOR=$(echo "$ProductVersion"|cut -d. -f3)

	test -z "${quiet}" && echo "${t_bold}# Configuring ${distdir}-${suffix}...${t_sgr0}"
	(
	test -n "${verbose}" && cat <<__EOF__
ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ./configure \
	--host=${arch} \
	--prefix=/usr/${arch} \
	--with-sdl-prefix="${sdlprefix}" \
	--enable-filereq-cli \
	--without-gtk3 --without-gtk2 \
	--without-alsa --without-oss --without-pulse \
	--without-sndfile --without-joydev \
	${configure_opts} \
	CFLAGS="-std=c17 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
	CXXFLAGS="-std=c++17 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
	LDFLAGS="-std=c17 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
__EOF__
	test -z "${verbose}" && exec >/dev/null
	ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ./configure \
		--host=${arch} \
		--prefix=/usr/${arch} \
		--with-sdl-prefix="${sdlprefix}" \
		--enable-filereq-cli \
		--without-gtk3 --without-gtk2 \
		--without-alsa --without-oss --without-pulse \
		--without-sndfile --without-joydev \
		${configure_opts} \
		CFLAGS="-std=c17 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
		LDFLAGS="-std=c17 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
	)

elif test "$target" = "macosx"; then
	# MacOSX build...
	test -z "${quiet}" && echo "${t_bold}# Configuring ${distdir}-${suffix}...${t_sgr0}"
	(
	test -z "${verbose}" && exec >/dev/null
	./configure \
		--without-gtk3 --without-gtk2 \
		--without-alsa --without-oss --without-pulse \
		--without-sndfile --without-joydev \
		${configure_opts} \
		CPPFLAGS="-Wall -Wextra" CFLAGS="-Ofast" OBJCFLAGS="-Ofast"
	)

fi

test -z "${quiet}" && echo "${t_bold}# Building ${distdir}-${suffix}...${t_sgr0}"
(
	test -z "${verbose}" && exec >/dev/null
	make $make_opts clean || exit 1
	make $make_opts -j 8 ARFLAGS="cr" || exit 1
	make $make_opts pdf || exit 1
)

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# PACKAGE

if test "$target" = "windows"; then
	# Windows ZIP...
	test -z "${quiet}" && echo "${t_bold}# Packaging ZIP ${distdir}-${suffix}.zip...${t_sgr0}"
	mkdir "${distdir}-${suffix}"
	cp COPYING.GPL ChangeLog README README.SDS src/xroar.exe doc/xroar.pdf doc/xroar.conf.example "${distdir}-${suffix}/" 
	${arch}-strip "${distdir}-${suffix}/xroar.exe"
	zip ${zip_opts} -r "${distdir}-${suffix}.zip" "${distdir}-${suffix}"

	# Windows MSI...
	if test -f "${wxsfile}"; then
		test -z "${quiet}" && echo "${t_bold}# Packaging MSI ${distdir}-${suffix}.msi...${t_sgr0}"
		cd "${distdir}-${suffix}" || exit 1

		# Lots of GUIDs in the MSI need to be different
		# per-architecture, and we also want them to differ for
		# snapshots, so calculate some here:
		uuidns=$(uuid -v5 ns:DNS 6809.org.uk)
		ProductId=$(uuid -v5 "$uuidns" "6809.org.uk/$platform/$package/product")
		UpgradeCode=$(uuid -v5 "$uuidns" "6809.org.uk/$platform/$package/upgradecode")
		ExecutableGuid=$(uuid -v5 "$uuidns" "6809.org.uk/$platform/$package/component/executable")
		ManualGuid=$(uuid -v5 "$uuidns" "6809.org.uk/$platform/$package/component/manual")
		ReadmeGuid=$(uuid -v5 "$uuidns" "6809.org.uk/$platform/$package/component/readme")

		# Apparently, asking wixl to use the icon already baked into
		# the executable will include that executable twice!  Instead,
		# copy the source icon:
		cp ../src/windows32/xroar-256x256.ico .

		# wixl builds the MSI using a wsx file:
		wixl ${wixl_opts} -a "$platform" \
			-D IsSnapshot="$is_snapshot" \
			-D Platform="$platform" \
			-D ProductVersion="$ProductVersion" \
			-D ProductId="$ProductId" \
			-D UpgradeCode="$UpgradeCode" \
			-D ExecutableGuid="$ExecutableGuid" \
			-D ManualGuid="$ManualGuid" \
			-D ReadmeGuid="$ReadmeGuid" \
			-o "../${distdir}-${suffix}.msi" "${wxsfile}" 2>&1 | egrep -v '^$|g_object_set_is_valid_property'

		cd .. || exit 1
	else
		echo "Not building MSI: ${wxsfile} not found"
	fi

elif test "$target" = "macosx"; then
	# MacOSX app...
	test -z "${quiet}" && echo "${t_bold}# Packaging ZIP ${distdir}-${suffix}.zip...${t_sgr0}"
	mkdir "$pkgdir"
	mkdir -p "${appdir}/Contents/MacOS"
	mkdir -p "${appdir}/Contents/Frameworks"
	mkdir -p "${appdir}/Contents/Resources"
	cp src/xroar "${appdir}/Contents/MacOS/"
	add_dylib "${appdir}" xroar /usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib
	#add_dylib "${appdir}" xroar /usr/local/lib/libsndfile.1.dylib
	strip "${appdir}/Contents/MacOS/xroar"
	sed -e "s!@VERSION@!${subversion}!g" src/macosx/Info.plist.in > "${appdir}/Contents/Info.plist"
	cp src/macosx/xroar.icns "${appdir}/Contents/Resources/"
	cp COPYING.GPL ChangeLog README README.SDS "$pkgdir/"
	cp doc/xroar.pdf "$pkgdir/"
	chmod -R g+rX,o+rX "$pkgdir/"
	zip ${zip_opts} -r "${pkgdir}.zip" "${pkgdir}"
	#hdiutil create -srcfolder "$pkgdir" -uid 99 -gid 99 "${pkgdir}.dmg"

fi

# Create a snapshot-specific ChangeLog to push out:
if test -n "$is_snapshot"; then
	cp ChangeLog "ChangeLog-snap${branch}-$ProductVersion"
fi

# Create a distribution tarball to push out:
if test \! -f "${distdir}.tar.gz"; then
	test -z "${quiet}" && echo "${t_bold}# Packaging source tarball ${distdir}.tar.gz...${t_sgr0}"
	(
	test -z "${verbose}" && exec >/dev/null
	make $make_opts dist distdir="${distdir}"
	)
fi
