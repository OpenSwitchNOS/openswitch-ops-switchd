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

#ifndef LOGICAL_SWITCH_PLUGIN_H
#define LOGICAL_SWITCH_PLUGIN_H

#include "log-switch-asic-provider.h"
#include "reconfigure-blocks.h"
#include "vswitch-idl.h"

/** @def LOGICAL_SWITCH_PLUGIN_NAME
    @brief Plugin plugin_name version definition
*/
/* Do not change this name */
#define LOGICAL_SWITCH_PLUGIN_NAME     "logical_switch"

#define LOGICAL_SWITCH_PRIORITY        NO_PRIORITY

/** @def LOGICAL_SWITCH_PLUGIN_MAJOR
    @brief Plugin major version definition
*/
#define LOGICAL_SWITCH_PLUGIN_MAJOR    0

/** @def LOGICAL_SWITCH_PLUGIN_MINOR
    @brief Plugin logical_switch version definition
*/
#define LOGICAL_SWITCH_PLUGIN_MINOR    1

/* bridge_reconfigure callback functions (registered by logical_switchos_plugin:init) */
/*
 * log_switch_callback_bridge_init
 *
 * bridge_reconfigure BLK_BRIDGE_INIT callback handler
 *
 * @param blk_params     struct holding references to ovsdb IDL
 *                       and ofproto handler required by external plugins to
 *                       properly process the reconfigure events
 *
 * @return none
 */
void log_switch_callback_bridge_init(struct blk_params *blk_params);
/*
 * log_switch_callback_bridge_reconfig
 *
 * bridge_reconfigure BLK_BR_FEATURE_RECONFIG callback handler
 *
 * @param blk_params     struct holding references to ovsdb IDL
 *                       and ofproto handler required by external plugins to
 *                       properly process the reconfigure events
 *
 * @return none
 */
void log_switch_callback_bridge_reconfig(struct blk_params *blk_params);

/* Configuration of Logical Switch tables */
/*
 * ofproto_set_logical_switch
 *
 * sets (add/delete/update) Logical Switch parameters in an ofproto
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
int
ofproto_set_logical_switch(const struct ofproto *ofproto, void *aux,
                           enum logical_switch_action action,
                           struct logical_switch_node *log_switch);
/*
 * logical_switch_lookup_by_key
 *
 * Lookup for a specified tunnel key in the Logical Switch table
 *
 * @param hmap                     Logical Switch hash table.
 * @param br_name                  bridge name
 * @param key                      tunnel key
 *
 * @return struct logical_switch   pointer to Logical Switch structure with
 *                                 lookup result:
 *                                 NULL             failure
 *                                 valid pointer    success
 */
struct logical_switch *
logical_switch_lookup_by_key(const struct shash *hmap, const char *br_name, const int key);

#endif /* LOGICAL_SWITCH_PLUGIN_H */
