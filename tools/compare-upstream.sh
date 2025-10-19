#!/bin/sh
# SPDX-License-Identifier: 0BSD
# Copyright 2025 J. Neuschäfer
set -e

usage() {
	echo "Usage: $0 PACKAGE VERSION"
	echo
	echo "  valid packages:  cctools ld64"
	echo
	echo "Shows the comulative changes between an upstream (Apple) version"
	echo "and the cctools-port version of the same tool."
	exit 1
}

if [ $# != 2 ]; then usage; fi

case "$1" in
cctools|ld64)
	TARBALL="$1-$2.tar.gz"
	TARBALL_URL="https://www.opensource.apple.com/tarballs/$1/$TARBALL"
	SRCDIR="$1-$1-$2"
	;;
*)
	usage
	;;
esac

cd "$(dirname "$0")/.."

if [ ! -e "$TARBALL" ]; then
	wget "$TARBALL_URL"
fi

if [ ! -e "$SRCDIR" ]; then
	echo "extracting $SRCDIR"
	tar xf "$TARBALL" "$SRCDIR"
fi

case "$1" in
cctools)
	diff -ru "$SRCDIR" cctools
	;;
ld64)
	diff -ru "$SRCDIR" cctools/ld64
	;;
*)
	echo "unhandled case"
	exit 1
	;;
esac
