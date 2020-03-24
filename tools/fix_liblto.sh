#!/usr/bin/env bash
## http://llvm.org/PR22677

pushd "${0%/*}" &>/dev/null

if [ $(uname -s) == "Darwin" ]; then
  echo "Darwin works out of the box."
  exit 1
fi

if [ -z "$LLVM_CONFIG" ]; then
  LLVM_CONFIG=llvm-config
fi

which $LLVM_CONFIG &>/dev/null

if [ $? -ne 0 ]; then
  echo -n "Cannot find $LLVM_CONFIG, you may want to try " 1>&2
  echo "LLVM_CONFIG=llvm-config<suffix> $0" 1>&2
  exit 1
fi

LIBDIR=$($LLVM_CONFIG --libdir)
INCDIR=$($LLVM_CONFIG --includedir)

V="$(readelf -Ws $LIBDIR/libLTO.so 2>/dev/null | grep LLVMCreateDisasm)"

if [ -n "$V" ]; then
  echo "No fix needed."
  exit 1
fi

if readelf -p .comment $LIBDIR/libLTO.so | grep clang &>/dev/null; then
  CC=clang
  CXX=clang++
else
  CC=gcc
  CXX=g++
fi


VERSION=$($LLVM_CONFIG --version | awk -F \. {'print $1$2'} | sed 's/svn//g')

if [ $VERSION -le 39 ]; then
  BRANCH=$($LLVM_CONFIG --version| tr '.' ' ' | awk '{print $1, $2}' | tr ' ' '.')
else
  BRANCH=$($LLVM_CONFIG --version| tr '.' ' ' | awk '{print $1}' | tr ' ' '.')
fi

BRANCH+=".x"

if [ $VERSION -lt 34 ]; then
  echo "This tool requires LLVM 3.4 or later." 1>&2
  exit 1
fi

LIBS=$($LLVM_CONFIG --libs all)
SYSLIBS=$($LLVM_CONFIG --system-libs 2>/dev/null || echo "-ldl -lz -ltinfo -pthread")
CXXFLAGS=$($LLVM_CONFIG --cxxflags)

if [ $VERSION -ge 35 ]; then
  SYSLIBS+=" -ledit"
fi

echo "int main(){Z3_mk_config();} " | \
  $CC -Wno-implicit-function-declaration -o /dev/null -xc - -lz3

if [ $? -eq 0 ]; then
  SYSLIBS+=" -lz3"
fi


set -e

TMP=$(mktemp -d)

pushd $TMP &>/dev/null

function download_sources() {
  wget https://raw.githubusercontent.com/llvm/llvm-project/$1/llvm/tools/lto/lto.cpp
  wget https://raw.githubusercontent.com/llvm/llvm-project/$1/llvm/tools/lto/LTODisassembler.cpp
  wget https://raw.githubusercontent.com/llvm/llvm-project/$1/llvm/tools/lto/lto.exports
}

download_sources "release/$BRANCH" || download_sources "master"

echo "{" > lto.ls
echo "  global:" >> lto.ls
while read p; do
  echo "   $p;" >> lto.ls
done < lto.exports
echo "   LLVM*;" >> lto.ls
echo "  local: *;" >> lto.ls
echo "};" >> lto.ls
popd &>/dev/null

set -x

$CXX -shared \
 -L$LIBDIR -I$INCDIR -Wl,--whole-archive $LIBS -Wl,--no-whole-archive $SYSLIBS \
 $CXXFLAGS $TMP/lto.cpp $TMP/LTODisassembler.cpp -Wl,-version-script,$TMP/lto.ls \
 -Wl,-no-undefined -fno-rtti -fPIC -o libLTO.so

rm -r $TMP

mv -f libLTO.so $LIBDIR || {
  set +x
  echo "Try again as root."
  echo "Or run the following command by hand: mv $PWD/libLTO.so $LIBDIR"
  exit 1
}
