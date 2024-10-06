!/bin/sh
echo "clone source"
git clone https://github.com/libsdl-org/SDL.git
cd SDL
echo "make"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build-scripts/cmake-toolchain-mingw64-x86_64.cmake && cmake --build build && cmake --install build

