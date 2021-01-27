// SPDX-License-Identifier: GPL-2.0-only
/***
 *
 *  examples/xenomai/posix/rtt-requester.c
 *
 *  Round-Trip Time Requester - sends packet, receives echo, evaluates
 *                              and displays per-station round-trip times
 *
 *  Based on Ulrich Marx's module, adopted to RTmac and later ported over
 *  user space POSIX.
 *
 *  Copyright (C) 2002 Ulrich Marx <marx@kammer.uni-hannover.de>
 *                2002 Marc Kleine-Budde <kleine-budde@gmx.de>
 *                2006 Jan Kiszka <jan.kiszka@web.de>
 *				  2020 Laurentiu-Cristian Duca (laurentiu [dot] duca [at] gmail [dot] com)
 *
 *
 */

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
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/select.h>

#include "rtdm_uapi_net.h"
#include "rtt_syscalls_nr.h"
#include "rtt_time.h"

void catch_signal(int sig);
int receive();

//#define vanilla 1
//#define with_recvmsg 1
#define with_recvfrom 1
#define SKIP_POLL_SELECT 1
//#define USE_POLL	1

char *dest_ip_s = "127.0.0.1";
char *local_ip_s = "";
/* if encounter errors use a bigger cycle */
unsigned int cycle = 10000; /* 10 ms */

pthread_t xmit_thread;
pthread_t recv_thread;

#define RCV_PORT                35999
#define XMT_PORT                36000

#define DEFAULT_ADD_BUFFERS     30

struct sockaddr_in dest_addr;

int sock;
#if 0
mqd_t mq;
#endif

#define BUFSIZE 1500
union {
    char            data[BUFSIZE];
    time_struct_64 tx_date;
} rx_packet, tx_packet;

struct station_stats {
    struct in_addr  addr;
    long long       last, min, max;
    unsigned long   count;
};
struct station_stats pstat;

struct packet_stats {
    struct in_addr  addr;
    long long       rtt;
};
struct packet_stats stats;

#define MAX_STATIONS 100
static struct station_stats station[MAX_STATIONS];

#define N_SAMPLES	(1000*1000)
int n_samples=0;
float samples[N_SAMPLES];

/* receiver */
struct msghdr       msgrcv;
struct iovec        iovrcv;
struct sockaddr_in  rx_addr;
struct timespec     rx_date;
int                 ret;
socklen_t rx_addrlen = sizeof(rx_addr);            /* length of addresses */

int transmitreceive()
{
    struct sched_param  param = { .sched_priority = 80 };
    struct timespec     next_period;
    struct timespec     tx_date;
    struct msghdr       msgxmit;
    struct iovec        iovxmit;
	int ret;
	
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    clock_gettime(CLOCK_MONOTONIC, &next_period);

    while(1) {
        next_period.tv_nsec += cycle * 1000;
        if (next_period.tv_nsec >= 1000000000) {
            next_period.tv_nsec = 0;
            next_period.tv_sec++;
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_period, NULL);

        clock_gettime(CLOCK_MONOTONIC, &tx_date);
		tx_packet.tx_date.tv_sec = tx_date.tv_sec;
		tx_packet.tx_date.tv_nsec = tx_date.tv_nsec;
		
		msgxmit.msg_name       = &dest_addr;
		msgxmit.msg_namelen    = sizeof(struct sockaddr_in);
		msgxmit.msg_iov        = &iovxmit;
		msgxmit.msg_iovlen     = 1;
		msgxmit.msg_control    = NULL;
		msgxmit.msg_controllen = 0;
		iovxmit.iov_base = &tx_packet;
		iovxmit.iov_len = sizeof(time_struct_64);

#if 0
		printf("tx_date.tv_sec=%ld tx_date.tv_nsec=%ld\n",
			   tx_date.tv_sec, tx_date.tv_nsec);
#endif
		
        /* transmit the request packet containing the local time */
#ifdef vanilla		
        if (sendto(sock, &tx_packet, sizeof(time_struct_64), 0, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in)) < 0) {
#elif with_recvmsg
		if (syscall(SYS_sendmsg_rtnet, sock, &msgxmit, 0) < 0) {
#else /* with_recvfrom */
		if (syscall(SYS_sendto_rtnet, sock, &tx_packet, sizeof(time_struct_64), 0, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in)) < 0) {
#endif
			printf("send failed: %s (%d)\n", strerror(errno), errno);
            return -1;
        }

		if((ret = receive()) < 0)
			return ret;
			
		/* taken from main */
        pstat.last = stats.rtt;
        if (pstat.last < pstat.min)
            pstat.min = pstat.last;
        if (pstat.last > pstat.max)
            pstat.max = pstat.last;
		samples[n_samples++] = pstat.last/1000.;
        pstat.count++;

		printf("%s %9.3f us, min=%9.3f us, max=%9.3f us, count=%ld\r",
               inet_ntoa(stats.addr), (float)pstat.last/1000,
               (float)pstat.min/1000, (float)pstat.max/1000, pstat.count);
		
		if(n_samples >= N_SAMPLES)
			catch_signal(0);
#if 0
        ret = mq_send(mq, (char *)&stats, sizeof(stats), 0);
		if(ret) {
			printf("mq_send error:%d \n", ret);
			return ret;
		}
#endif
    }			
}

