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
 * File:    openswitch-idl.h
 *
 * Purpose: This file contains manually generated #defines and enums to
 *          represent valid values for maps of string-string pairs in the
 *          OVSDB schema.  This is intended to be temporary until we can
 *          extend the schema & IDL generation code to produce these entries
 *          automatically.
 *
 *          For non-map columns, IDL should already automatically generate
 *          the necessary
 *          #defines in vswitch-idl.h file.
 */

#ifndef OPENSWITCH_IDL_HEADER
#define OPENSWITCH_IDL_HEADER 1

/****************************** Global Definitions ******************************/

/* Default VRF name used during system bootup */
#define DEFAULT_VRF_NAME                      "vrf_default"
/* Default bridge name used during system bootup */
#define DEFAULT_BRIDGE_NAME                   "bridge_normal"

/****************************** INTERFACE TABLE ******************************/

#define OVSREC_INTERFACE_ERROR_UNINITIALIZED            "uninitialized"
#define OVSREC_INTERFACE_ERROR_ADMIN_DOWN               "admin_down"
#define OVSREC_INTERFACE_ERROR_MODULE_MISSING           "module_missing"
#define OVSREC_INTERFACE_ERROR_MODULE_UNRECOGNIZED      "module_unrecognized"
#define OVSREC_INTERFACE_ERROR_MODULE_UNSUPPORTED       "module_unsupported"
#define OVSREC_INTERFACE_ERROR_LANES_SPLIT              "lanes_split"
#define OVSREC_INTERFACE_ERROR_LANES_NOT_SPLIT          "lanes_not_split"
#define OVSREC_INTERFACE_ERROR_INVALID_MTU              "invalid_mtu"
#define OVSREC_INTERFACE_ERROR_INVALID_SPEEDS           "invalid_speeds"
#define OVSREC_INTERFACE_ERROR_AUTONEG_NOT_SUPPORTED    "autoneg_not_supported"
#define OVSREC_INTERFACE_ERROR_AUTONEG_REQUIRED         "autoneg_required"
#define OVSREC_INTERFACE_ERROR_OK                       "ok"

#define OVSREC_PORT_ERROR_ADMIN_DOWN                    "port_admin_down"

/************************** UDP BROADCAST SERVER TABLE***********************/
#define SYSTEM_OTHER_CONFIG_MAP_UDP_BCAST_FWD_ENABLED   "udp_bcast_forwarder_enabled"

enum ovsrec_interface_error_e {
    INTERFACE_ERROR_UNINITIALIZED,
    INTERFACE_ERROR_ADMIN_DOWN,
    INTERFACE_ERROR_MODULE_MISSING,
    INTERFACE_ERROR_MODULE_UNRECOGNIZED,
    INTERFACE_ERROR_MODULE_UNSUPPORTED,
    INTERFACE_ERROR_LANES_SPLIT,
    INTERFACE_ERROR_LANES_NOT_SPLIT,
    INTERFACE_ERROR_INVALID_MTU,
    INTERFACE_ERROR_INVALID_SPEEDS,
    INTERFACE_ERROR_AUTONEG_NOT_SUPPORTED,
    INTERFACE_ERROR_AUTONEG_REQUIRED,
    PORT_ERROR_ADMIN_DOWN,
    INTERFACE_ERROR_OK
};

#define OVSREC_INTERFACE_PM_INFO_CABLE_TECHNOLOGY_ACTIVE        "active"
#define OVSREC_INTERFACE_PM_INFO_CABLE_TECHNOLOGY_PASSIVE       "passive"

enum ovsrec_interface_pm_info_cable_technology_e {
    INTERFACE_PM_INFO_CABLE_TECHNOLOGY_ACTIVE,
    INTERFACE_PM_INFO_CABLE_TECHNOLOGY_PASSIVE
};

#define INTERFACE_PM_INFO_MAP_CONNECTOR                         "connector"

#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4             "QSFP_CR4"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4             "QSFP_LR4"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4             "QSFP_SR4"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_CX               "SFP_CX"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_DAC              "SFP_DAC"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_FC               "SFP_FC"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LR               "SFP_LR"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LRM              "SFP_LRM"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LX               "SFP_LX"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45             "SFP_RJ45"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SR               "SFP_SR"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SX               "SFP_SX"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_ABSENT               "absent"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN              "unknown"

enum ovsrec_interface_pm_info_connector_e {
    INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4,
    INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4,
    INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4,
    INTERFACE_PM_INFO_CONNECTOR_SFP_CX,
    INTERFACE_PM_INFO_CONNECTOR_SFP_DAC,
    INTERFACE_PM_INFO_CONNECTOR_SFP_FC,
    INTERFACE_PM_INFO_CONNECTOR_SFP_LR,
    INTERFACE_PM_INFO_CONNECTOR_SFP_LRM,
    INTERFACE_PM_INFO_CONNECTOR_SFP_LX,
    INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45,
    INTERFACE_PM_INFO_CONNECTOR_SFP_SR,
    INTERFACE_PM_INFO_CONNECTOR_SFP_SX,
    INTERFACE_PM_INFO_CONNECTOR_ABSENT,
    INTERFACE_PM_INFO_CONNECTOR_UNKNOWN
};

#define INTERFACE_PM_INFO_MAP_CONNECTOR_STATUS                  "connector_status"

#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED     "supported"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED  "unrecognized"
#define OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED   "unsupported"

enum ovsrec_interface_pm_info_connector_status_e {
    INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED,
    INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED,
    INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED
};

#define OVSREC_INTERFACE_PM_INFO_POWER_MODE_HIGH                "high"
#define OVSREC_INTERFACE_PM_INFO_POWER_MODE_LOW                 "low"

enum ovsrec_interface_pm_info_power_mode_e {
    INTERFACE_PM_INFO_POWER_MODE_HIGH,
    INTERFACE_PM_INFO_POWER_MODE_LOW
};

#define INTERFACE_OTHER_CONFIG_MAP_LACP_PORT_ID                 "lacp-port-id"
#define INTERFACE_OTHER_CONFIG_MAP_LACP_PORT_PRIORITY           "lacp-port-priority"
#define INTERFACE_OTHER_CONFIG_MAP_LACP_AGGREGATION_KEY         "lacp-aggregation-key"

