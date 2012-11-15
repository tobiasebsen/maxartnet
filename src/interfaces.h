/*
 *  interfaces.h
 *  maxartnet
 *
 *  Created by Tobias Ebsen on 11/15/12.
 *  Copyright 2012 Tobias Ebsen. All rights reserved.
 *
 */

typedef void* iface;

iface iface_get_first();
iface iface_get_next(iface i);
void iface_free(iface first);
char* iface_get_name(iface i);
char* iface_get_ip(iface i);
char* iface_get_bcast(iface i);
void iface_get_hwaddr(iface i, char* hwaddr);