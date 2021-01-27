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
#include <linux/in.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

#include "rtt_syscalls_nr.h"

//#define vanilla 1
//#define with_recvmsg 1
//#define with_recvfrom 1

unsigned short checksum(unsigned short* buff, int _16bitword)
{
	unsigned long sum;
	for(sum=0;_16bitword>0;_16bitword--) 
		sum+=htons(*(buff)++);
	sum = ((sum >> 16) + (sum & 0xFFFF));
	sum += (sum>>16);
	return (unsigned short)(~sum);
}

int main()
{
	int sock_raw, send_len;
	struct ifreq ifreq_i;
	struct ifreq ifreq_c;
	struct ifreq ifreq_ip;
	unsigned char *sendbuff;
	struct ethhdr *eth;
	int total_len=0, ret;
	struct iphdr *iph;
	struct udphdr *uh;
	struct sockaddr_ll sadr_ll;

#ifdef vanilla
	sock_raw=socket(AF_PACKET,SOCK_RAW,IPPROTO_RAW);
#else
	sock_raw= syscall(SYS_socket_rtnet, AF_PACKET,SOCK_RAW,IPPROTO_RAW);
#endif
	if(sock_raw < 0) {
		perror("error in socket");
		return -1;
	}

	// get interface index
	memset(&ifreq_i,0,sizeof(ifreq_i));
	strncpy(ifreq_i.ifr_name,"rteth0",IFNAMSIZ-1); 
 	if((ioctl(sock_raw,SIOCGIFINDEX,&ifreq_i))<0) {
		printf("index ioctl reading: err=%d %s\n", errno, strerror(errno));
		return -1;
	}
 	printf("index=%d\n",ifreq_i.ifr_ifindex);
	
	// get interface mac
	memset(&ifreq_c,0,sizeof(ifreq_c));
	strncpy(ifreq_c.ifr_name,"rteth0",IFNAMSIZ-1); 
	if((ioctl(sock_raw,SIOCGIFHWADDR,&ifreq_c))<0) {
		printf("SIOCGIFHWADDR ioctl: err=%d %s\n", errno, strerror(errno));
		return -1;
	} else {
		printf("mac address: %x %x %x %x %x %x\n", 
			   ifreq_c.ifr_hwaddr.sa_data[0], ifreq_c.ifr_hwaddr.sa_data[1], ifreq_c.ifr_hwaddr.sa_data[2],
			   ifreq_c.ifr_hwaddr.sa_data[3], ifreq_c.ifr_hwaddr.sa_data[4], ifreq_c.ifr_hwaddr.sa_data[5]);
	}
	
	// get ip address of the interface
	memset(&ifreq_ip,0,sizeof(ifreq_ip));
	strncpy(ifreq_ip.ifr_name,"rteth0",IFNAMSIZ-1);
	if(ioctl(sock_raw,SIOCGIFADDR,&ifreq_ip)<0) {
		printf("SIOCGIFADDR ioctl: err=%d %s\n", errno, strerror(errno));
		return -1;
	} else {
		printf("ip src: %s\n", inet_ntoa((((struct sockaddr_in *)&(ifreq_ip.ifr_addr))->sin_addr)));
	}
	
	// construct eth header
	sendbuff=(unsigned char*)malloc(64); // increase in case of more data
	memset(sendbuff,0,64);
	eth = (struct ethhdr *)(sendbuff);
	eth->h_source[0] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[0]);
	eth->h_source[1] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[1]);
	eth->h_source[2] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[2]);
	eth->h_source[3] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[3]);
	eth->h_source[4] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[4]);
	eth->h_source[5] = (unsigned char)(ifreq_c.ifr_hwaddr.sa_data[5]);

	/* filling destination mac. DESTMAC0 to DESTMAC5 are macro having octets of mac address. */
	// gateway: 44:ff:ba:5b:6d:b6
#if 0
	// rootfs-master: 52:54:00:11:22:33
	eth->h_dest[0] = 0x52; //DESTMAC0;
	eth->h_dest[1] = 0x54; //DESTMAC1;
	eth->h_dest[2] = 0x00; //DESTMAC2;
	eth->h_dest[3] = 0x11; //DESTMAC3;
	eth->h_dest[4] = 0x22; //DESTMAC4;
	eth->h_dest[5] = 0x33; //DESTMAC5;
