## RTnet (UDP) on PREEMPT_RT Linux


Please read the whole doc to have an impression.

#### Features:
* ported to raspberry pi 4 (bcmgenet), orange pi one (stmmac), 
realtek (8139too), microchip (enc28j60)
* note: the bbb driver from this repository is not ported to rtnet-preempt_rt,
I do not remember exactly why I have included it at that time (2 years ago)
* rtnet UDP socket, bind, recvmsg, sendto, recvfrom, sendmsg, select, poll system calls
(the system calls names are appended with _rtnet(), but you can rename them);
* timeout possible for recv;
* sockets with AF_INET (UDP) or AF_PACKET (raw) family;
* rtnetproxy for ssh and scp (but uses RT driver bandwidth)
* also see the help docs in the rtnet-geek repository.
* TODO: port routing, rtcfg, rtmac, tdma and nomac if there will be interest
* improvements can be made around the variable msg_in_userspace,
like avoiding multiple packet copies

#### Paper
* if you use our work, please cite us:
L. -C. Duca and A. Duca, "Achieving Hard Real-Time Networking on PREEMPT_RT Linux with RTnet"
2020 International Symposium on Fundamentals of Electrical Engineering (ISFEE), 
2020, pp. 1-4, doi: 10.1109/ISFEE51261.2020.9756165.
* https://ieeexplore.ieee.org/document/9756165

#### 0. Download linux-5.9.tar.xz
https://cdn.kernel.org/pub/linux/kernel

#### 1a. Apply patch (includes PREEMPT_RT)
```
cd linux-5.9
patch -p1 < ../rtnet-v11b-preempt_rt-linux-5.9.patch
```

#### 1b. Add the _rtnet() system calls
- rpi-4
```
cp ../rpi-4/unistd.h include/uapi/asm-generic/
```

- x86_64 qemu
```
cp ../x86_64/syscall_64.tbl arch/x86/entry/syscalls/
```

- orangepi-one
```
cp ../orangepi-one/syscall.tbl arch/arm/tools/
```

- beaglebone black (patch with bbb rtnet driver including PREEMPT_RT)
```
cp ../bbb/am335x-bone-common.dtsi arch/arm/boot/dts/
cp ../bbb/syscall.tbl arch/arm/tools/
```

