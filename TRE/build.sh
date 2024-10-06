!/bin/sh
echo "clone source"
git clone https://github.com/laurikari/tre.git
cd tre
git checkout 6092368aabdd0dbb0fbceb2766a37b98e0ff6911
echo "autogen"
./utils/autogen.sh
echo "configure"
./configure
echo "make"
make && make install