#define INTERFACE_USER_CONFIG_MAP_ADMIN                         "admin"

#define OVSREC_INTERFACE_USER_CONFIG_ADMIN_DOWN                 "down"
#define OVSREC_INTERFACE_USER_CONFIG_ADMIN_UP                   "up"

enum ovsrec_interface_user_config_admin_e {
    INTERFACE_USER_CONFIG_ADMIN_DOWN,
    INTERFACE_USER_CONFIG_ADMIN_UP
};

#define INTERFACE_USER_CONFIG_MAP_AUTONEG                       "autoneg"

#define INTERFACE_USER_CONFIG_MAP_AUTONEG_OFF                   "off"
#define INTERFACE_USER_CONFIG_MAP_AUTONEG_ON                    "on"
#define INTERFACE_USER_CONFIG_MAP_AUTONEG_DEFAULT               "default"

enum ovsrec_interface_user_config_autoneg_e {
    INTERFACE_USER_CONFIG_AUTONEG_OFF,
    INTERFACE_USER_CONFIG_AUTONEG_ON,
    INTERFACE_USER_CONFIG_AUTONEG_DEFAULT
};

#define INTERFACE_USER_CONFIG_MAP_SPEEDS                        "speeds"

#define INTERFACE_USER_CONFIG_MAP_MTU                           "mtu"

#define INTERFACE_USER_CONFIG_MAP_PAUSE                         "pause"

#define INTERFACE_USER_CONFIG_MAP_PAUSE_NONE                    "none"
#define INTERFACE_USER_CONFIG_MAP_PAUSE_RX                      "rx"
#define INTERFACE_USER_CONFIG_MAP_PAUSE_TX                      "tx"
#define INTERFACE_USER_CONFIG_MAP_PAUSE_RXTX                    "rxtx"

enum ovsrec_interface_user_config_pause_e {
    INTERFACE_USER_CONFIG_PAUSE_NONE,
    INTERFACE_USER_CONFIG_PAUSE_RX,
    INTERFACE_USER_CONFIG_PAUSE_TX,
    INTERFACE_USER_CONFIG_PAUSE_RXTX
};

#define INTERFACE_USER_CONFIG_MAP_DUPLEX                        "duplex"

#define INTERFACE_USER_CONFIG_MAP_DUPLEX_HALF                   "half"
#define INTERFACE_USER_CONFIG_MAP_DUPLEX_FULL                   "full"

enum ovsrec_interface_user_config_duplex_e {
    INTERFACE_USER_CONFIG_DUPLEX_HALF,
    INTERFACE_USER_CONFIG_DUPLEX_FULL
};

#define INTERFACE_USER_CONFIG_MAP_LANE_SPLIT                    "lane_split"
#define INTERFACE_USER_CONFIG_MAP_LANE_SPLIT_NO_SPLIT           "no-split"
#define INTERFACE_USER_CONFIG_MAP_LANE_SPLIT_SPLIT              "split"

enum ovsrec_interface_user_config_lane_split_e {
    INTERFACE_USER_CONFIG_LANE_SPLIT_DEFAULT,
    INTERFACE_USER_CONFIG_LANE_SPLIT_NO_SPLIT,
    INTERFACE_USER_CONFIG_LANE_SPLIT_SPLIT
};

#define INTERFACE_HW_INTF_CONFIG_MAP_ENABLE                     "enable"

#define INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_FALSE               "false"
#define INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_TRUE                "true"

enum ovsrec_interface_hw_intf_config_enable_e {
    INTERFACE_HW_INTF_CONFIG_ENABLE_FALSE,
    INTERFACE_HW_INTF_CONFIG_ENABLE_TRUE
};

#define INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG                    "autoneg"

#define INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_OFF                "off"
#define INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_ON                 "on"
#define INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_DEFAULT            "default"

enum ovsrec_interface_hw_intf_config_autoneg_e {
    INTERFACE_HW_INTF_CONFIG_AUTONEG_OFF,
    INTERFACE_HW_INTF_CONFIG_AUTONEG_ON,
    INTERFACE_HW_INTF_CONFIG_AUTONEG_DEFAULT
};

#define INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS                     "speeds"

#define INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX                     "duplex"

#define INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_HALF                "half"
#define INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_FULL                "full"

enum ovsrec_interface_hw_intf_config_duplex_e {
    INTERFACE_HW_INTF_CONFIG_DUPLEX_HALF,
    INTERFACE_HW_INTF_CONFIG_DUPLEX_FULL
};

#define INTERFACE_HW_INTF_CONFIG_MAP_PAUSE                      "pause"

#define INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_NONE                 "none"
#define INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RX                   "rx"
#define INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_TX                   "tx"
#define INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RXTX                 "rxtx"

enum ovsrec_interface_hw_intf_config_pause_e {
    INTERFACE_HW_INTF_CONFIG_PAUSE_NONE,
    INTERFACE_HW_INTF_CONFIG_PAUSE_RX,
    INTERFACE_HW_INTF_CONFIG_PAUSE_TX,
    INTERFACE_HW_INTF_CONFIG_PAUSE_RXTX
};

#define INTERFACE_HW_INTF_CONFIG_MAP_MTU                        "mtu"

#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE             "interface_type"

#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_UNKNOWN     "unknown"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_BACKPLANE   "backplane"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_1GBASE_SX   "1GBASE_SX"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_1GBASE_T    "1GBASE_T"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_10GBASE_CR  "10GBASE_CR"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_10GBASE_SR  "10GBASE_SR"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_10GBASE_LR  "10GBASE_LR"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_10GBASE_LRM "10GBASE_LRM"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_40GBASE_CR4 "40GBASE_CR4"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_40GBASE_SR4 "40GBASE_SR4"
#define INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE_40GBASE_LR4 "40GBASE_LR4"

enum ovsrec_interface_hw_intf_config_interface_type_e {
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_UNKNOWN,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_BACKPLANE,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_1GBASE_SX,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_1GBASE_T,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_CR,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_SR,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_LR,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_LRM,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_CR4,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_SR4,
    INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_LR4
};

