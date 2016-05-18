/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#ifndef LSWITCH_ASIC_PROVIDER_H
#define LSWITCH_ASIC_PROVIDER_H 1

#include "ofproto/ofproto.h"
#include "bridge.h"
#include "hmap.h"

#ifdef  __cplusplus
extern "C" {
#endif


/** @def LSWITCH_ASIC_PLUGIN_INTERFACE_NAME
 *  @brief asic plugin name definition
 */
#define LSWITCH_ASIC_PLUGIN_INTERFACE_NAME     "LSWITCH_ASIC_PLUGIN"

/** @def LSWITCH_ASIC_PLUGIN_INTERFACE_MAJOR
 *  @brief plugin major version definition
 */
#define LSWITCH_ASIC_PLUGIN_INTERFACE_MAJOR    1

/** @def LSWITCH_ASIC_PLUGIN_INTERFACE_MINOR
 *  @brief plugin minor version definition
 */
#define LSWITCH_ASIC_PLUGIN_INTERFACE_MINOR    1

enum logical_switch_action {
    LSWITCH_ACTION_UNDEF,   /* undefined action */
    LSWITCH_ACTION_ADD,     /* add logical switch */
    LSWITCH_ACTION_DEL,     /* delete logical switch */
    LSWITCH_ACTION_MOD      /* modify logical switch */
};

enum logical_switch_type {
    LSWITCH_TYPE_UNDEF,     /* undefined type */
    LSWITCH_TYPE_VXLAN,     /* Vxlan type logical switch */
};

struct logical_switch_node {
    char *name;                    /* Logical Switch Name */
    char *description;             /* Description of the Switch */
    unsigned int  tunnel_key;      /* Key used for overlay tunnels */
    enum logical_switch_type type; /* logical switch type */
};

struct logical_switch {
    struct bridge *br;
    struct hmap_node node;                   /* In 'all_logical_switches'. */
    const struct ovsrec_logical_switch *cfg;
    char *name;
    char *description;
    unsigned int tunnel_key;
};

/** @struct log_switch_asic_plugin_interface
 *  @brief log_switch_asic_plugin_interface enforces the interface that an LSWITCH_ASIC plugin must
 *  provide to be compatible with SwitchD Asic plugin infrastructure.
 *  When an external plugin attempts to register itself as an LSWITCH_ASIC plugin, the
 *  code will validate that the interface provided meets the requirements for
 *  MAJOR and MINOR versions.
 *
 *  - The LSWITCH_ASIC_PLUGIN_INTERFACE_NAME identifies the registered interface as an
 *  LSWITCH_ASIC plugin. All asic plugins must use the same interface name. The plugin
 *  infrastructure will enforce that only one asic plugin can be registered at a
 *  time. Asic plugins from vendors will have different names but they will
 *  register the same interface name.
 *
 *  - The LSWITCH_ASIC_PLUGIN_INTERFACE_MAJOR identifies any large change in the fields
 *  of struct log_switch_asic_plugin_interface that would break the ABI, so any extra
 *  fields added in the middle of previous fields, removal of previous fields
 *  would trigger a change in the MAJOR number.
 *
 *  - The LSWITCH_ASIC_PLUGIN_INTERFACE_MINOR indentifies any incremental changes to the
 *  fields of struct log_switch_asic_plugin_interface that would not break the ABI but
 *  would just make the new fields unavailable to the older component.
 *
 *  For example if LSWITCH_ASIC_PLUGIN_INTERFACE_MAJOR is 1 and
 *  LSWITCH_ASIC_PLUGIN_INTERFACE_MINOR is 2, then a plugin can register itself as an
 *  asic plugin if the provided interface has a MAJOR=1 and MINOR>=2. This means
 *  that even if the plugin provides more functionality in the interface fields
 *  those would not be used by SwitchD. But if the plugin has a MAJOR=1 and
 *  MINOR=1 then it cannot be used as an asic plugin as SwitchD will see fields
 *  in the interface struct that are not provided by the plugin.
 *
 */

struct log_switch_asic_plugin_interface {

    /*
     * set_logical_switch
     *
     * configure (add/delete/modify) Logical Switch settings per-bridge
     *
     * @param ofproto     struct ofproto that describes either a bridge or a VRF.
     * @param aux         pointer to struct port that is used to look up a
     *                    previously-added bundle
     * @param action      add/delete/modify action.
     * @param log_switch  pointer to logical_switch_node, describes how the logical switch
     *                    should be configured.
     *
     * @return int        API status:
     *                    0               success
     *                    EOPNOTSUPP      this API not supported by this provider
     *                    other value     ASIC provider dependent error
     */
    int (*set_logical_switch)(const struct ofproto *ofproto, void *aux,
                              enum logical_switch_action action,
                              const struct logical_switch_node *log_switch);
};


#ifdef  __cplusplus
}
#endif

#endif /* LSWITCH_ASIC_PROVIDER_H */
