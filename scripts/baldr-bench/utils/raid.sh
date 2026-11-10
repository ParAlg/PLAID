#!/usr/bin/bash
source disks.sh
device_name=raid0
if [ "$1" = "create" ]; then
    sudo mdadm --create --verbose "$device_name" --level=0 --raid-devices=${#disks[@]} "${disks[@]}"
    raid_device="/dev/md/${device_name}"
    parted "$raid_device" mklabel gpt mkpart P1 xfs 0% 100% -s
    sudo mkfs.xfs -f "${raid_device}p1"
elif [ "$1" = "stop" ]; then
    parted "/dev/md/${device_name}" rm 1
    sudo mdadm --stop "/dev/md/${device_name}"
    for disk in "${disks[@]}"; do
        sudo mdadm --zero-superblock "$disk"
    done
else
    echo "Invalid argument"
fi