#!/bin/sh
cd /usr/local/xroar
pwd | echo
ls -la | echo
echo "preparing build-w64"
if [ -d "/usr/local/xroar/build-w64" ]
then
  rm -rf /usr/local/xroar/build-w64
  mkdir /usr/local/xroar/build-w64
  mkdir /usr/local/xroar/build-w64/bin
fi
echo "preparing bin"
if [ -d "/usr/local/xroar/bin" ]
then
  rm -rf /usr/local/xroar/bin
  mkdir /usr/local/xroar/bin
fi
echo "autogen"
/usr/local/xroar/autogen.sh
echo "configure"
#/usr/local/xroar/configure
#echo "make native"
#make
#echo "make install native"
#make install
#cp /usr/local/bin/xroar ./bin/xroar
#cd /usr/local/xroar/src
#make distclean
cd /usr/local/xroar/build-w64
ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ../configure \
--host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
--with-sdl-prefix="/usr/x86_64-w64-mingw32" \
--enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa \
--without-oss --without-pulse --without-sndfile --without-joydev \
CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
echo "make win exe"
make
echo "install"
make install
