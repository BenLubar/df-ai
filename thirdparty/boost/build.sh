#!/bin/bash -e

pv=pv
command -v pv &>/dev/null || pv=cat

test -n "$TOOLSET" || { echo "must set \$TOOLSET first - try 'gcc-4.8' or 'msvc'" >&2; exit 2; }

test -f boost_1_67_0.tar.bz2 || wget https://dl.bintray.com/boostorg/release/1.67.0/source/boost_1_67_0.tar.bz2
echo "2684c972994ee57fc5632e03bf044746f6eb45d4920c343937a465fd67a5adba  boost_1_67_0.tar.bz2" | sha256sum -c

rm -rf boost_1_67_0
$pv boost_1_67_0.tar.bz2 | tar xj

dir="$(pwd)"

pushd boost_1_67_0
./bootstrap.sh --prefix="$dir" --with-toolset="${TOOLSET//-*}" --with-libraries="context"
./b2 --toolset="$TOOLSET" --layout=versioned cxxflags=-fPIC variant=release link=static threading=multi address-model=32,64 install
popd

rm -rf boost_1_67_0 include/boost-1_67