#define INTERFACE_HW_INTF_INFO_MAP_SWITCH_UNIT                  "switch_unit"
#define INTERFACE_HW_INTF_INFO_MAP_SWITCH_INTF_ID               "switch_intf_id"
#define INTERFACE_HW_INTF_INFO_MAP_MAC_ADDR                     "mac_addr"
#define INTERFACE_HW_INTF_INFO_MAP_MAX_SPEED                    "max_speed"
#define INTERFACE_HW_INTF_INFO_MAP_SPEEDS                       "speeds"
#define INTERFACE_HW_INTF_INFO_MAP_CONNECTOR                    "connector"
#define INTERFACE_HW_INTF_INFO_MAP_PLUGGABLE                    "pluggable"
#define INTERFACE_HW_INTF_INFO_MAP_ENET1G                       "enet1G"
#define INTERFACE_HW_INTF_INFO_MAP_ENET10G                      "enet10G"
#define INTERFACE_HW_INTF_INFO_MAP_ENET40G                      "enet40G"
#define INTERFACE_HW_INTF_INFO_MAP_SPLIT_4                      "split_4"
#define INTERFACE_HW_INTF_INFO_SPLIT_PARENT                     "split_parent"

#define INTERFACE_HW_INTF_INFO_MAP_CONNECTOR_RJ45               "RJ45"
#define INTERFACE_HW_INTF_INFO_MAP_CONNECTOR_SFP_PLUS           "SFP_PLUS"
#define INTERFACE_HW_INTF_INFO_MAP_CONNECTOR_QSFP_PLUS          "QSFP_PLUS"

enum ovsrec_interface_hw_intf_connector_e {
    INTERFACE_HW_INTF_INFO_CONNECTOR_UNKNOWN,
    INTERFACE_HW_INTF_INFO_CONNECTOR_RJ45,
    INTERFACE_HW_INTF_INFO_CONNECTOR_SFP_PLUS,
    INTERFACE_HW_INTF_INFO_CONNECTOR_QSFP_PLUS
};

#define INTERFACE_HW_INTF_INFO_MAP_PLUGGABLE_FALSE              "false"
#define INTERFACE_HW_INTF_INFO_MAP_PLUGGABLE_TRUE               "true"

#define INTERFACE_HW_INTF_INFO_MAP_SPLIT_4_FALSE                "false"
#define INTERFACE_HW_INTF_INFO_MAP_SPLIT_4_TRUE                 "true"

enum ovsrec_interface_hw_intf_info_pluggable_e {
    INTERFACE_HW_INTF_INFO_PLUGGABLE_FALSE,
    INTERFACE_HW_INTF_INFO_PLUGGABLE_TRUE
};

#define INTERFACE_HW_INTF_INFO_MAP_TYPE                         "type"
#define INTERFACE_HW_INTF_INFO_MAP_TYPE_BRIDGE                  "bridge"
/* OPS_TODO: Remove above INTERFACE_HW_INTF_INFO_MAP_TYPE
   and INTERFACE_HW_INTF_INFO_MAP_TYPE_BRIDGE
   after fixing daemons */
#define INTERFACE_HW_INTF_INFO_MAP_BRIDGE                       "bridge"
#define INTERFACE_HW_INTF_INFO_MAP_BRIDGE_FALSE                 "false"
#define INTERFACE_HW_INTF_INFO_MAP_BRIDGE_TRUE                  "true"

#define INTERFACE_HW_BOND_CONFIG_MAP_RX_ENABLED                 "rx_enabled"
#define INTERFACE_HW_BOND_CONFIG_MAP_TX_ENABLED                 "tx_enabled"

#define INTERFACE_HW_BOND_CONFIG_MAP_ENABLED_FALSE              "false"
#define INTERFACE_HW_BOND_CONFIG_MAP_ENABLED_TRUE               "true"

enum ovsrec_interface_hw_bond_config_enabled_e {
    INTERFACE_HW_BOND_CONFIG_ENABLED_FALSE,
    INTERFACE_HW_BOND_CONFIG_ENABLED_TRUE
};

/* lldp interface statistics */

/* required as per the design doc */
#define INTERFACE_STATISTICS_LLDP_TX_COUNT              "lldp_tx"
#define INTERFACE_STATISTICS_LLDP_TX_LEN_ERR            "lldp_tx_len_err"
#define INTERFACE_STATISTICS_LLDP_RX_COUNT              "lldp_rx"
#define INTERFACE_STATISTICS_LLDP_RX_ERR                "lldp_rx_err"
#define INTERFACE_STATISTICS_LLDP_RX_DISCARDED_COUNT    "lldp_rx_discard"
#define INTERFACE_STATISTICS_LLDP_RX_TLV_DISCARD        "lldp_rx_tlv_discard"
#define INTERFACE_STATISTICS_LLDP_RX_TLV_UNKNOWN        "lldp_rx_tlv_unknown"

/* extras available */
#define INTERFACE_STATISTICS_LLDP_RX_UNRECOGNIZED_COUNT "lldp_rx_unrecognized"
#define INTERFACE_STATISTICS_LLDP_AGEOUT_COUNT          "lldp_ageout"
#define INTERFACE_STATISTICS_LLDP_INSERT_COUNT          "lldp_insert"
#define INTERFACE_STATISTICS_LLDP_DELETE_COUNT          "lldp_delete"
#define INTERFACE_STATISTICS_LLDP_DROP_COUNT            "lldp_drop"

#define INTERFACE_OTHER_CONFIG_MAP_LLDP_ENABLE_DIR      "lldp_enable_dir"

#define INTERFACE_OTHER_CONFIG_MAP_LLDP_ENABLE_DIR_OFF  "off"
#define INTERFACE_OTHER_CONFIG_MAP_LLDP_ENABLE_DIR_RX   "rx"
#define INTERFACE_OTHER_CONFIG_MAP_LLDP_ENABLE_DIR_TX   "tx"
#define INTERFACE_OTHER_CONFIG_MAP_LLDP_ENABLE_DIR_RXTX "rxtx"