int receive_prepare()
{
    msgrcv.msg_name       = &rx_addr;
    msgrcv.msg_namelen    = sizeof(rx_addr);
    msgrcv.msg_iov        = &iovrcv;
    msgrcv.msg_iovlen     = 1;
    msgrcv.msg_control    = NULL;
    msgrcv.msg_controllen = 0;

        iovrcv.iov_base = &rx_packet;
        iovrcv.iov_len  = sizeof(rx_packet);
}
		
int receive()
{
#ifndef SKIP_POLL_SELECT
#ifdef USE_POLL
	struct pollfd pfd;
	pfd.fd = sock;
	pfd.events = EPOLLIN | EPOLLRDNORM | EPOLLRDBAND;
#else
	fd_set set;
#endif
#endif
	
#ifndef SKIP_POLL_SELECT
#ifdef USE_POLL
#define POLL_INIFINITE_TIMEOUT -1
#ifdef vanilla
		if((ret = poll(&pfd, 1, POLL_INIFINITE_TIMEOUT)) < 0) {
#else
		if((ret = syscall(SYS_poll_rtnet, &pfd, 1, POLL_INIFINITE_TIMEOUT)) < 0) {
#endif
			perror("poll");
			printf("poll error: %d\n", errno);
			return NULL;
		}
		 if(!(pfd.revents & (EPOLLIN | EPOLLRDNORM | EPOLLRDBAND))) {
			printf("\npoll pfd.events=%4x pfd.revents=%4x\n\n", pfd.events, pfd.revents);
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
			//printf("select FD_ISSET(sock, &set)=%x\n", FD_ISSET(sock, &set));
		}
#endif
#endif /* SKIP_POLL_SELECT */
			
#ifdef vanilla
        ret = recvmsg(sock, &msgrcv, 0);
#elif with_recvmsg
		ret = syscall(SYS_recvmsg_rtnet, sock, &msgrcv, 0);
#else /* with_recvfrom */
		ret = syscall(SYS_recvfrom_rtnet, sock, &rx_packet, BUFSIZE, 0, (struct sockaddr *)&rx_addr, &rx_addrlen);	
#endif
        if (ret <= 0) {
            printf("terminating receiver thread\n");
            return ret;
        }

        clock_gettime(CLOCK_MONOTONIC, &rx_date);
        stats.rtt = rx_date.tv_sec * 1000000000LL + rx_date.tv_nsec;
        stats.rtt -= rx_packet.tx_date.tv_sec * 1000000000LL +
            rx_packet.tx_date.tv_nsec;
        stats.addr = rx_addr.sin_addr;
}

void catch_signal(int sig)
{
	FILE *f;
	
    /* write timing data to file */
    f = fopen("rtt.csv", "w");
    if(f != NULL) {
		for (int j = 0; j < n_samples; j++)
        	fprintf(f, "%.2f\n", samples[j]);
        fclose(f);
		printf("\nsaved timing data\n");
    } else {
    	printf("\nfopen error\n");
	}

    printf("closing socket...\n");
    while ((close(sock) < 0) && (errno == EAGAIN)) {
        printf("socket busy - waiting...\n");
        sleep(1);
    }
	exit(0);
}
	
int main(int argc, char *argv[])
{
    struct sockaddr_in local_addr;
    int add_rtskbs = DEFAULT_ADD_BUFFERS;
    pthread_attr_t thattr;
    char mqname[32];
    struct mq_attr mqattr;
    int stations = 0;
    int ret;

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
    if (ret != add_rtskbs)
        perror("WARNING: ioctl(RTNET_RTIOC_EXTPOOL)");
	 else 
		printf("ioctl RTNET_RTIOC_EXTPOOL OK\n");
#endif
	
#if 0		
    /* create statistics message queue */
    snprintf(mqname, sizeof(mqname), "/rtt-sender-%d", getpid());
    mqattr.mq_flags   = 0;
    mqattr.mq_maxmsg  = 100;
    mqattr.mq_msgsize = sizeof(struct packet_stats);
    mq = mq_open(mqname, O_RDWR | O_CREAT | O_EXCL, 0600, &mqattr);
    if (mq == (mqd_t)-1) {
        perror("opening mqueue failed");
		printf("mq=%d\n", mq);
        close(sock);
        return 1;
    }
		
	ret = mq_receive(mq, (char *)&pack, sizeof(pack), NULL);
	if (ret < (int)sizeof(pack)) {
		printf("mq_receive error:%d \n", ret);
		break;
	}
#endif

	pstat.min = LONG_MAX;
	pstat.max = LONG_MIN;
	pstat.count = 0;
	n_samples = 0;
	receive_prepare();
	if((ret = transmitreceive()) < 0)
		printf("transmit_receive returns %d\n", ret);
	
    printf("closing socket...\n");
    while ((close(sock) < 0) && (errno == EAGAIN)) {
        printf("socket busy - waiting...\n");
        sleep(1);
    }

	catch_signal(0);
		
    return 0;
}
