/* inspired from https://opensourceforu.com/2015/03/a-guide-to-using-raw-sockets */
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
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include "rtdm_uapi_net.h"
#include "rtt_syscalls_nr.h"

//#define vanilla 1

int main()
{
	int sock_r;
	unsigned char *buffer;
	struct sockaddr saddr;
	struct sockaddr_in source, dest;
	int saddr_len = sizeof (struct sockaddr);
	struct ethhdr *eth;
	int iphdrlen, buflen;
	struct iphdr *ip;
	struct udphdr *udp;
	int i;
	
#ifdef vanilla
	sock_r=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
#else
	//sock_r= syscall(SYS_socket_rtnet, AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	sock_r= syscall(SYS_socket_rtnet, AF_PACKET, SOCK_RAW, htons(ETH_P_IPV6));
#endif
	if(sock_r < 0) {
		perror("error in socket");
		return -1;
	} else
		printf("sock_r=%d\n", sock_r);

	buffer = (unsigned char *) malloc(65536); //to receive data
	memset(buffer,0,65536);
	 
	//Receive a network packet and copy in to buffer
#ifdef vanilla
	buflen=recvfrom(sock_r,buffer,65536,0,&saddr,(socklen_t *)&saddr_len);
#else
	buflen= syscall(SYS_recvfrom_rtnet, sock_r, buffer,65536,0,&saddr,(socklen_t *)&saddr_len);
#endif
	if(buflen<0) {
		perror("error in reading recvfrom function\n");
		return -1;
	} else
		printf("buflen=%d\n", buflen);
	
	eth = (struct ethhdr *)(buffer);
	printf("\t|Ethernet Header\n");
	printf("\t|-Source Address : %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",eth->h_source[0],eth->h_source[1],eth->h_source[2],eth->h_source[3],eth->h_source[4],eth->h_source[5]);
	printf("\t|-Destination Address : %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",eth->h_dest[0],eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]);
	printf("\t|-Protocol : %x\n",ntohs(eth->h_proto));
	printf("\n");
	
	ip = (struct iphdr*)(buffer + sizeof(struct ethhdr));
	memset(&source, 0, sizeof(source));
	source.sin_addr.s_addr = ip->saddr;
	memset(&dest, 0, sizeof(dest));
	dest.sin_addr.s_addr = ip->daddr;
	printf("\t|-Version : %d\n",(unsigned int)ip->version);
	printf("\t|-Internet Header Length : %d DWORDS or %d Bytes\n",(unsigned int)ip->ihl,((unsigned int)(ip->ihl))*4);
	printf("\t|-Type Of Service : %d\n",(unsigned int)ip->tos);
	printf("\t|-Total Length : %d Bytes\n",ntohs(ip->tot_len));
	printf("\t|-Identification : %d\n",ntohs(ip->id));
	printf("\t|-Time To Live : %d\n",(unsigned int)ip->ttl);
	printf("\t|-Protocol : %d\n",(unsigned int)ip->protocol);
	printf("\t|-Header Checksum : %d\n",ntohs(ip->check));
	printf("\t|-Source IP : %s\n", inet_ntoa(source.sin_addr));
	printf("\t|-Destination IP : %s\n",inet_ntoa(dest.sin_addr));
	printf("\n");
	
	// udp header
	/* getting actual size of IP header*/
	iphdrlen = ip->ihl*4;
	/* getting pointer to udp header*/
	udp=(struct udphdr*)(buffer + iphdrlen + sizeof(struct ethhdr));
	printf("\t|-Source Port : %d\n" , ntohs(udp->source));
	printf("\t|-Destination Port : %d\n" , ntohs(udp->dest));
	printf("\t|-UDP Length : %d\n" , ntohs(udp->len));
	printf("\t|-UDP Checksum : %d\n" , ntohs(udp->check));
	printf("\n");
	
	// extract packet data
	unsigned char * data = (buffer + iphdrlen + sizeof(struct ethhdr) + sizeof(struct udphdr));
	int remaining_data = buflen - (iphdrlen + sizeof(struct ethhdr) + sizeof(struct udphdr));
 	for(i=0;i<remaining_data;i++) {
		if(i!=0 && i%16==0)
			printf("\n");
			printf(" %.2X ",data[i]);
	}
	printf("\n");
	
	return 0;
}
