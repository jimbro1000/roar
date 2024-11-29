!/bin/sh
if [ ! -d "SDL" ]; then
  echo "clone source"
  wget "https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.30.8.tar.gz"
  tar -xvf release-2.30.8.tar.gz
  mv SDL-release-2.30.8 SDL
  rm release-2.30.8.tar.gz
fi
cd SDL
./configure --prefix=/usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
echo "make"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build-scripts/cmake-toolchain-mingw64-x86_64.cmake
cmake --build build
cmake --install build

