!/bin/sh
echo "clone source"
if [ -d "tre" ]
then
  rm -rf tre
fi
git clone https://github.com/laurikari/tre.git
cd tre
git checkout 6092368aabdd0dbb0fbceb2766a37b98e0ff6911
echo "autogen"
./utils/autogen.sh
echo "configure"
./configure --prefix /usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    --disable-agrep --disable-approx \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
echo "make"
make && make install
make dist

