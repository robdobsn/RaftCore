rm -rf build_linux/
mkdir build_linux
cd build_linux
cmake -DCMAKE_INSTALL_PREFIX=install ..
make -j$(nproc) VERBOSE=1
make install
cd ..
