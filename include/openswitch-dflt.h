/*
 * Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
 * All Rights Reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *    License for the specific language governing permissions and limitations
 *    under the License.
 *
 * File:    openswitch-dflt.h
 *
 * Purpose: This file contains default values for various columns in the OVSDB.
 *          The purpose is to avoid hard-coded values inside each module/daemon code.
 *
 */

#ifndef OPENSWITCH_DFLT_HEADER
#define OPENSWITCH_DFLT_HEADER 1

/************************* Open vSwitch Table  ***************************/

/* Interface Statistics update interval should
 * always be greater than or equal to 5 seconds. */
#define DFLT_SYSTEM_OTHER_CONFIG_STATS_UPDATE_INTERVAL        5000

/* Default min_vlan ID for internal VLAN range */
#define DFLT_SYSTEM_OTHER_CONFIG_MAP_MIN_INTERNAL_VLAN_ID     1024

/* Default max_vlan ID for internal VLAN range */
#define DFLT_SYSTEM_OTHER_CONFIG_MAP_MAX_INTERNAL_VLAN_ID     4094

/* Defaults and min/max values LACP parameters */
#define DFLT_SYSTEM_LACP_CONFIG_SYSTEM_PRIORITY   65534
#define MIN_SYSTEM_LACP_CONFIG_SYSTEM_PRIORITY    0
#define MAX_SYSTEM_LACP_CONFIG_SYSTEM_PRIORITY    65535

#define MIN_INTERFACE_OTHER_CONFIG_LACP_PORT_ID                 1
#define MAX_INTERFACE_OTHER_CONFIG_LACP_PORT_ID                 65535
#define MIN_INTERFACE_OTHER_CONFIG_LACP_PORT_PRIORITY           1
#define MAX_INTERFACE_OTHER_CONFIG_LACP_PORT_PRIORITY           65535
#define MIN_INTERFACE_OTHER_CONFIG_LACP_AGGREGATION_KEY         1
#define MAX_INTERFACE_OTHER_CONFIG_LACP_AGGREGATION_KEY         65535
#define DFLT_INTERFACE_HW_INTF_INFO_MAP_BRIDGE                  false

#define MAX_NEXTHOPS_PER_ROUTE                                      32

/* Default for port hw_config */
#define PORT_HW_CONFIG_MAP_ENABLE_DEFAULT                       "true"

#endif /* OPENSWITCH_DFLT_HEADER */
