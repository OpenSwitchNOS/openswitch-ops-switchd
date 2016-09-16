/* TUNNEL CLI commands
 *
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * File: tunnel_vty.h
 *
 * Purpose:  To add TUNNEL CLI configuration and display commands.
 */
#ifndef _TUNNEL_VTY_H
#define _TUNNEL_VTY_H

#define MAX_TUNNEL_LENGTH       15
#define MAX_INTF_LENGTH         15
#define MAX_VLAN_LENGTH         15

/* Help strings */
#define TUNNEL_NUM_HELP_STR             "Tunnel number\n"
#define TUNNEL_MODE_HELP_STR            "Select a tunnel mode\n"
#define TUNNEL_MODE_VXLAN_HELP_STR      "VxLAN tunnel mode for the interface\n"
#define TUNNEL_MODE_GRE_HELP_STR        "GRE tunnel mode for the interface\n"
#define TUNNEL_SOURCE_HELP_STR          "Source information\n"
#define TUNNEL_SOURCE_IP_HELP_STR       "Set the tunnel source IP\n"
#define TUNNEL_NO_SOURCE_IP_HELP_STR    "Remove tunnel source IP\n"
#define TUNNEL_DEST_HELP_STR            "Destination information\n"
#define TUNNEL_NO_DEST_IP_HELP_STR      "Remove the destination IP\n"
#define TUNNEL_TTL_HELP_STR             "Time to live\n"
#define TUNNEL_NO_TTL_HELP_STR          "Unset the TTL value\n"
#define TUNNEL_SOURCE_IF_HELP_STR       "Source interface\n"
#define TUNNEL_LOOPBACK_IF_HELP_STR     "Loopback interface\n"

/* Constants */
#define TUNNEL_MODE_GRE_STR     "gre"
#define TUNNEL_IPV4_TYPE_STR    "ipv4"

void cli_post_init(void);
void cli_pre_init(void);

#endif
