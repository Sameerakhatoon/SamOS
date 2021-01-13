#!/bin/bash
# build.sh - top-level build for SamOs.
#
# Walks current build steps in order. Each chapter that adds a build step
# extends this script.

set -e

cd "$(dirname "$0")"
mkdir -p bin build

# Stub: bootloader sources will be assembled here as the chapters progress.
# The next chapter (Hello World) replaces this stub with a real `nasm` call.
echo "build: nothing to do yet"
