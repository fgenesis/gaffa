#!/bin/bash

# check if the build directory exists
if [ -d "build" ]; then
    echo "Build directory exists"
else
    echo "Build directory does not exist"
    mkdir build
fi

# change to the build directory
pushd build

# run cmake
cmake ..

# run make
make -j $(nproc)

popd
