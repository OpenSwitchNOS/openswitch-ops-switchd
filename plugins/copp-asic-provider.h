/*
 * Copyright (c) 2016 Hewlett Packard Enterprise Development LP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * Control Plane Policing (COPP) SwitchD ASIC Provider API
 *
 * Declares the functions and data structures that are used between the
 * SwitchD COPP feature and ASIC-specific providers.
 */

#ifndef COPP_ASIC_PROVIDER_H
#define COPP_ASIC_PROVIDER_H 1

#include "smap.h"

#ifdef  __cplusplus
extern "C" {
#endif

/** @def COPP_ASIC_PLUGIN_INTERFACE_NAME
 *  @brief asic plugin name definition
 */
#define COPP_ASIC_PLUGIN_INTERFACE_NAME     "COPP_ASIC_PLUGIN"

/** @def COPP_ASIC_PLUGIN_INTERFACE_MAJOR
 *  @brief plugin major version definition
 */
#define COPP_ASIC_PLUGIN_INTERFACE_MAJOR    1

/** @def COPP_ASIC_PLUGIN_INTERFACE_MINOR
 *  @brief plugin minor version definition
 */
#define COPP_ASIC_PLUGIN_INTERFACE_MINOR    1

/* structures */

/** @struct copp_asic_plugin_interface
 *
 * @brief copp_asic_plugin_interface enforces the interface that an COPP_ASIC
 * plugin must provide to be compatible with SwitchD Asic plugin
 * infrastructure.  When an external plugin attempts to register itself as an
 * COPP_ASIC plugin, the code will validate that the interface provided meets
 * the requirements for MAJOR and MINOR versions.
 *
 *  - The COPP_ASIC_PLUGIN_INTERFACE_NAME identifies the registered interface as
 *  an COPP_ASIC plugin. All asic plugins must use the same interface name. The
 *  plugin infrastructure will enforce that only one asic plugin can be
 *  registered at a time. Asic plugins from vendors will have different names
 *  but they will register the same interface name.
 *
 *  - The COPP_ASIC_PLUGIN_INTERFACE_MAJOR identifies any large change in the
 *  fields of struct copp_asic_plugin_interface that would break the ABI, so
 *  any extra fields added in the middle of previous fields, removal of
 *  previous fields would trigger a change in the MAJOR number.
 *
 *  - The COPP_ASIC_PLUGIN_INTERFACE_MINOR indentifies any incremental changes
 *  to the fields of struct copp_asic_plugin_interface that would not break the
 *  ABI but would just make the new fields unavailable to the older component.
 *
 *  For example if COPP_ASIC_PLUGIN_INTERFACE_MAJOR is 1 and
 *  COPP_ASIC_PLUGIN_INTERFACE_MINOR is 2, then a plugin can register itself as
 *  an asic plugin if the provided interface has a MAJOR=1 and MINOR>=2. This
 *  means that even if the plugin provides more functionality in the interface
 *  fields those would not be used by SwitchD. But if the plugin has a MAJOR=1
 *  and MINOR=1 then it cannot be used as an asic plugin as SwitchD will see
 *  fields in the interface struct that are not provided by the plugin.
 *
 */

/* */
enum copp_protocol_class {
    COPP_ACL_LOGGING,
    COPP_ARP_BROADCAST,
    COPP_ARP_MY_UNICAST,    /* Unicast MAC or broadcast w/ TPA=switch IP */
    COPP_ARP_SNOOP,         /* Unicast ARPs not to any switch MAC */
    COPP_BGP,
    COPP_DEFAULT_UNKNOWN,   /* Packets not matching any other class */
    COPP_DHCPv4,
    COPP_DHCPv6,
    COPP_ICMPv4_MULTIDEST,  /* Broadcast or multicast */
    COPP_ICMPv4_UNICAST,
    COPP_ICMPv6_MULTICAST,
    COPP_ICMPv6_UNICAST,
    COPP_LACP,
    COPP_LLDP,
    COPP_OSPFv2_MULTICAST,  /* All OSPF Router address, etc */
    COPP_OSPFv2_UNICAST,
    COPP_sFLOW_SAMPLES,     /* Packets sent to CPU to be sFlow encapsulated */
    COPP_STP_BPDU,
    COPP_BFD,
    COPP_UNKNOWN_IP_UNICAST,
    COPP_IPv4_OPTIONS,
    COPP_IPv6_OPTIONS,

    /* add new classes above this line */
    COPP_MAX                /* not used */
};

#define COPP_NUM_CLASSES COPP_MAX

/* Per COPP Protocol Class statistics
 *
 * Values of unsupported statistics are set to all-1-bits (UINT64_MAX) */
struct copp_protocol_stats {
    uint64_t  packets_passed;
    uint64_t  bytes_passed;
    uint64_t  packets_dropped;
    uint64_t  bytes_dropped;
};

/* Per COPP Protocol Class Hardware Status */
struct copp_hw_status {
    uint64_t  rate;            /* Units of packets-per-second */
    uint64_t  burst;           /* Units of packets */
    uint64_t  local_priority;
};

struct copp_asic_plugin_interface {

    /* Retrieves statistics for a COPP protocol class from the ASIC-specific
     * provider.  Caller will supply the buffer pre-filled with all-one bits.
     * Returns EOPNOTSUPP when the protocol class is unsupported or unknown.
     * Returns EINVAL for any other problem with the parameters. */
    int (*copp_stats_get)(const unsigned int hw_asic_id,
                          const enum copp_protocol_class class,
                          struct copp_protocol_stats *const stats);

    /* Retrieves the hardware status for a COPP protocol class from the
     * ASIC-specific provider.  Caller will provide a zero-filled buffer.
     * Returns EOPNOTSUPP when the protocol class is unsupported or unknown.
     * Returns ENOSPC when insuffient ASIC resources available for the class.
     * Returns EIO when any problem encountered programming the ASIC.
     * Returns EINVAL for any other problem with the parameters.
     */
    int (*copp_hw_status_get)(const unsigned int hw_asic_id,
                              const enum copp_protocol_class class,
                              struct copp_hw_status *const hw_status);
};

#ifdef  __cplusplus
}
#endif

#endif /* copp_asic_provider.h */
