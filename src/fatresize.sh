#!/bin/sh
function gp() {
dev=$1
[ ! -e "$dev" ] && return 1
# part=`ls --color=never ${dev}p* | tail -n1 | tail -c 2`
# [ "0$part" -lt 1 ] && return 1
# part=${dev}p${part}
# dls=`fdisk -l $dev | grep mmcblk | head -n 1 | tr -s " " | cut -d " " -f7`
# pfs=`fdisk -l $dev | grep mmcblk | tail -n 1 | tr -s " " | cut -d " " -f2`
# pls=`fdisk -l $dev | grep mmcblk | tail -n 1 | tr -s " " | cut -d " " -f3`
# ptype=`fdisk -l $dev | grep mmcblk | tail -n 1 | tr -s " " | cut -d " " -f6`
# max=$(( ( $dls - $pfs - 1 ) * 512))
# [ "p$ptype" != "pb" ] && [ "p$ptype" != "pc" ] && return 1
# [ $(($dls - 2560)) -le $pls ] && return 1
# umount -fl /home/retrofw ${part}
# (
# echo c
# echo w
# echo q
# echo Y
# ) | fixparts $dev
# fatresize -s $max ${part}


echo "
start= 342016, size= 460800, type=82
start= 802816,               type=c
" | sfdisk ${dev}

sudo mkswap ${dev}p2
sudo mkfs.vfat -F32 -va -n RETROFW ${dev}p3
}
gp "$(readlink -f /dev/root | head -c -3)"
partprobe
