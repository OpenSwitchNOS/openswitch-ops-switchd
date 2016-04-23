/* SFLOW CLI commands
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
 * File: sflow_vty.h
 *
 * Purpose:  To add SFLOW CLI configuration and display commands.
 */
#ifndef _SFLOW_VTY_H
#define _SFLOW_VTY_H

/* Maximum length of collector string which includes:
 * 1. Max length of IP address - 49
 * 2. Max length of port number - 5
 * 3. Max length of VRF name - 32
 * 4. To accommodate "/" between IP address, port number and VRF name - 3
 */
#define MAX_COLLECTOR_LENGTH 89
#define OVSDB_SFLOW_GLOBAL_ROW_NAME "global"

void
sflow_vty_init(void);

#endif
