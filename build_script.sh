#!/bin/bash

# Remove the build directory
rm -r build

# Recreate the build directory
mkdir build

# Change to the build directory
cd build

# Run cmake
cmake ..

# Run make
make
