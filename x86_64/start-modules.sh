set -x
mount -t debugfs none /sys/kernel/debug
#modprobe rt_eepro100
#modprobe rt_8139too
#modprobe rtipv4 && modprobe rtudp
#modprobe rtpacket
#modprobe rtmac
#modprobe nomac
#modprobe rtcap
modprobe rtnetproxy
#/usr/xenomai/sbin/rtifconfig rteth0 up 192.168.1.30
./rtifconfig rteth0 up 192.168.1.30
./rtifconfig rtlo up 127.0.0.1
ifconfig rtproxy up 192.168.1.30
/etc/init.d/S50dropbear restart
#/usr/xenomai/sbin/nomaccfg rteth0 attach
#ifconfig vnic0 up 192.168.1.30
#/root/S50dropbear restart
#/usr/xenomai/sbin/rtnet start
#/usr/xenomai/sbin/rtroute solicit 192.168.1.40 dev rteth0
./rtroute solicit 192.168.1.40 dev rteth0