#define INTERFACE_LACP_STATUS_MAP_ACTOR_SYSTEM_ID       "actor_system_id"
#define INTERFACE_LACP_STATUS_MAP_ACTOR_PORT_ID         "actor_port_id"
#define INTERFACE_LACP_STATUS_MAP_ACTOR_KEY             "actor_key"
#define INTERFACE_LACP_STATUS_MAP_ACTOR_STATE           "actor_state"
#define INTERFACE_LACP_STATUS_MAP_PARTNER_SYSTEM_ID     "partner_system_id"
#define INTERFACE_LACP_STATUS_MAP_PARTNER_PORT_ID       "partner_port_id"
#define INTERFACE_LACP_STATUS_MAP_PARTNER_KEY           "partner_key"
#define INTERFACE_LACP_STATUS_MAP_PARTNER_STATE         "partner_state"

#define INTERFACE_LACP_STATUS_STATE_ACTIVE              "Activ"
#define INTERFACE_LACP_STATUS_STATE_TIMEOUT             "TmOut"
#define INTERFACE_LACP_STATUS_STATE_AGGREGATION         "Aggr"
#define INTERFACE_LACP_STATUS_STATE_SYNCHRONIZATION     "Sync"
#define INTERFACE_LACP_STATUS_STATE_COLLECTING          "Col"
#define INTERFACE_LACP_STATUS_STATE_DISTRIBUTING        "Dist"
#define INTERFACE_LACP_STATUS_STATE_DEFAULTED           "Def"
#define INTERFACE_LACP_STATUS_STATE_EXPIRED             "Exp"

/****************************** PORT TABLE *******************************/

#define PORT_STATUS_BOND_HW_HANDLE                      "bond_hw_handle"
#define PORT_HW_CONFIG_MAP_INTERNAL_VLAN_ID             "internal_vlan_id"
#define PORT_HW_CONFIG_MAP_ENABLE                       "enable"
#define PORT_HW_CONFIG_MAP_ENABLE_FALSE                 "false"
#define PORT_HW_CONFIG_MAP_ENABLE_TRUE                  "true"

#define PORT_STATUS_MAP_ERROR                           "error"
#define PORT_STATUS_MAP_ERROR_NO_INTERNAL_VLAN          "no_internal_vlan"

#define PORT_OTHER_CONFIG_MAP_LACP_TIME                 "lacp-time"

#define PORT_OTHER_CONFIG_LACP_FALLBACK                 "lacp-fallback"

#define PORT_OTHER_CONFIG_LACP_TIME_SLOW                "slow"
#define PORT_OTHER_CONFIG_LACP_TIME_FAST                "fast"

#define PORT_OTHER_CONFIG_MAP_LACP_SYSTEM_PRIORITY      "lacp-system-priority"
#define PORT_OTHER_CONFIG_MAP_LACP_SYSTEM_ID            "lacp-system-id"

#define PORT_OTHER_CONFIG_MAP_BOND_MODE                 "bond_mode"

#define PORT_OTHER_CONFIG_MAP_PROXY_ARP_ENABLED         "proxy_arp_enabled"
#define PORT_OTHER_CONFIG_MAP_PROXY_ARP_ENABLED_TRUE    "true"

#define PORT_LACP_STATUS_MAP_BOND_SPEED                 "bond_speed"
#define PORT_LACP_STATUS_MAP_BOND_STATUS                "bond_status"
#define PORT_LACP_STATUS_MAP_BOND_STATUS_REASON         "bond_status_reason"

#define PORT_LACP_STATUS_BOND_STATUS_OK                 "ok"
#define PORT_LACP_STATUS_BOND_STATUS_DOWN               "down"
#define PORT_LACP_STATUS_BOND_STATUS_DEFAULTED          "defaulted"

#define PORT_CONFIG_ADMIN_DOWN                          "down"

enum ovsrec_port_config_admin_e {
    PORT_ADMIN_CONFIG_DOWN,
    PORT_ADMIN_CONFIG_UP
};

/****************************** SUBSYSTEM TABLE *******************************/

#define SUBSYSTEM_OTHER_INFO_MAX_TRANSMISSION_UNIT       "max_transmission_unit"

/****************************** VLAN TABLE ******************************/

#define VLAN_HW_CONFIG_MAP_ENABLE                               "enable"

#define VLAN_HW_CONFIG_MAP_ENABLE_FALSE                         "false"
#define VLAN_HW_CONFIG_MAP_ENABLE_TRUE                          "true"
#define VLAN_INTERNAL_USAGE_L3PORT                              "l3port"

/************************* System TABLE  ***************************/

/* software_info column keys */
#define SYSTEM_SOFTWARE_INFO_OS_NAME                      "os_name"

#define SYSTEM_OTHER_CONFIG_MAP_CLI_SESSION_TIMEOUT       "cli_session_timeout"

/* LLDP related */
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_ENABLE               "lldp_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_ENABLE_DEFAULT       false

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TX_INTERVAL          "lldp_tx_interval"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TX_INTERVAL_DEFAULT  30
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TX_INTERVAL_MIN      5
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TX_INTERVAL_MAX      32768

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_HOLD                 "lldp_hold"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_HOLD_DEFAULT         4
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_HOLD_MIN             2
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_HOLD_MAX             10

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_REINIT               "lldp_reinit"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_REINIT_DEFAULT       2
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_REINIT_MIN           1
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_REINIT_MAX           10

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_MGMT_ADDR            "lldp_mgmt_addr"

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_SYS_NAME_ENABLE                 \
                                                   "lldp_tlv_sys_name_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_SYS_DESC_ENABLE                 \
                                                   "lldp_tlv_sys_desc_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_SYS_CAP_ENABLE                  \
                                                   "lldp_tlv_sys_cap_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_MGMT_ADDR_ENABLE                \
                                                   "lldp_tlv_mgmt_addr_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_PORT_DESC_ENABLE                \
                                                   "lldp_tlv_port_desc_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_PORT_VLAN_ID_ENABLE             \
                                            "lldp_tlv_port_vlan_id_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_PORT_PROTO_VLAN_ID_ENABLE       \
                                            "lldp_tlv_port_proto_vlan_id_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_PORT_VLAN_NAME_ENABLE           \
                                            "lldp_tlv_port_vlan_name_enable"
