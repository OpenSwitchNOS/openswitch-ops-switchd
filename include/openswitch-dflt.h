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

/* Default for port status */
#define PORT_STATUS_MAP_ERROR_DEFAULT                           "up"


/************************************************************************/
/*  OSPF Related declarations */
/************************************************************************/

#define OSPF_PASSIVE_INTERFACE_DEFAULT       "default"
#define OSPF_ROUTER_ID_STATIC_DEFAULT        "false"
#define OSPF_DEFAULT_INFO_ORIG_DEFAULT       "false"
#define OSPF_ALWAYS_DEFAULT                  "false"

#define OSPF_AUTO_COST_REF_BW_DEFAULT        "40000"
#define OSPF_DEFAULT_METRIC_DEFAULT          "20"
#define OSPF_LOG_ADJACENCY_CHGS_DEFAULT      "false"
#define OSPF_LOG_ADJACENCY_DETAIL_DEFAULT    "false"
#define OSPF_RFC1583_COMPATIBLE_DEFAULT      "false"
#define OSPF_ENABLE_OPAQUE_LSA_DEFAULT       "false"

#define OSPF_LSA_ARRIVAL_INTERVAL_DEFAULT    1000
#define OSPF_LSA_GROUP_PACING_DEFAULT        10

#define OSPF_AREA_TYPE_DEFAULT               "default"
#define OSPF_AREA_TYPE_NSSA                  "nssa"
#define OSPF_AREA_TYPE_STUB                  "stub"

#define OSPF_AREA_NO_SUMMARYDEFAULT          "false"
#define OSPF_AREA_STUB_DEFAULT_COST          "1"
#define OSPF_AREA_NSSA_TRANSLATOR_ROLE       "candidate"

#define OSPF_PRIORITY_DEFAULT                "1"

/* Stub router defaults */
#define OSPF_ROUTER_STUB_ADMIN_DEFAULT       "false"
#define OSPF_ROUTER_STUB_STARTUP_DEFAULT     "0"

#define OSPF_INTERFACE_IFSM_DEPEND_ON        "depend_upon"

/* Neighbor FSM */
#define OSPF_NEIGHBOR_FSM_FULL               "full"
#define OSPF_NEIGHBOR_FSM_DEPEND_UPON        "depend_upon"
#define OSPF_NEIGHBOR_FSM_DOWN               "down"
#define OSPF_NEIGHBOR_FSM_DELETED            "deleted"
#define OSPF_NEIGHBOR_FSM_ATTEMPT            "attempt"
#define OSPF_NEIGHBOR_FSM_INIT               "init"
#define OSPF_NEIGHBOR_FSM_TWO_WAY            "two_way"
#define OSPF_NEIGHBOR_FSM_EX_START           "ex_start"
#define OSPF_NEIGHBOR_FSM_EXCHANGE           "exchange"
#define OSPF_NEIGHBOR_FSM_LOADING            "loading"

/* Distance */
#define OSPF_ROUTER_DISTANCE_DEFAULT         110

/* SPF */
#define OSPF_SPF_HOLD_MULTIPLIER_DEFAULT     1

/* Area type */
#define OSPF_AREA_TYPE_DEFAULT               "default"
#define OSPF_AREA_TYPE_NSSA                  "nssa"
#define OSPF_AREA_TYPE_STUB                  "stub"
#define OSPF_AREA_TYPE_NSSA_NO_SUMMARY       "nssa"
#define OSPF_AREA_TYPE_STUB_NO_SUMMARY       "stub"

#endif /* OPENSWITCH_DFLT_HEADER */
