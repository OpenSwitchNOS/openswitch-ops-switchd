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

#define PORT_OTHER_CONFIG_MAP_LACP_TIME                 "lacp-time"

#define PORT_OTHER_CONFIG_LACP_TIME_SLOW                "slow"
#define PORT_OTHER_CONFIG_LACP_TIME_FAST                "fast"

#define PORT_OTHER_CONFIG_MAP_LACP_SYSTEM_PRIORITY      "lacp-system-priority"
#define PORT_OTHER_CONFIG_MAP_LACP_SYSTEM_ID            "lacp-system-id"

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

/************************* OPEN vSWITCH TABLE  ***************************/

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
#define SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT                 "true"

#endif /* OPENSWITCH_IDL_HEADER */
