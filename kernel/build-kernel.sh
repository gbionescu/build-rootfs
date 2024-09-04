#!/bin/bash
set -e

wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.22.tar.xz
tar -xf linux-6.1.22.tar.xz

cd linux-6.1.22
cp ../.config .config
cp arch/x86/boot/bzImage ../bzImage-6.1.22

make -j"$(nproc)"