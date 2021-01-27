/***
 *  Author: L-C. Duca
 *  Date: 2020/06/11
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
#define _GNU_SOURCE
#include <errno.h>
#include <mqueue.h>
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
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/select.h>

#include "rtdm_uapi_net.h"
#include "rtt_syscalls_nr.h"

//#define vanilla 1
#define with_recvmsg 1
//#define with_recvfrom 1

//#define use_timeout
//#define SKIP_POLL_SELECT
//#define USE_POLL	1

char *dest_ip_s = "127.0.0.1";
char *local_ip_s = "";
unsigned int cycle = 50000; /* 50 ms */

pthread_t xmit_thread;
pthread_t recv_thread;

#define RCV_PORT                35999
#define XMT_PORT                RCV_PORT
//36000

#define DEFAULT_ADD_BUFFERS     30

struct sockaddr_in dest_addr;

int sock;
mqd_t mq;

#define BUFSIZE 1500
char recvbuf[BUFSIZE];

union {
    char            data[BUFSIZE];
    struct timespec tx_date;
} packet;

struct station_stats {
    struct in_addr  addr;
    long long       last, min, max;
    unsigned long   count;
};

struct packet_stats {
    struct in_addr  addr;
    long long       rtt;
};

#define MAX_STATIONS 100
static struct station_stats station[MAX_STATIONS];


static struct station_stats *lookup_stats(struct in_addr addr)
{
    int i;

    for (i = 0; i < MAX_STATIONS; i++) {
        if (station[i].addr.s_addr == addr.s_addr)
            break;
        if (station[i].addr.s_addr == 0) {
            station[i].addr = addr;
            station[i].min  = LONG_MAX;
            station[i].max  = LONG_MIN;
            break;
        }
    }
    if (i == MAX_STATIONS)
        return NULL;
    return &station[i];
}


void *transmitter(void *arg)
{
    struct sched_param  param = { .sched_priority = 80 };
	struct timespec tx_date;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	char *str="abcd";
	int ret;
    struct msghdr       msg;
    struct iovec        iov;

    msg.msg_name       = &dest_addr;
    msg.msg_namelen    = sizeof(struct sockaddr_in);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
	iov.iov_base = str;
	iov.iov_len = strlen(str);
	
	clock_gettime(CLOCK_MONOTONIC, &tx_date);

        /* transmit the request packet containing the local time */
#ifdef vanilla
        if ((ret = sendto(sock, str, strlen(str), 0, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in))) < 0) {
#elif with_recvmsg
		if ((ret = syscall(SYS_sendmsg_rtnet, sock, &msg, 0)) < 0) {
#else /* with_recvfrom */
		if ((ret = syscall(SYS_sendto_rtnet, sock, str, strlen(str), 0, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in))) < 0) {
#endif
            if (errno == EBADF)
                printf("terminating transmitter thread\n");
            else
                perror("sendto failed");
            return NULL;
        } else
#ifdef vanilla
			printf("sendto done: %d\n", ret);
#elif with_recvmsg
			printf("SYS_sendmsg_rtnet done: %d\n", ret);
#else
			printf("SYS_sendto_rtnet done: %d\n", ret);
#endif
	
	return (void *)0;
}


