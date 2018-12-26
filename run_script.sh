#!/bin/sh

echo 0 40000 chen /dev/sdb1 dev dev_public_one PUBLIC PUBLIC_PRIVATE 5000|sudo dmsetup create dev_pub

echo 0 40000 chen /dev/sdb1 dev dev_private_one PRIVATE PUBLIC_PRIVATE 5000|sudo dmsetup create dev_hid

sudo dmesg -c


