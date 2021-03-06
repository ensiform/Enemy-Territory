#!/bin/bash

export CC="gcc-8"
export CXX="g++-8"

cmake --version

# Build ETe

#export BUILD_CONFIGURATION="Debug"
export BUILD_CONFIGURATION="Release"

cd src
mkdir build
cd build

cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_CONFIGURATION" -DUSE_SDL2=TRUE -DCMAKE_TOOLCHAIN_FILE="../cmake/toolchains/linux-i686.cmake" || exit 1
cmake --build . --config $BUILD_CONFIGURATION || exit 1

ls -R *.x86

echo "$TRAVIS_TAG"
echo "$BUILD_CONFIGURATION"

7z a "$TRAVIS_BUILD_DIR/wolfet-2.60e-${TRAVIS_COMMIT:0:10}-$BUILD_CONFIGURATION-linux-i386.7z" ete.x86 ete-ded.x86
7z a "$TRAVIS_BUILD_DIR/wolfet-2.60e-${TRAVIS_COMMIT:0:10}-$BUILD_CONFIGURATION-linux-i386.7z" ../../docs/* '-xr!*.cmd'
