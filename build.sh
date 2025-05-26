#!/bin/sh

rm -rf build
mkdir build
cd build
cmake ..
make -j 12
cd ..


cd src/platforms/esp32
idf.py fullclean
idf.py build
./build/mkimage.sh

cp build/atomvm-esp32.img ~/Documents/Code/esp32/atom-cluster/atomvm-esp32.img