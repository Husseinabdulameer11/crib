#!/bin/sh
# Runs once when the dev container is created (postCreateCommand).
# Gets gram, installs it into ~/.local, then builds and installs Crib.
set -eu

GRAM_REPO=${GRAM_REPO:-https://github.com/Husseinabdulameer11/gram.git}
GRAM_REF=${GRAM_REF:-main}
PREFIX=$HOME/.local
export PATH="$PREFIX/bin:$PATH"

if ! command -v cc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "== installing a C compiler and make"
    sudo apt-get update -q
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends build-essential
fi

# gram next to crib if we may write there (Codespaces: /workspaces/gram)
crib_dir=$(pwd)
parent=$(dirname "$crib_dir")
if [ -w "$parent" ]; then gram_dir=$parent/gram; else gram_dir=$HOME/gram; fi

if [ -f "$gram_dir/gram.c" ]; then
    echo "== using gram in $gram_dir"
else
    echo "== cloning gram ($GRAM_REPO) into $gram_dir"
    git clone --depth 1 --branch "$GRAM_REF" "$GRAM_REPO" "$gram_dir"
fi

echo "== installing gram into $PREFIX"
make -C "$gram_dir" install PREFIX="$PREFIX"

echo "== building and installing crib"
make clean
make test GRAM_PREFIX="$PREFIX"
make install PREFIX="$PREFIX"

echo
echo "Ready. Try:  crib examples/exam.crib"
echo "             crib --html exam.html examples/exam.crib"
echo "gram is in $gram_dir. After changing src/crib.gram:  make install"
echo "After changing gram itself:  make -C $gram_dir install PREFIX=$PREFIX && make clean install"
