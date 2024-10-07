!/bin/sh
if [ -d "SDL" ]
then
  rm -rf SDL
fi
echo "clone source"
git clone https://github.com/libsdl-org/SDL.git
cd SDL
git checkout 79ec168f3c1e2fe27335cb8886439f7ef676fb49
./configure --prefix=/usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
echo "make"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build-scripts/cmake-toolchain-mingw64-x86_64.cmake && cmake --build build && cmake --install build