- microchip enc28j60 on raspberry pi zero
(needs RT SPI drivers, see https://github.com/laurentiuduca/real-time-spi)
```
cp ../rpi-zero-enc28j60/bcm2835-rpi.dtsi arch/arm/boot/dts/
cp ../bbb/syscall.tbl arch/arm/tools/
```

#### 2. Use buildroot to setup a rootfs for the target board

#### 3. Configure linux

###### Note for rpi 4 (choose the 64 bits version)
- SSH is disabled by default; can be enabled by creating a file with name "ssh" in boot partition
- cmdline.txt: root=/dev/mmcblk1p2 rootwait console=tty1 console=ttyS1,115200

https://gist.github.com/lategoodbye/c7317a42bf7f9c07f5a91baed8c68f75

https://www.raspberrypi.org/forums/viewtopic.php?t=249579

###### defconfig
rpi-4
```
make ARCH=arm64 defconfig
```
orangepi-one
```
make ARCH=arm sunxi_defconfig
```
bbb
```
make ARCH=arm omap2plus_defconfig
```
rpi-0<br>
- cmdline.txt: root=/dev/mmcblk0p2 rootwait console=tty1 console=ttyS1,115200
```
make ARCH=arm bcm2835_defconfig
```
qemu x86_64
```
make ARCH=x86_64 x86_64_defconfig
```

###### In the kernel configuration, set the following settings as enabled [*] or disabled []

- CONFIG_PREEMPT_RT_FULL: General setup → Preemption Model (Fully Preemptible Kernel (RT)) → Fully Preemptible Kernel (RT)
(Depends on: <choice> && EXPERT [=y] && ARCH_SUPPORTS_RT [=y] && KVM=[n])
- Enable HIGH_RES_TIMERS: General setup → Timers subsystem → High Resolution Timer Support (Actually, this should already be enabled in the standard configuration.)
CONFIG_HZ_PERIODIC=n, CONFIG_NO_HZ=n, CONFIG_NO_HZ_FULL=y
- Kernel features - Processor type and features → [] Multi-core scheduler support (CONFIG_SCHED_MC)
- [] ACPI Support
- Power management and ACPI options / CPU Power Management ---><br>
CPU Frequency scaling ---> [ ] CPU Frequency scaling<br>
CPU idle ---> [ ] CPU idle PM support

- rtnet is in the net folder and must compile into kernel its driver
(there must be selected in-kernel ipv4, icmp and udp from the protocol stack
and proxy as a module):
```
Select Networking Support - RTnet, 
  Protocol Stack -> (32) Size of central RX-FIFO,
    Real-Time IPv4, ICMP support, (32)  Maximum host routing table entries, 
    UDP support, Real-Time Packet Socket Support.
    The rest must be unselected.
  Drivers -> the driver for the target computer
  Add-Ons -> IP protocol proxy for Linux
```

- be sure to disable the non-RTnet network drivers from net/ethernet
Device Drivers -> Network device support -> Ethernet driver support -> Broadcom, STMicroelectronics devices, TI, etc

#### 4. Compile linux

- x86_64 qemu
```
make -j5 ARCH=x86_64 INSTALL_MOD_PATH=/home/user/modules CONFIG_DEBUG_INFO=y bzImage modules modules_install
```

- rpi-4
```
make -j5 ARCH=arm64 CROSS_COMPILE="..." CONFIG_DEBUG_INFO=y INSTALL_MOD_PATH=/home/laur/lucru/rtnet/modules Image bcm2711-rpi-4-b.dtb modules modules_install
```

- orangepi-one
```
make -j5 ARCH=arm CROSS_COMPILE="..." CONFIG_DEBUG_INFO=y INSTALL_MOD_PATH=/home/laur/lucru/rtnet/modules zImage sun8i-h3-orangepi-one.dtb modules modules_install
```

- bbb
```
make -j5 ARCH=arm CROSS_COMPILE="..." CONFIG_DEBUG_INFO=y INSTALL_MOD_PATH=/home/laur/lucru/rtnet/modules zImage am335x-boneblack.dtb modules modules_install
```

- rpi-0
```
make -j5 ARCH=arm CROSS_COMPILE="..." CONFIG_DEBUG_INFO=y INSTALL_MOD_PATH=/home/laur/lucru/rtnet/modules zImage bcm2835-rpi-zero-w.dtb modules modules_install
```

#### 5. Boot qemu x86_64 emulator or boot target board
- qemu x86_64 emulator (see x86_64/qemu/config-qemu.txt)
```
sudo qemu-system-x86_64 -m 1G --enable-kvm -M q35 -kernel bzImage -hda rootfs-50 -append "console=tty1 console=ttyS0 root=/dev/sda rw" -device rtl8139,netdev=bridgeid,mac=52:54:00:11:22:44 -netdev bridge,br=br0,id=bridgeid -serial stdio
sudo qemu-system-x86_64 -m 1G              -M q35 -kernel bzImage -hda rootfs-50 -append "console=tty1 console=ttyS0 root=/dev/sda rw" -device rtl8139,netdev=bridgeid,mac=52:54:00:11:22:44 -netdev bridge,br=br0,id=bridgeid -serial stdio
with empty password (ENTER).
```

#### 6. After booting qemu or target, read start-modules.sh (a better name would be setup-rtnet.sh)
- on the target:
```
./start-modules.sh:
```
which equivalates to
```
set -x
mount -t debugfs debugfs /sys/kernel/debug
/root/rtifconfig rteth0 up 192.168.1.70
## now you should wait for the interface to be set up
/root/rtifconfig rtlo up 127.0.0.1
ifconfig rtproxy up 192.168.1.70
/root/rtroute solicit 192.168.1.30 dev rteth0
/root/rtroute solicit 192.168.1.40 dev rteth0
/root/rtroute solicit 192.168.1.50 dev rteth0
/root/rtroute solicit 192.168.1.60 dev rteth0
```

- on the development host:
```
ping 192.168.1.70
```

#### 7. There are provided tftp client (both x86_64 and arm) and server (for x86_64).

If you do not use rtnetproxy for scp or ssh, you can use tftp.<br>
On the development host:
```
cp zImage /tmp/
./tftpd -d -P 8086
```
On the target, to get zImage:
```
./tftpc 192.168.1.100 -P 8086 -g zImage -o
```
On the target, to copy to server:
```
./tftpc 192.168.1.100 -P 8086 -p filename -o
```
You will find filename in /tmp

#### 8. Testing
Please read rtt-laur.c and rtt-sender.c/rtt-responder.c for UDP sockets
and raw_recv.c and raw_send.c for raw sockets.

###### 8a. Hello world
on one computer:
```
./rtt-laur.out
```
on another computer
```
./rtt-laur.out -d 192.168.1.20
```

###### 8b. Test RTT
On one computer
```
./rtt-responder.out
```
on another computer
```
./rtt-sender -d 192.168.1.20
```

###### 8.c. Test raw sockets
On one computer
```
./raw_recv
```
on another computer
```
./raw_send
```

Success, <br>
laurentiu [dot] duca [at] gmail [dot] com
