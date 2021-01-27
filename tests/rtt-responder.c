/***
 *
 *  examples/xenomai/posix/rtt-responder.c
 *
 *  Round-Trip Time Responder - listens and sends back a packet
 *
 *  Based on Ulrich Marx's module, later ported over user space POSIX.
 *
 *  Copyright (C) 2002 Ulrich Marx <marx@kammer.uni-hannover.de>
 *                2002 Marc Kleine-Budde <kleine-budde@gmx.de>
 *                2004, 2006 Jan Kiszka <jan.kiszka@web.de>
 *				  2020, Laurentiu-Cristian Duca <laurentiu.duca@gmail.com>
 *
 *  RTnet - real-time networking example
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <limits.h>
#include <sys/ioctl.h>

#include "rtdm_uapi_net.h"
#include "rtt_syscalls_nr.h"
#include "rtt_time.h"

//#define vanilla 1
//#define with_recvmsg 1
#define with_recvfrom 1

char *dest_ip_s = "";
char *local_ip_s  = "";
unsigned int reply_size = 0;
int add_rtskbs = 30;

pthread_t rt_thread;

#define RCV_PORT    36000
#define XMT_PORT    35999

struct sockaddr_in dest_addr;

int sock;

#define BUFSIZE 1500
union {
    char            data[BUFSIZE];
    time_struct_64 tx_date;
} packet;

void *responder(void* arg)
{
    struct sched_param  param = { .sched_priority = 81 };
    struct msghdr       rx_msg;
    struct iovec        iov;
    ssize_t             ret;
    int done=0;
	struct sockaddr_in remaddr;     /* remote address */
	socklen_t addrlen = sizeof(remaddr);            /* length of addresses */

    if (dest_addr.sin_addr.s_addr == INADDR_ANY) {
		printf("rx_msg.msg_name    = &dest_addr;\n");
        rx_msg.msg_name    = &dest_addr;
        rx_msg.msg_namelen = sizeof(dest_addr);
    } else {
		printf("rx_msg.msg_name    = NULL;\n");
        rx_msg.msg_name    = NULL;
        rx_msg.msg_namelen = 0;
    }
    rx_msg.msg_iov         = &iov;
    rx_msg.msg_iovlen      = 1;
    rx_msg.msg_control     = NULL;
    rx_msg.msg_controllen  = 0;

    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    while(1) {
        iov.iov_base = &packet;
        iov.iov_len  = sizeof(packet);
	//if(!done)
	    //if(xntrace_user_start() < 0)
        //        perror("xntrace_user_start:");

#ifdef vanilla
		ret = recvmsg(sock, &rx_msg, 0);
#elif with_recvmsg
		ret = syscall(SYS_recvmsg_rtnet, sock, &rx_msg, 0);
#else /* with_recvfrom */
		ret = syscall(SYS_recvfrom_rtnet, sock, &packet, BUFSIZE, 0, (struct sockaddr *)&dest_addr, &addrlen);			
#endif		
        if (ret <= 0) {
            printf("terminating responder thread\n");
            return NULL;
        }
	//if(!done) {
	//	xntrace_user_freeze(0xf, 1);
	//	done = 1;
	//}
		
#ifdef vanilla
		sendmsg(sock, &rx_msg, 0);
        //sendto(sock, &packet, reply_size ? : ret, 0, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in));
#elif with_recvmsg
		syscall(SYS_sendmsg_rtnet, sock, &rx_msg, 0);
#else /* with_recvfrom */
		syscall(SYS_sendto_rtnet, sock, &packet, ret, 0, (struct sockaddr *)&dest_addr, addrlen);
#endif
#if 0
		printf("ret=%d, packet.tx_date.tv_sec=%ld packet.tx_date.tv_nsec=%ld sent=%d\n",
			   ret, packet.tx_date.tv_sec, packet.tx_date.tv_nsec, reply_size ? : ret);
#endif
    }
	
}


void catch_signal(int sig)
{
}


int main(int argc, char *argv[])
{
    struct sockaddr_in local_addr;
    pthread_attr_t thattr;
    int ret, err;

#ifdef vanilla
	printf("version vanilla\n");
#elif with_recvmsg
	printf("version with_recvmsg\n");
#elif with_recvfrom
	printf("version with_recvfrom\n");
#else
	printf("version unknown\n");
#endif
	
    while (1) {
        switch (getopt(argc, argv, "d:l:s:")) {
            case 'd':
                dest_ip_s = optarg;
                break;

            case 'l':
                local_ip_s = optarg;
                break;

            case 's':
                reply_size = atoi(optarg);
                break;

            case -1:
                goto end_of_opt;

            default:
                printf("usage: %s [-d <dest_ip>] [-l <local_ip>] "
                       "[-s <reply_size>]\n", argv[0]);
                return 0;
        }
    }
 end_of_opt:

    if (dest_ip_s[0]) {
        inet_aton(dest_ip_s, &dest_addr.sin_addr);
        dest_addr.sin_port = htons(XMT_PORT);
    } else
        dest_addr.sin_addr.s_addr = INADDR_ANY;

    if (local_ip_s[0])
        inet_aton(local_ip_s, &local_addr.sin_addr);
    else
        local_addr.sin_addr.s_addr = INADDR_ANY;

    if (reply_size > 65505)
        reply_size = 65505;
    else if (reply_size < sizeof(time_struct_64))
        reply_size = sizeof(time_struct_64);

    signal(SIGTERM, catch_signal);
    signal(SIGINT, catch_signal);
    signal(SIGHUP, catch_signal);
    mlockall(MCL_CURRENT|MCL_FUTURE);

    printf("destination ip address: %s = %08x\n",
           dest_ip_s[0] ? dest_ip_s : "SENDER", dest_addr.sin_addr.s_addr);
    printf("local ip address: %s = %08x\n",
           local_ip_s[0] ? local_ip_s : "INADDR_ANY", local_addr.sin_addr.s_addr);
    printf("reply size: %d\n", reply_size);

    /* create rt-socket */
#ifdef vanilla
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#else
	if ((sock = syscall(SYS_socket_rtnet, AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#endif
        perror("socket cannot be created");
        return 1;
    }

    /* bind the rt-socket to local_addr */
    local_addr.sin_family = AF_INET;
    local_addr.sin_port   = htons(RCV_PORT);
#ifdef vanilla
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
#else
	if (syscall(SYS_bind_rtnet, sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
#endif
        perror("cannot bind to local ip/port");
        close(sock);
        return 1;
    }

#ifndef vanilla
    /* extend the socket pool */
    ret = ioctl(sock, RTNET_RTIOC_EXTPOOL, &add_rtskbs);
    if (ret != add_rtskbs) {
		perror("ioctl(RTNET_RTIOC_EXTPOOL)");
        printf("WARNING: ioctl(RTNET_RTIOC_EXTPOOL) = %d\n", ret);
    } else 
		printf("ioctl RTNET_RTIOC_EXTPOOL OK\n");
#endif

	responder(NULL);
	printf("closing socket ...\n");
	ret = close(sock);
	printf("close returns %d\n", ret);

    return 0;
}