#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_PORT_PROTO_ID_ENABLE       \
                                            "lldp_tlv_port_proto_id_enable"

#define SYSTEM_OTHER_CONFIG_MAP_LLDP_TLV_DEFAULT        true

/* VLAN internal range */
#define SYSTEM_OTHER_CONFIG_MAP_MIN_INTERNAL_VLAN     "min_internal_vlan"
#define SYSTEM_OTHER_CONFIG_MAP_MAX_INTERNAL_VLAN     "max_internal_vlan"
#define SYSTEM_OTHER_CONFIG_MAP_INTERNAL_VLAN_POLICY  "internal_vlan_policy"

#define SYSTEM_OTHER_CONFIG_MAP_INTERNAL_VLAN_POLICY_ASCENDING_DEFAULT \
                                                            "ascending"
#define SYSTEM_OTHER_CONFIG_MAP_INTERNAL_VLAN_POLICY_DESCENDING        \
                                                            "descending"
/*Source interface selection parameters */
#define SYSTEM_OTHER_CONFIG_MAP_TFTP_SOURCE          "tftp_source"
#define SYSTEM_OTHER_CONFIG_MAP_PROTOCOLS_SOURCE     "protocols_source"

/*DHCP-Relay global configuration key */
#define SYSTEM_OTHER_CONFIG_MAP_DHCP_RELAY_DISABLED    "dhcp_relay_disabled"

/* lacp global configuration parameters */
#define SYSTEM_LACP_CONFIG_MAP_LACP_SYSTEM_ID        "lacp-system-id"
#define SYSTEM_LACP_CONFIG_MAP_LACP_SYSTEM_PRIORITY  "lacp-system-priority"

/* lldp global statistics */
#define OVSDB_STATISTICS_LLDP_TABLE_INSERTS         "lldp_table_inserts"
#define OVSDB_STATISTICS_LLDP_TABLE_DELETES         "lldp_table_deletes"
#define OVSDB_STATISTICS_LLDP_TABLE_DROPS           "lldp_table_drops"
#define OVSDB_STATISTICS_LLDP_TABLE_AGEOUTS         "lldp_table_ageouts"

/* BGP timers */
#define OVSDB_BGP_TIMER_KEEPALIVE       "keepalive"
#define OVSDB_BGP_TIMER_HOLDTIME        "holdtime"

/* ROUTE table global protocol specific column definitions */
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_FLAGS          "BGP_flags"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_AS_PATH        "BGP_AS_path"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_ORIGIN         "BGP_origin"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_LOC_PREF       "BGP_loc_pref"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_PEER_ID        "BGP_peer_ID"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_INTERNAL       "BGP_internal"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_IBGP           "BGP_iBGP"
#define OVSDB_ROUTE_PROTOCOL_SPECIFIC_BGP_UPTIME         "BGP_uptime"

/* BGP_ROUTE table path_attributes column definitions */
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_FLAGS          "BGP_flags"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_AS_PATH        "BGP_AS_path"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_ORIGIN         "BGP_origin"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_LOC_PREF       "BGP_loc_pref"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_INTERNAL       "BGP_internal"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_IBGP           "BGP_iBGP"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_UPTIME         "BGP_uptime"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_WEIGHT           "BGP_weight"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_AGGREGATOR_ID    "BGP_aggregator_id"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_AGGREGATOR_ADDR  "BGP_aggregator_addr"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_COMMUNITY        "BGP_community"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_ECOMMUNITY       "BGP_ecommunity"
#define OVSDB_BGP_ROUTE_PATH_ATTRIBUTES_ATOMIC_AGGREGATE "BGP_atomic_aggregate"

/* BGP Neighbor table status keys for clear commands*/
#define OVSDB_BGP_NEIGHBOR_CLEAR_COUNTERS_SOFT_OUT_PERFORMED   "bgp_num_clear_counters_peer_soft_out_performed"
#define OVSDB_BGP_NEIGHBOR_CLEAR_COUNTERS_SOFT_OUT_REQUESTED   "bgp_num_clear_counters_peer_soft_out_requested"
#define OVSDB_BGP_NEIGHBOR_CLEAR_COUNTERS_SOFT_IN_PERFORMED    "bgp_num_clear_counters_peer_soft_in_performed"
#define OVSDB_BGP_NEIGHBOR_CLEAR_COUNTERS_SOFT_IN_REQUESTED    "bgp_num_clear_counters_peer_soft_in_requested"

/* BGP Neighbor state, goes into "status" column */
#define BGP_PEER_STATE                          "bgp_peer_state"

/* BGP Neighbor statistics */
#define BGP_PEER_ESTABLISHED_COUNT              "bgp_peer_established_count"
#define BGP_PEER_DROPPED_COUNT                  "bgp_peer_dropped_count"
#define BGP_PEER_OPEN_IN_COUNT                  "bgp_peer_open_in_count"
#define BGP_PEER_OPEN_OUT_COUNT                 "bgp_peer_open_out_count"
#define BGP_PEER_UPDATE_IN_COUNT                "bgp_peer_update_in_count"
#define BGP_PEER_UPDATE_OUT_COUNT               "bgp_peer_update_out_count"
#define BGP_PEER_KEEPALIVE_IN_COUNT             "bgp_peer_keepalive_in_count"
#define BGP_PEER_KEEPALIVE_OUT_COUNT            "bgp_peer_keepalive_out_count"
#define BGP_PEER_NOTIFY_IN_COUNT                "bgp_peer_notify_in_count"
#define BGP_PEER_NOTIFY_OUT_COUNT               "bgp_peer_notify_out_count"
#define BGP_PEER_REFRESH_IN_COUNT               "bgp_peer_refresh_in_count"
#define BGP_PEER_REFRESH_OUT_COUNT              "bgp_peer_refresh_out_count"
#define BGP_PEER_DYNAMIC_CAP_IN_COUNT           "bgp_peer_dynamic_cap_in_count"
#define BGP_PEER_DYNAMIC_CAP_OUT_COUNT          "bgp_peer_dynamic_cap_out_count"
#define BGP_PEER_UPTIME                         "bgp_peer_uptime"
#define BGP_PEER_READTIME                       "bgp_peer_readtime"
#define BGP_PEER_RESETTIME                      "bgp_peer_resettime"