#endif
	//rpi-4: DC:A6:32:C3:43:EE
	eth->h_dest[0] = 0xDC; //DESTMAC0;
	eth->h_dest[1] = 0xA6; //DESTMAC1;
	eth->h_dest[2] = 0x32; //DESTMAC2;
	eth->h_dest[3] = 0xC3; //DESTMAC3;
	eth->h_dest[4] = 0x43; //DESTMAC4;
	eth->h_dest[5] = 0xEE; //DESTMAC5;
	
	// ETH_P_IPV6 because rtnet does not support IPV6, and ETH_P_IP is taken by rt_ip_rcv()
	eth->h_proto = htons(ETH_P_IPV6);
	/* end of ethernet header */
	total_len+=sizeof(struct ethhdr);
		
	// construct ip header
	iph = (struct iphdr*)(sendbuff + sizeof(struct ethhdr));
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 16;
	iph->id = htons(10201);
	iph->ttl = 64;
	iph->protocol = 17;
	iph->saddr = inet_addr(inet_ntoa((((struct sockaddr_in *)&(ifreq_ip.ifr_addr))->sin_addr)));
	iph->daddr = inet_addr("192.168.1.40"); // put destination IP address
	total_len += sizeof(struct iphdr);
	
	// construct udp header
	uh = (struct udphdr *)(sendbuff + sizeof(struct iphdr) + sizeof(struct ethhdr));
	uh->source = htons(23451);
	uh->dest = htons(23452);
	uh->check = 0;
	total_len+= sizeof(struct udphdr);
	
	// data to send
	sendbuff[total_len++] = 0xAA;
	sendbuff[total_len++] = 0xBB;
	sendbuff[total_len++] = 0xCC;
	sendbuff[total_len++] = 0xDD;
	sendbuff[total_len++] = 0xEE;
	
	//Filling the remaining fields of the IP and UDP headers
	//UDP length field
	uh->len = htons((total_len - sizeof(struct iphdr) - sizeof(struct ethhdr)));
	//IP length field
	iph->tot_len = htons(total_len - sizeof(struct ethhdr));
	
	// The IP header checksum
	iph->check = checksum((unsigned short*)(sendbuff + sizeof(struct ethhdr)), (sizeof(struct iphdr)/2));
	
	// Send the packet
	//struct sockaddr_ll sadr_ll;
	sadr_ll.sll_family = AF_PACKET;
	sadr_ll.sll_ifindex = ifreq_i.ifr_ifindex; // index of interface
	sadr_ll.sll_halen = ETH_ALEN; // length of destination mac address
#if 0
	sadr_ll.sll_addr[0] = 0x52; //DESTMAC0;
	sadr_ll.sll_addr[1] = 0x54; //DESTMAC1;
	sadr_ll.sll_addr[2] = 0x00; //DESTMAC2;
	sadr_ll.sll_addr[3] = 0x11; //DESTMAC3;
	sadr_ll.sll_addr[4] = 0x22; //DESTMAC4;
	sadr_ll.sll_addr[5] = 0x33; //DESTMAC5;
#endif
	//rpi-4: DC:A6:32:C3:43:EE
	sadr_ll.sll_addr[0] = 0xDC; //DESTMAC0;
	sadr_ll.sll_addr[1] = 0xA6; //DESTMAC1;
	sadr_ll.sll_addr[2] = 0x32; //DESTMAC2;
	sadr_ll.sll_addr[3] = 0xC3; //DESTMAC3;
	sadr_ll.sll_addr[4] = 0x43; //DESTMAC4;
	sadr_ll.sll_addr[5] = 0xEE; //DESTMAC5;
	
#ifdef vanilla
	send_len = sendto(sock_raw, sendbuff, 64, 0, (const struct sockaddr*)&sadr_ll,sizeof(struct sockaddr_ll));
#else
	send_len = syscall(SYS_sendto_rtnet, sock_raw, sendbuff, 64, 0, (const struct sockaddr*)&sadr_ll,sizeof(struct sockaddr_ll));
#endif
	if(send_len<0) {
		printf("error in sending....sendlen=%d....errno=%d\n",send_len,errno);
		return -1;
	}
	
	printf("\n");
	
	return 0;
}

