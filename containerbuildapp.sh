#!/bin/sh
cd /usr/local/xroar
/usr/local/xroar/autogen.sh
/usr/local/xroar/configure
make
make install
mkdir bin
cp /usr/local/bin/xroar ./bin/xroar
make distclean
cd build-w64
ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ../configure \
--host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
--with-sdl-prefix="/usr/x86_64-w64-mingw32" \
--enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa \
--without-oss --without-pulse --without-sndfile --without-joydev \
CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
make
make install
