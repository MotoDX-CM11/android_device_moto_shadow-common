#!/sbin/sh

 /sbin/e2fsck -L /badblocks.txt -y /dev/block/mmcblk1p3 > /tmp/1.txt
