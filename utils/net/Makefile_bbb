# Tested with linaro toolchain (gcc-linaro-7.4.1-2019.02-x86_64_arm-linux-gnueabihf)
CC = arm-linux-gnueabihf-gcc
CFLAGS = -I/home/laur/lucru/raspberry-pi/robertcnelson/ti-linux-kernel-dev/rtnet/linux-5.4.5-rtnet/net/rtnet/stack/include

all: rtifconfig rtping rtroute
rtifconfig: rtifconfig.o
	$(CC) -o rtifconfig rtifconfig.o $(LFLAGS) 
rtifconfig.o: rtifconfig.c
	$(CC) -c -o rtifconfig.o rtifconfig.c $(CFLAGS) $(LIBCFLAGS)
rtping: rtping.o
	$(CC) -o rtping rtping.o $(LFLAGS) 
rtping.o: rtping.c
	$(CC) -c -o rtping.o rtping.c $(CFLAGS) $(LIBCFLAGS)
rtroute: rtroute.o
	$(CC) -o rtroute rtroute.o $(LFLAGS) $(LFLAGS_ALCHEMY)
rtroute.o: rtroute.c
	$(CC) -c -o rtroute.o rtroute.c $(CFLAGS) $(LIBCFLAGS)

.PHONY: clean all

clean:
	rm -rf *.out *.so *.o rtifconfig rtping rtroute