void *receiver(void *arg)
{
    struct sched_param  param = { .sched_priority = 82 };
    struct msghdr       msg;
    struct iovec        iov;
    struct sockaddr_in  addr;
    struct timespec     rx_date;
    struct packet_stats stats;
    int                 ret;
    struct sockaddr_in remaddr;     /* remote address */
    socklen_t addrlen = sizeof(remaddr);            /* length of addresses */
#ifdef USE_POLL
	struct pollfd pfd;
	pfd.fd = sock;
	pfd.events = EPOLLIN | EPOLLRDNORM | EPOLLRDBAND;
#else
	fd_set set;
#endif

    msg.msg_name       = &addr;
    msg.msg_namelen    = sizeof(addr);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;

    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

        iov.iov_base = &packet;
        iov.iov_len  = sizeof(packet);

#ifndef SKIP_POLL_SELECT
#ifdef USE_POLL
#define POLL_INIFINITE_TIMEOUT -1
		printf("launching poll() ...\n");
#ifdef vanilla
		if((ret = poll(&pfd, 1, POLL_INIFINITE_TIMEOUT)) < 0) {
#else
		if((ret = syscall(SYS_poll_rtnet, &pfd, 1, POLL_INIFINITE_TIMEOUT)) < 0) {
#endif
			perror("poll");
			printf("poll error: %d\n", errno);
			return NULL;
		} else
			printf("\npoll pfd.events=%4x pfd.revents=%4x\n\n", pfd.events, pfd.revents);
		if(!(pfd.revents & (EPOLLIN | EPOLLRDNORM | EPOLLRDBAND))) {
			printf("\npoll no events...\n");
			return NULL;
		}
#else
		// select instead of poll
		FD_ZERO(&set);
		FD_SET(sock, &set);
#ifdef vanilla
		ret = select(sock+1, &set, NULL, NULL, NULL);
#else
		ret = syscall(SYS_select_rtnet, sock+1, &set, NULL, NULL, NULL);
#endif
		if (ret < 0) {
			printf("select() error: %d\n", errno);
            perror("select()");
			return NULL;
		} else {
			printf("select FD_ISSET(sock, &set)=%x\n", FD_ISSET(sock, &set));
		}
#endif
#endif /* ifndef SKIP_POLL_SELECT */
			
#ifdef vanilla
        ret = recvmsg(sock, &msg, 0);
#elif with_recvmsg
		ret = syscall(SYS_recvmsg_rtnet, sock, &msg, 0);
#else /* with_recvfrom */
		ret = syscall(SYS_recvfrom_rtnet, sock, recvbuf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
#endif
        if (ret <= 0) {
            printf("terminating receiver thread, errno=%d %s\n", errno, strerror(errno));
            return NULL;
        } else {
#ifdef vanilla
			printf("recvmsg done: iov=%s ret=%d IP=%s\n", (char*)iov.iov_base, ret, 
				   inet_ntoa(((struct sockaddr_in*)msg.msg_name)->sin_addr));
#elif with_recvmsg
			printf("SYS_recvmsg_rtnet done: iov=%s ret=%d IP=%s\n", (char*)iov.iov_base, ret,
				   inet_ntoa(((struct sockaddr_in*)msg.msg_name)->sin_addr));
#else /* with_recvfrom */
			recvbuf[ret] = 0;
			printf("SYS_recvfrom_rtnet done: recvbuf=%s ret=%d\n", recvbuf, ret);
#endif
		}
	return (void*)0;
}


void catch_signal(int sig)
{
    //mq_close(mq);
}


int main(int argc, char *argv[])
{
    struct sched_param param = { .sched_priority = 1 };
    struct sockaddr_in local_addr;
    int add_rtskbs = DEFAULT_ADD_BUFFERS;
    pthread_attr_t thattr;
    char mqname[32];
    struct mq_attr mqattr;
    int stations = 0;
    int ret;
	int transmit=0;
#ifdef use_timeout
	long long nsecs=(long long)(5*1000)*(long long)(1000*1000);
#endif

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
        switch (getopt(argc, argv, "d:l:c:b:")) {
            case 'd':
				transmit=1;
                dest_ip_s = optarg;
                break;

            case 'l':
                local_ip_s = optarg;
                break;

            case 'c':
                cycle = atoi(optarg);
                break;

            case 'b':
                add_rtskbs = atoi(optarg);

            case -1:
                goto end_of_opt;

            default:
                printf("usage: %s [-d <dest_ip>] [-l <local_ip>] "
                       "[-c <cycle_microsecs>] [-b <add_buffers>]\n",
                       argv[0]);
                return 0;
        }
    }
 end_of_opt:

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(XMT_PORT);
    if (dest_ip_s[0])
        inet_aton(dest_ip_s, &dest_addr.sin_addr);
    else
        dest_addr.sin_addr.s_addr = INADDR_ANY;

    if (local_ip_s[0])
        inet_aton(local_ip_s, &local_addr.sin_addr);
    else
        local_addr.sin_addr.s_addr = INADDR_ANY;

    signal(SIGTERM, catch_signal);
    signal(SIGINT, catch_signal);
    signal(SIGHUP, catch_signal);
    mlockall(MCL_CURRENT|MCL_FUTURE);

    printf("destination ip address: %s = %08x\n",
           dest_ip_s[0] ? dest_ip_s : "SENDER", dest_addr.sin_addr.s_addr);
    printf("local ip address: %s = %08x\n",
           local_ip_s[0] ? local_ip_s : "INADDR_ANY", local_addr.sin_addr.s_addr);
    printf("cycle: %d us\n", cycle);

    /* create rt-socket */
#ifdef vanilla
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#else
	if ((sock = syscall(SYS_socket_rtnet, AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#endif
        perror("socket cannot be created");
        return 1;
    } else
		printf("socket() returns %d\n", sock);

#ifdef use_timeout
	ret = ioctl(sock, RTNET_RTIOC_TIMEOUT, &nsecs);
    if (ret != 0) {
		printf("WARNING: ioctl(RTNET_RTIOC_TIMEOUT) = %d, errno=%d\n", ret, errno);
		perror("ioctl(RTNET_RTIOC_TIMEOUT)");
	} else 
		printf("ioctl RTNET_RTIOC_TIMEOUT OK\n");
#endif
		
	if(!transmit) {
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
	}
    if (transmit) {
		// sender
		transmitter(NULL);
	} else {
		receiver(NULL);
	}
	
    /* This call also leaves primary mode, required for socket cleanup. */
    printf("closing socket\n");

    /* Note: The following loop is no longer required since Xenomai 2.4,
     *       plain close works as well. */
#if 0
    while ((close(sock) < 0) && (errno == EAGAIN)) {
        printf("socket busy - waiting...\n");
        sleep(1);
    }
#endif
	ret = close(sock);
	printf("close returns %d\n", ret);

    return 0;
}