/****************************** VRF TABLE ******************************/

#define OVSDB_VRF_NAME_MAXLEN                       32

/****************************** NEIGHBOR TABLE ***************************/
#define OVSDB_NEIGHBOR_STATUS_DP_HIT                "dp_hit"
#define OVSDB_NEIGHBOR_STATUS_MAP_DP_HIT_DEFAULT    true

/****************************** NEXTHOP TABLE ***************************/
#define OVSDB_NEXTHOP_STATUS_ERROR                     "error"

/* Management Interface Column */
#define SYSTEM_MGMT_INTF_MAP_MODE                 "mode"

#define SYSTEM_MGMT_INTF_MAP_MODE_DHCP            "dhcp"
#define SYSTEM_MGMT_INTF_MAP_MODE_STATIC          "static"

#define SYSTEM_MGMT_INTF_MAP_NAME                 "name"
#define SYSTEM_MGMT_INTF_MAP_IP                   "ip"
#define SYSTEM_MGMT_INTF_MAP_IPV6                 "ipv6"
#define SYSTEM_MGMT_INTF_MAP_IPV6_LINKLOCAL       "ipv6_linklocal"
#define SYSTEM_MGMT_INTF_MAP_SUBNET_MASK          "subnet_mask"
#define SYSTEM_MGMT_INTF_MAP_DEFAULT_GATEWAY      "default_gateway"
#define SYSTEM_MGMT_INTF_MAP_DEFAULT_GATEWAY_V6   "default_gateway_v6"
#define SYSTEM_MGMT_INTF_MAP_DNS_SERVER_1         "dns_server_1"
#define SYSTEM_MGMT_INTF_MAP_DNS_SERVER_2         "dns_server_2"
#define SYSTEM_MGMT_INTF_MAP_HOSTNAME             "hostname"
#define SYSTEM_MGMT_INTF_MAP_DHCP_HOSTNAME        "dhcp_hostname"
#define SYSTEM_MGMT_INTF_MAP_DOMAIN_NAME          "domain_name"
#define SYSTEM_MGMT_INTF_MAP_DHCP_DOMAIN_NAME     "dhcp_domain_name"

/* buffer monitoring statistics config table (bufmon)*/
#define BUFMON_CONFIG_MAP_ENABLED                               "enabled"
#define BUFMON_CONFIG_MAP_COUNTERS_MODE                         "counters_mode"
#define BUFMON_CONFIG_MAP_PERIODIC_COLLECTION_ENABLED           "periodic_collection_enabled"
#define BUFMON_CONFIG_MAP_COLLECTION_PERIOD                     "collection_period"
#define BUFMON_CONFIG_MAP_THRESHOLD_TRIGGER_COLLECTION_ENABLED  "threshold_trigger_collection_enabled"
#define BUFMON_CONFIG_MAP_TRIGGER_RATE_LIMIT                    "threshold_trigger_rate_limit"
#define BUFMON_CONFIG_MAP_SNAPSHOT_ON_THRESHOLD_TRIGGER         "snapshot_on_threshold_trigger"
#define BUFMON_INFO_MAP_LAST_COLLECTION_TIMESTAMP               "last_collection_timestamp"

/* ECMP configuration (ecmp_config)*/
#define SYSTEM_ECMP_CONFIG_STATUS                         "enabled"
#define SYSTEM_ECMP_CONFIG_HASH_SRC_IP                    "hash_srcip_enabled"
#define SYSTEM_ECMP_CONFIG_HASH_SRC_PORT                  "hash_srcport_enabled"
#define SYSTEM_ECMP_CONFIG_HASH_DST_IP                    "hash_dstip_enabled"
#define SYSTEM_ECMP_CONFIG_HASH_DST_PORT                  "hash_dstport_enabled"
#define SYSTEM_ECMP_CONFIG_HASH_RESILIENT                 "resilient_hash_enabled"
#define SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT                 "true"

/************************************************************************/
/*  OSPF Related declarations */
/************************************************************************/

enum ospf_spf_key_types_e {
    OSPF_SPF_DELAY,
    OSPF_SPF_HOLD_TIME,
    OSPF_SPF_MAX_WAIT,
    OSPF_SPF_MAX
};

enum ospf_lsa_timer_config_types_e {
    OSPF_LSA_ARRIVAL_INTERVAL,
    OSPF_LSA_GROUP_PACING,
    OSPF_LSA_MAX
};


#define OSPF_ROUTER_DISTANCE_MAX \
                OSPF_ROUTER_DISTANCE_INTRA_AREA + 1

enum ospf_area_statistics_e {
    OSPF_AREA_STATISTICS_SPF_CALC,
    OSPF_AREA_STATISTICS_ABR_COUNT,
    OSPF_AREA_STATISTICS_ASBR_COUNT,
    OSPF_AREA_STATISTICS_MAX
};

enum ovs_ospf_area_type_e {
    OVS_OSPF_AREA_TYPE_DEFAULT,
    OVS_OSPF_AREA_TYPE_STUB,
    OVS_OSPF_AREA_TYPE_NSSA,
    OVS_OSPF_AREA_TYPE_MAX
};

enum ospf_if_intervals_e {
    OSPF_INTERVAL_TRANSMIT_DELAY,
    OSPF_INTERVAL_RETRANSMIT_INTERVAL,
    OSPF_INTERVAL_HELLO_INTERVAL,
    OSPF_INTERVAL_DEAD_INTERVAL,
    OSPF_INTERVAL_MAX
};

enum ospf_nbr_statistics_e {
   OSPF_NEIGHBOR_DB_SUMMARY_COUNT,
   OSPF_NEIGHBOR_LS_REQUEST_COUNT,
   OSPF_NEIGHBOR_LS_RETRANSMIT_COUNT,
   OSPF_NEIGHBOR_STATE_CHANGE_COUNT,
   OSPF_NEIGHBOR_STATISTICS_MAX
};

