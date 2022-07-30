#!/bin/bash
dd if=mbrboot/loader.bin of=disk bs=446 count=1 conv=notrunc 2>/dev/null
dd if=loader/loader.bin of=disk bs=512 seek=1 count=15 conv=notrunc 2>/dev/null
sudo losetup -o 8192 /dev/loop0 disk
#sudo mkfs.vfat /dev/loop0
sudo mount /dev/loop0 /mnt/1
sudo cp vmlinux /mnt/1
sudo umount /mnt/1
sudo losetup -d /dev/loop0
sync
echo done
