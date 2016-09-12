/* TUNNEL CLI commands
 *
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * File: tunnel_vty.h
 *
 * Purpose:  To add TUNNEL CLI configuration and display commands.
 */
#ifndef _TUNNEL_VTY_H
#define _TUNNEL_VTY_H

#define MAX_TUNNEL_LENGTH   15
#define MAX_INTF_LENGTH     15
#define MAX_VLAN_LENGTH     15

/* Help strings */
#define TUNNEL_HELP_STR "Create a tunnel interface\n"
#define TUNNEL_NUM_HELP_STR "Tunnel number\n"
#define TUNNEL_MODE_HELP_STR "Select a tunnel mode\n"
#define TUNNEL_MODE_OPTS_HELP_STR "Tunnel mode for the interface\n"
#define IPV4_HELP_STR "IPv4 information\n"

/* Constants */
#define TUNNEL_MODE_GRE_STR "gre"
#define TUNNEL_IPV4_TYPE_STR "ipv4"
#define TUNNEL_GRE_IPV4_STR "gre_ipv4"

void cli_post_init(void);
void cli_pre_init(void);

#endif
