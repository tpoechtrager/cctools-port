#!/usr/bin/env bash

export LC_ALL=C
pushd "${0%/*}" &>/dev/null

export CC=clang
export CXX=clang++

set -e

function verbose_cmd
{
    echo "$@"
    eval "$@"
}

function extract()
{
    echo "extracting $(basename $1) ..."
    local tarflags="xf"

    case $1 in
        *.tar.xz)
            xz -dc $1 | tar $tarflags -
            ;;
        *.tar.gz)
            gunzip -dc $1 | tar $tarflags -
            ;;
        *.tar.bz2)
            bzip2 -dc $1 | tar $tarflags -
            ;;
        *)
            echo "unhandled archive type"
            exit 1
            ;;
    esac
}

if [ $# -lt 2 ]; then
    echo "usage: $0 iPhoneOS.sdk.tar* <target cpu>" 1>&2
    echo "i.e. $0 /path/to/iPhoneOS.sdk.tar.xz armv7" 1>&2
    exit 1
fi

TRIPLE="arm-apple-darwin11"
TARGETDIR="$PWD/target"
SDKDIR="$TARGETDIR/SDK"

if [ -d $TARGETDIR ]; then
    echo "cleaning up ..."
    rm -rf $TARGETDIR
fi

mkdir -p $TARGETDIR
mkdir -p $TARGETDIR/bin
mkdir -p $SDKDIR

echo ""
echo "*** extracting SDK ***"
echo ""

pushd $SDKDIR &>/dev/null
SDK_VERSION=$(echo $1 | grep -P -o "[0-9].[0-9]+" | head -1)
if [ -z "$SDK_VERSION" ]; then
    echo "iPhoneOS Version must be in the SDK filename!" 1>&2
    exit 1
fi
extract $1
SYSLIB=$(find $SDKDIR -name "libSystem.dylib" | head -n1)
if [ -z "$SYSLIB" ]; then
    echo "SDK should contain libSystem.dylib" 1>&2
    exit 1
fi
SYSROOT="$(dirname $SYSLIB)/../.."
set +e
mv $SYSROOT/* $SDKDIR 2>/dev/null
set -e
popd &>/dev/null

echo ""
echo "*** building wrapper ***"
echo ""

echo "int main(){return 0;}" | $CC -xc -O2 -o $TARGETDIR/bin/dsymutil -

verbose_cmd $CC -O2 -Wall -Wextra -pedantic wrapper.c \
    -DTARGET_CPU=\"\\\"$2\\\"\" \
    -DOS_VER_MIN=\"\\\"$SDK_VERSION\\\"\" \
    -o $TARGETDIR/bin/$TRIPLE-clang

pushd $TARGETDIR/bin &>/dev/null
verbose_cmd ln -sf $TRIPLE-clang $TRIPLE-clang++
popd &>/dev/null

echo ""
echo "*** building ldid ***"
echo ""

rm -rf tmp
mkdir -p tmp
pushd tmp &>/dev/null
git clone git://git.saurik.com/ldid.git
pushd ldid &>/dev/null
git clone git://git.saurik.com/minimal.git
cp ../../LDID.Makefile Makefile
make INSTALLPREFIX=$TARGETDIR -j4 install
popd &>/dev/null
popd &>/dev/null

echo ""
echo "*** building cctools / ld64 ***"
echo ""

pushd ../../cctools &>/dev/null
git clean -fdx . &>/dev/null || true
./autogen.sh
./configure --target=$TRIPLE --prefix=$TARGETDIR
make -j4 && make install
popd &>/dev/null

echo ""
echo "*** checking toolchain ***"
echo ""

export PATH=$TARGETDIR/bin:$PATH

echo "int main(){return 0;}" | $TRIPLE-clang -xc -O2 -o test - 1>/dev/null || exit 1
rm test
echo "OK"

echo ""
echo "*** all done ***"
echo ""
echo "do not forget to add $TARGETDIR/bin to your PATH variable"
echo ""

