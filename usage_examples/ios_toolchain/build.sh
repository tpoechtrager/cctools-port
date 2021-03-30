#!/usr/bin/env bash

export LC_ALL=C
pushd "${0%/*}" &>/dev/null

PLATFORM=$(uname -s)
OPERATING_SYSTEM=$(uname -o || echo "-")

if [ $OPERATING_SYSTEM == "Android" ]; then
  export CC="clang -D__ANDROID_API__=26"
  export CXX="clang++ -D__ANDROID_API__=26"
fi

if [ -z "$LLVM_DSYMUTIL" ]; then
    LLVM_DSYMUTIL=llvm-dsymutil
fi

if [ -z "$JOBS" ]; then
    JOBS=$(nproc 2>/dev/null || ncpus 2>/dev/null || echo 1)
fi

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
            echo "unhandled archive type" 1>&2
            exit 1
            ;;
    esac
}

function git_clone_repository
{
    local url=$1
    local branch=$2
    local directory

    directory=$(basename $url)
    directory=${directory/\.git/}

    if [ -n "$CCTOOLS_IOS_DEV" ]; then
        rm -rf $directory
        cp -r $CCTOOLS_IOS_DEV/$directory .
        return
    fi

    if [ ! -d $directory ]; then
        local args=""
        test "$branch" = "master" && args="--depth 1"
        git clone $url $args
    fi

    pushd $directory &>/dev/null

    git reset --hard
    git clean -fdx
    git checkout $branch
    git pull origin $branch

    popd &>/dev/null
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
SDK_VERSION=$(echo $1 | grep -P -o "[0-9][0-9].[0-9]+" | head -1)
if [ -z "$SDK_VERSION" ]; then
  SDK_VERSION=$(echo $1 | grep -P -o "[0-9].[0-9]+" | head -1)
fi
if [ -z "$SDK_VERSION" ]; then
    echo "iPhoneOS Version must be in the SDK filename!" 1>&2
    exit 1
fi
extract $1
SYSLIB=$(find $SDKDIR -name libSystem.dylib -o -name libSystem.tbd | head -n1)
if [ -z "$SYSLIB" ]; then
    echo "SDK should contain libSystem{.dylib,.tbd}" 1>&2
    exit 1
fi
WRAPPER_SDKDIR=$(echo iPhoneOS*sdk | head -n1)
if [ -z "$WRAPPER_SDKDIR" ]; then
    echo "broken SDK" 1>&2
    exit 1
fi
popd &>/dev/null

echo ""
echo "*** building wrapper ***"
echo ""

OK=0

set +e
which $LLVM_DSYMUTIL &>/dev/null
if [ $? -eq 0 ]; then
    case $($LLVM_DSYMUTIL --version | \
           grep "LLVM version" | head -1 | awk '{print $3}') in
        3.8*|3.9*|4.0*|5.0*|6.0*|7.0*|8.0*|9.0*|10*|11*|12*|13*) OK=1 ;;
    esac
fi
set -e

if [ $OK -eq 1 ]; then
    ln -sf $(which $LLVM_DSYMUTIL) $TARGETDIR/bin/dsymutil
    pushd $TARGETDIR/bin &>/dev/null
    ln -sf $TRIPLE-lipo lipo
    popd &>/dev/null
elif ! which dsymutil &>/dev/null; then
    echo "int main(){return 0;}" | cc -xc -O2 -o $TARGETDIR/bin/dsymutil -
fi

verbose_cmd cc -O2 -Wall -Wextra -pedantic wrapper.c \
    -DSDK_DIR=\"\\\"$WRAPPER_SDKDIR\\\"\" \
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
git_clone_repository https://github.com/tpoechtrager/ldid.git master
pushd ldid &>/dev/null
make INSTALLPREFIX=$TARGETDIR -j$JOBS install
popd &>/dev/null
popd &>/dev/null

echo ""
echo "*** building apple-libtapi ***"
echo ""

pushd tmp &>/dev/null
git_clone_repository https://github.com/tpoechtrager/apple-libtapi.git 1100.0.11
pushd apple-libtapi &>/dev/null
INSTALLPREFIX=$TARGETDIR ./build.sh
./install.sh
popd &>/dev/null
popd &>/dev/null

echo ""
echo "*** building cctools / ld64 ***"
echo ""

pushd ../../cctools &>/dev/null
git clean -fdx &>/dev/null || true
popd &>/dev/null

pushd tmp &>/dev/null
mkdir -p cctools
pushd cctools &>/dev/null
../../../../cctools/configure --target=$TRIPLE --prefix=$TARGETDIR --with-libtapi=$TARGETDIR
make -j$JOBS && make install
popd &>/dev/null
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

