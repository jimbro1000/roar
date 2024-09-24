#!/bin/sh
rm -rf /usr/local/xroar/bin
cd /usr/local/xroar/SDL
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build-scripts/cmake-toolchain-mingw64-x86_64.cmake && cmake --build build && cmake --install build
cd ..
/usr/local/xroar/autogen.sh
/usr/local/xroar/configure
make
make install
mkdir bin
cp /usr/local/bin/xroar ./bin/xroar
make distclean
cd build-w64
/usr/local/xroar/configure \
    --prefix=/usr/x86_64-w64-mingw32 \
    --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
make
make install
