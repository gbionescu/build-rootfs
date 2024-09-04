#!/bin/bash

run() {
    qemu-system-x86_64 \
        -kernel /home/debian/linux/linux-6.1.22/arch/x86/boot/bzImage \
        -append "console=ttyS0 root=/dev/sda rw earlyprintk=serial" \
        -nographic -m 1G \
        -hda "$1"
}

run "$1"