#define OSPF_INTERFACE_ACTIVE               1
#define OSPF_INTERFACE_NO_ACTIVE            0

#define OSPF_NUM_SPF_KEYS                   OSPF_SPF_MAX
#define OSPF_NUM_LSA_TIMER_KEYS             OSPF_LSA_MAX
#define OSPF_NUM_AREA_KEYS                  1

#define OSPF_KEY_ROUTER_ID_VAL              "router_id_val"

/* OSPF interface config */
#define OSPF_KEY_TRANSMIT_DELAY             "transmit_delay"
#define OSPF_KEY_RETRANSMIT_INTERVAL        "retransmit_interval"
#define OSPF_KEY_HELLO_INTERVAL             "hello_interval"
#define OSPF_KEY_DEAD_INTERVAL              "dead_interval"
#define OSPF_KEY_PRIORITY                   "priority"
#define OSPF_KEY_MTU_IGNORE                 "mtu_ignore"
#define OSPF_KEY_AUTH_CONF_TYPE             "auth_type"
#define OSPF_KEY_AUTH_CONF_KEY              "auth_key"
#define OSPF_KEY_INTERFACE_TYPE             "intf_type"
#define OSPF_KEY_INTERFACE_OUT_COST         "if_out_cost"
#define OSPF_KEY_HELLO_DUE                  "hello_due_at"

#define OSPF_KEY_DISTANCE_ALL               "all"
#define OSPF_KEY_DISTANCE_EXTERNAL          "external"
#define OSPF_KEY_DISTANCE_INTRA_AREA        "intra_area"
#define OSPF_KEY_DISTANCE_INTER_AREA        "inter_area"

#define OSPF_KEY_ROUTER_ID_STATIC           "router_id_static"
#define OSPF_KEY_ROUTER_ID_VAL              "router_id_val"
#define OSPF_KEY_DEFAULT_INFO_ORIG          "default_info_originate"
#define OSPF_KEY_ALWAYS                     "always"

/* SPF config */
#define OSPF_KEY_SPF_DELAY                  "spf_delay"
#define OSPF_KEY_SPF_HOLD_TIME              "spf_holdtime"
#define OSPF_KEY_SPF_MAX_WAIT               "spf_max_wait"

#define OSPF_KEY_SPF_HOLD_MULTIPLIER        "spf_hold_multiplier"
#define OSPF_KEY_CAPABILITY_OPAQUE          "capability_opaque"


#define OSPF_KEY_ROUTER_DEFAULT_METRIC      "default_metric"
#define OSPF_KEY_AUTO_COST_REF_BW           "auto_cost_ref_bw"
#define OSPF_KEY_DEFAULT_METRIC             "default_metric"
#define OSPF_KEY_LOG_ADJACENCY_CHGS         "log_adjacency_changes"
#define OSPF_KEY_LOG_ADJACENCY_DETAIL       "log_adjacency_details"
#define OSPF_KEY_RFC1583_COMPATIBLE         "ospf_rfc1583_compatible"
#define OSPF_KEY_ENABLE_OPAQUE_LSA          "enable_ospf_opaque_lsa"
#define OSPF_KEY_ENABLE_STUB_ROUTER_SUPPORT "stub_router_support"
#define OSPF_KEY_ENABLE_STUB_ROUTER_ACTIVE  "stub_router_state_active"
#define OSPF_KEY_ROUTER_STATUS_ABR          "abr"
#define OSPF_KEY_ROUTER_STATUS_ASBR         "asbr"
#define OSPF_KEY_OPAQUE_ORIGIN_BLOCKED      "opaque_origination_blocked"
#define OSPF_KEY_ROUTER_EXT_CHKSUM          "as_ext_lsas_sum_cksum"
#define OSPF_KEY_ROUTER_OPAQUE_CHKSUM       "opaque_as_lsas_sum_cksum"

/* Stub router config keys */
#define OSPF_KEY_ROUTER_STUB_ADMIN          "admin_set"
#define OSPF_KEY_ROUTER_STUB_STARTUP        "startup"

#define OSPF_KEY_ARRIVAL_INTERVAL           "lsa_arrival_interval"
#define OSPF_KEY_LSA_GROUP_PACING           "lsa_group_pacing"

#define OSPF_KEY_AREA_NO_SUMMARY            "no_summary"
#define OSPF_KEY_AREA_STUB_DEFAULT_COST     "stub_default_cost"
#define OSPF_KEY_AREA_NSSA_TRANSLATOR_ROLE  "NSSA_translator_role"

/* Area status */
#define OSPF_KEY_AREA_ACTIVE_INTERFACE      "active_interfaces"
#define OSPF_KEY_AREA_FULL_NEIGHBORS        "full_nbrs"
#define OSPF_KEY_AREA_SPF_LAST_RUN          "spf_last_run_timestamp"
#define OSPF_KEY_AREA_ROUTER_CHKSUM         "router_lsas_sum_cksum"
#define OSPF_KEY_AREA_NETWORK_CHKSUM        "network_lsas_sum_cksum"
#define OSPF_KEY_AREA_ABR_SUMMARY_CHKSUM    "abr_summary_lsas_sum_cksum"
#define OSPF_KEY_AREA_ASBR_SUMMARY_CHKSUM   "asbr_summary_lsas_sum_cksum"
#define OSPF_KEY_AREA_NSSA_CHKSUM           "as_nssa_lsas_sum_cksum"
#define OSPF_KEY_AREA_OPAQUE_AREA_CHKSUM    "opaque_area_lsas_sum_cksum"
#define OSPF_KEY_AREA_OPAQUE_LINK_CHKSUM    "opaque_link_lsas_sum_cksum"
#define OSPF_KEY_AREA_STATS_SPF_EXEC        "spf_calc"
#define OSPF_KEY_INTERFACE_ACTIVE           "active"

