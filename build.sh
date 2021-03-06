#!/bin/bash
set -e

git submodule update --init

# Build CLI11
echo "# Building third party dep: CLI11"
cd third_party/CLI11
git submodule update --init
mkdir build && cd build
cmake .. && make && sudo make install

# Build ELFIO
echo "# Building third party dep: ELFIO"
cd ../ELFIO
aclocal
autoconf
autoheader
automake --add-missing
./configure && make && sudo make install

# Build uvw
echo "# Building third party dep: uvw"
cd ../uvw/build
cmake .. && make && sudo make install

echo "# Building xendbg"
mkdir build && cd build
cmake ..
make
