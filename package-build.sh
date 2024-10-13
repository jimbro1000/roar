#!/bin/bash
t_bold=""
t_sgr0=""

if test -t 1; then
	t_bold=$(tput bold)
	t_sgr0=$(tput sgr0)
fi

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# FUNCTIONS

do_helptext () {
        cat <<__END__
Usage: $myname [OPTION]... [VERSION] [REVISION]
Builds package for Windows.

 Architecture options:
   --w32      build for 32-bit Windows (i686-w64-mingw32)
   --w64      build for 64-bit Windows (x86_64-w64-mingw32)

 Other options:
   -q, --quiet     less output
   -v, --verbose   more output
   -h, --help      display this help and exit

Default is to build for both w32 and w64.
__END__

	exit 0
}

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# DEFAULTS

myname=${0##*/}
packagedir="$HOME/package"
quiet=""
verbose=""
build_archs=""

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# OPTIONS

while test -n "$1"; do
	case "$1" in
		--) shift; break; ;;
		-w32|--w32) build_archs="${build_archs} w32"; ;;
		-w64|--w64) build_archs="${build_archs} w64"; ;;
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

test -z "${build_archs}" && build_archs="w32 w64"
package="$1"

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# BUILD PATHS

if test -z "$package"; then
	mydir=$(pwd)
	case "$mydir" in
		*/package/*/build-w32)
			build_archs="w32"
			packagedir="${mydir%/package/*}/package"
			_tmp=${mydir#*/package/}
			package="${_tmp%/build-w32}"
			;;
		*/package/*/build-w64)
			build_archs="w64"
			packagedir="${mydir%/package/*}/package"
			_tmp=${mydir#*/package/}
			package="${_tmp%/build-w64}"
			;;
		*/package/*/*)
			echo "don't know what to do in $mydir" >&2
			exit 1
			;;
		*/package/*)
			packagedir="${mydir%/package/*}/package"
			package=${mydir#*/package/}
			;;
		*/src/*/build-w32)
			build_archs="w32"
			packagedir="${mydir%/src/*}/src"
			_tmp=${mydir#*/src/}
			package="${_tmp%/build-w32}"
			;;
		*/src/*/build-w64)
			build_archs="w64"
			packagedir="${mydir%/src/*}/src"
			_tmp=${mydir#*/src/}
			package="${_tmp%/build-w64}"
			;;
		*/src/*/*)
			echo "don't know what to do in $mydir" >&2
			exit 1
			;;
		*/src/*)
			packagedir="${mydir%/src/*}/src"
			package=${mydir#*/src/}
			;;
		*)
			;;
	esac
fi

if test -z "${package}"; then
	echo "must specify a package to build" >&2
	exit 1
fi

srcdir="${packagedir}/${package}"

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# BUILD PACKAGE

build_package () {
	local arch="$1"
	case "$arch" in
		w32)
			triple="i686-w64-mingw32"
			;;
		w64)
			triple="x86_64-w64-mingw32"
			;;
		*)
			echo "don't know how to build for arch $arch" >&1
			exit 1
	esac
	local builddir="${srcdir}/build-${arch}"
	local configure_prefix_opts="--prefix=/usr/${triple} --host=${triple}"
	local configure_lib_opts="--enable-static --disable-shared"
	local configure_cflags="-Ofast -g"
	local configure_cppflags="-D__USE_MINGW_ANSI_STDIO=1"
	local configure_extra_opts=""

	local link_parent=""

	case "$package" in
		SDL_image)
			configure_extra_opts="--with-sdl-prefix=/usr/${triple} --disable-webp"
			;;
		xroar)
			configure_lib_opts=""
			configure_extra_opts="--with-sdl-prefix=/usr/${triple} --enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa --without-oss --without-pulse --without-joydev"
			;;
		tre)
			configure_extra_opts="--disable-agrep --disable-approx"
			;;
		libsndfile)
			configure_extra_opts="--disable-external-libs"
			;;
		libsamplerate)
			configure_extra_opts="--disable-sndfile --disable-fftw"
			;;
		mman-win32)
			configure_prefix_opts="--prefix=/usr/${triple} --cross-prefix=${triple}-"
			configure_cflags=""
			configure_cppflags=""
			link_parent=1
			;;
		dzip)
			configure_extra_opts="LDFLAGS=-lmman"
			;;
		*)
			;;
	esac

	local use_flags=""
	if test -n "${configure_cflags}${configure_cppflags}"; then
		use_flags=1
	fi

	cd "${srcdir}" || exit 1
	local configure_ac=""
	if test -f "configure.ac"; then
		configure_ac="configure.ac"
	elif test -f "configure.in"; then
		configure_ac="configure.in"
	fi

	# if there is no configure script, or configure is older than its
	# autoconf source file, try to regenerate it

	if test \! -x "configure" -o "configure" -ot "${configure_ac}"; then
		if test -x "autogen.sh"; then
			./autogen.sh || exit 1
		else
			echo "can't regenerate configure" >&2
			exit 1
		fi
	fi

	if test \! -x "configure"; then
		echo "configure script not found, nor generated" >&2
		exit 1
	fi

	mkdir -p "${builddir}" || exit 1
	cd "${builddir}" || exit 1

	if test -n "$link_parent"; then
		ln -s ../* .
	fi

	test -z "${quiet}" && echo "${t_bold}# Configuring ${package} (${arch})...${t_sgr0}"
	(
	test -z "${verbose}" && exec >/dev/null
	test -f Makefile && make clean
	if test -n "${use_flags}"; then
		../configure ${configure_prefix_opts} ${configure_lib_opts} CFLAGS="${configure_cflags}" CPPFLAGS="${configure_cppflags}" ${configure_extra_opts}
	else
		../configure ${configure_prefix_opts} ${configure_lib_opts} ${configure_extra_opts}
	fi
	)

	test -z "${quiet}" && echo "${t_bold}# Building ${package} (${arch})...${t_sgr0}"
	(
	test -z "${verbose}" && exec >/dev/null
	make -j 4 V=0 
	)
}

for arch in $build_archs; do
	build_package "$arch"
done