/* Neighbors */
#define OSPF_KEY_NEIGHBOR_DEAD_TIMER_DUE      "dead_timer_due"
#define OSPF_KEY_NEIGHBOR_DB_SUMMARY_CNT      "db_summary_count"
#define OSPF_KEY_NEIGHBOR_LS_REQUEST_CNT      "ls_request_count"
#define OSPF_KEY_NEIGHBOR_LS_RE_TRANSMIT_CNT  "ls_retransmit_count"
#define OSPF_KEY_NEIGHBOR_STATE_CHG_CNT       "state_changes_count"
#define OSPF_KEY_NEIGHBOR_LAST_UP_TIMESTAMP   "last_up_timestamp"

#define OSPF_KEY_ROUTER_STUB_ADV_STARTUP      "startup"

/******************************** NTP START **********************************/
/****************************** NTP_KEY TABLE ********************************/
#define NTP_KEY_KEY_PASSWORD_LEN_MIN                    8
#define NTP_KEY_KEY_PASSWORD_LEN_MAX                    16

#define NTP_KEY_KEY_ID_MIN                              1
#define NTP_KEY_KEY_ID_MAX                              65534

/************************** NTP_ASSOCIATION TABLE ****************************/

#define NTP_ASSOC_SERVER_NAME_LEN                       57
#define NTP_ASSOC_MAX_SERVERS                           8

/* NTP Association attributes (association_attributes) */
#define NTP_ASSOC_ATTRIB_REF_CLOCK_ID                   "ref_clock_id"

#define NTP_ASSOC_ATTRIB_PREFER                         "prefer"
#define NTP_ASSOC_ATTRIB_PREFER_DEFAULT                 "false"
#define NTP_ASSOC_ATTRIB_PREFER_DEFAULT_VAL             false

#define NTP_ASSOC_ATTRIB_VERSION                        "version"
#define NTP_ASSOC_ATTRIB_VERSION_3                      "3"
#define NTP_ASSOC_ATTRIB_VERSION_4                      "4"
#define NTP_ASSOC_ATTRIB_VERSION_DEFAULT                NTP_ASSOC_ATTRIB_VERSION_3

#define NTP_ASSOC_STATUS_REMOTE_PEER_ADDRESS            "remote_peer_address"
#define NTP_ASSOC_STATUS_REMOTE_PEER_REF_ID             "remote_peer_ref_id"

#define NTP_ASSOC_STATUS_STRATUM                        "stratum"
#define NTP_ASSOC_STATUS_STRATUM_MIN                    1
#define NTP_ASSOC_STATUS_STRATUM_MAX                    16

#define NTP_ASSOC_STATUS_PEER_TYPE                      "peer_type"
#define NTP_ASSOC_STATUS_PEER_TYPE_UNI_MANY_CAST        "uni_or_many_cast"
#define NTP_ASSOC_STATUS_PEER_TYPE_B_M_CAST             "bcast_or_mcast_client"
#define NTP_ASSOC_STATUS_PEER_TYPE_LOCAL_REF_CLOCK      "local_ref_clock"
#define NTP_ASSOC_STATUS_PEER_TYPE_SYMM_PEER            "symm_peer"
#define NTP_ASSOC_STATUS_PEER_TYPE_MANYCAST             "manycast_server"
#define NTP_ASSOC_STATUS_PEER_TYPE_BROADCAST            "bcast_server"
#define NTP_ASSOC_STATUS_PEER_TYPE_MULTICAST            "mcast_server"

#define NTP_ASSOC_STATUS_LAST_POLLED                    "last_polled"
#define NTP_ASSOC_STATUS_POLLING_INTERVAL               "polling_interval"
#define NTP_ASSOC_STATUS_REACHABILITY_REGISTER          "reachability_register"
#define NTP_ASSOC_STATUS_NETWORK_DELAY                  "network_delay"
#define NTP_ASSOC_STATUS_TIME_OFFSET                    "time_offset"
#define NTP_ASSOC_STATUS_JITTER                         "jitter"
#define NTP_ASSOC_STATUS_ROOT_DISPERSION                "root_dispersion"
#define NTP_ASSOC_STATUS_REFERENCE_TIME                 "reference_time"
#define NTP_ASSOC_STATUS_ASSOCID                        "associd"

#define NTP_ASSOC_STATUS_PEER_STATUS_WORD               "peer_status_word"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_REJECT        "reject"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_FALSETICK     "falsetick"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_EXCESS        "excess"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_OUTLIER       "outlier"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_PPSPEER       "pps_peer"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_CANDIDATE     "candidate"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_BACKUP        "backup"
#define NTP_ASSOC_STATUS_PEER_STATUS_WORD_SYSTEMPEER    "system_peer"

/********************** NTP Global config from System Table ***************************/
#define SYSTEM_NTP_CONFIG_AUTHENTICATION_ENABLE         "authentication_enable"
#define SYSTEM_NTP_CONFIG_AUTHENTICATION_ENABLED        "enabled"
#define SYSTEM_NTP_CONFIG_AUTHENTICATION_DISABLED       "disabled"
#define SYSTEM_NTP_CONFIG_AUTHENTICATION_DEFAULT        false

#define SYSTEM_NTP_STATS_PKTS_RCVD                      "ntp_pkts_received"
#define SYSTEM_NTP_STATS_PKTS_CUR_VER                   "ntp_pkts_with_current_version"
#define SYSTEM_NTP_STATS_PKTS_OLD_VER                   "ntp_pkts_with_older_version"
#define SYSTEM_NTP_STATS_PKTS_BAD_LEN_OR_FORMAT         "ntp_pkts_with_bad_length_or_format"
#define SYSTEM_NTP_STATS_PKTS_AUTH_FAILED               "ntp_pkts_with_auth_failed"
#define SYSTEM_NTP_STATS_PKTS_DECLINED                  "ntp_pkts_declined"
#define SYSTEM_NTP_STATS_PKTS_RESTRICTED                "ntp_pkts_restricted"
#define SYSTEM_NTP_STATS_PKTS_RATE_LIMITED              "ntp_pkts_rate_limited"
#define SYSTEM_NTP_STATS_PKTS_KOD_RESPONSES             "ntp_pkts_kod_responses"

#define SYSTEM_NTP_STATUS_UPTIME                        "uptime"

/************************************* NTP END ****************************************/

#endif /* OPENSWITCH_IDL_HEADER */
