#!/usr/bin/env bash

export LC_ALL="C"

set -e
# set -x

export CC=clang
export CXX=clang++

SCRIPTDIR=$(cd $(dirname "$0"); pwd)

mkdir -p tmp

pushd tmp
rm -rf *libcxx*

wget -c https://launchpad.net/ubuntu/+archive/primary/+files/libc%2B%2B_1.0~svn181765.orig.tar.gz
tar xzfv libc++*.tar.gz

pushd libcxx*

INCDIR=`pwd`/include/v1
LIB=`pwd`/lib/libc++.a

pushd lib

./buildit
rm libc++.so
ln -sf libc++.so* libc++.so

echo -n "-nostdinc++ -stdlib=libc++ -cxx-isystem `pwd`/../include  -L`pwd` -Qunused-arguments" > ../../libcxx-conf

echo "#!/usr/bin/env bash" > ../../install_libcxx.sh
echo "cp `pwd`/libc++.so \$1/libc++.so.1" >> ../../install_libcxx.sh

popd
popd

pushd libcxxabi*
pushd lib

patch -p0 < $SCRIPTDIR/libcxxabi.patch
./buildit

echo -n " -cxx-isystem `pwd`/../include -L`pwd` -lc++abi" >> ../../libcxx-conf
echo "cp `pwd`/libc++abi.so \$1/libc++abi.so.1" >> ../../install_libcxx.sh
chmod +x ../../install_libcxx.sh
touch ../../have_libcxx

popd

