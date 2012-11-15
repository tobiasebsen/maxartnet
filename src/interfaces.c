/*
 *  interfaces.c
 *  maxartnet
 *
 *  Created by Tobias Ebsen on 11/15/12.
 *  Copyright 2012 Tobias Ebsen. All rights reserved.
 *
 */

#include "interfaces.h"

#include <errno.h>

#ifndef WIN32
#include <sys/socket.h> // socket before net/if.h for mac
#include <net/if.h>
#include <sys/ioctl.h>
#else
typedef int socklen_t;
#include <winsock2.h>
#include <Lm.h>
#include <iphlpapi.h>
#endif

#include <unistd.h>

#include "private.h"

#ifdef HAVE_GETIFADDRS
#ifdef HAVE_LINUX_IF_PACKET_H
#define USE_GETIFADDRS
#endif
#endif

#ifdef USE_GETIFADDRS
#include <ifaddrs.h>
#include <linux/types.h> // required by if_packet
#include <linux/if_packet.h>
#endif


enum { INITIAL_IFACE_COUNT = 10 };
enum { IFACE_COUNT_INC = 5 };
enum { IFNAME_SIZE = 32 }; // 32 sounds a reasonable size

typedef struct iface_s {
	struct sockaddr_in ip_addr;
	struct sockaddr_in bcast_addr;
	int8_t hw_addr[ARTNET_MAC_SIZE];
	char   if_name[IFNAME_SIZE];
	struct iface_s *next;
} iface_t;

//////////////////////////////////////////////////////////////////////

int get_ifaces(iface_t **if_head);
void free_ifaces(iface_t *head);

//////////////////////////////////////////////////////////////////////


iface iface_get_first() {

	iface_t *ift_head = NULL;
	get_ifaces(&ift_head);
	return ift_head;
}

iface iface_get_next(iface i) {
	iface_t *ift = (iface_t*)i;
	return ift->next;
}

void iface_free(iface first) {
	free_ifaces(first);
}

char* iface_get_name(iface i) {
	iface_t *ift = (iface_t*)i;
	return ift->if_name;
}

char* iface_get_ip(iface i) {
	iface_t *ift = (iface_t*)i;
	return inet_ntoa(ift->ip_addr.sin_addr);
}

char* iface_get_bcast(iface i) {
	iface_t *ift = (iface_t*)i;
	return inet_ntoa(ift->bcast_addr.sin_addr);
}

void iface_get_hwaddr(iface i, char* hwaddr) {
	iface_t *ift = (iface_t*)i;
	sprintf(hwaddr, "%02X:%02X:%02X:%02X:%02X:%02X", ift->hw_addr[0], ift->hw_addr[1], ift->hw_addr[2], ift->hw_addr[3], ift->hw_addr[4], ift->hw_addr[5]);
}
