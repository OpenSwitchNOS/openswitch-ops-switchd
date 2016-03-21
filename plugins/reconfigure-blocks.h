/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2014 Nicira, Inc.
 * Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
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
 */

#ifndef RECONFIGURE_BLOCKS_H
#define RECONFIGURE_BLOCKS_H 1

/* Reconfigure Blocks allows an external SwitchD plugin to register callback
 * handlers to be triggered at several different points in the reconfigure
 * bridge event. This enables the external plugin to be able to listen and make
 * changes at different points in the bridge reconfigure logic.
 *
 * Once a change in the switch configuration is detected (by a change in the
 * OVSDB sequence number), the Bridge reconfigure function can be broken down in
 * the following segments:
 *
 * - Update Bridge and VRF ofproto data structures, nothing is pushed down the ofproto layer
 * - <RECONFIGURE ENTRY POINT BLK_INIT_RECONFIGURE>
 * - For each bridge delete ports
 * - <RECONFIGURE ENTRY POINT BLK_BR_DELETE_PORTS>
 * - For each Vrf delete ports
 * - <RECONFIGURE ENTRY POINT BLK_VRF_DELETE_PORTS>
 * - Applies delete changes to ofproto layer
 * - For each bridge delete or reconfigure ports
 * - <RECONFIGURE ENTRY POINT BLK_BR_RECONFIGURE_PORTS>
 * - For each vrf delete or reconfigure ports
 * - <RECONFIGURE ENTRY POINT BLK_VRF_RECONFIGURE_PORTS>
 * - Create and push new bridge and vrf ofproto objects to ofproto layer
 * - For each bridge add new ports
 * - <RECONFIGURE ENTRY POINT BLK_BR_ADD_PORTS>
 * - For each bridge add new ports
 * - <RECONFIGURE ENTRY POINT BLK_VRF_ADD_PORTS>
 * - Configure features like vlans, mac_table
 * - <RECONFIGURE ENTRY POINT BLK_BR_FEATURE_RECONFIG>
 * - For each configured port in a vrf add neighbors
 * - <RECONFIGURE ENTRY POINT BLK_VRF_ADD_NEIGHBORS>
 * - For each vrf reconfigure neighbors and reconfigure routes
 * - <RECONFIGURE ENTRY POINT BLK_VRF_RECONFIGURE_NEIGHBORS>
 *
 * The callback handler will receive a blk_params struct as a parameter which
 * holds references to the global bridge configuration, OVSDB IDL handle, current
 * bridge instance being process and current vrf intances being process.
 * Current Bridge and Vrf instance members are only valid in the blocks
 * they are called from.
 *
 * blk_params.br member is only valid in the blocks:
 *   BLK_BR_DELETE_PORTS, BLK_BR_RECONFIGURE_PORTS, BLK_BR_ADD_PORTS,
 *   BLK_BR_FEATURE_RECONFIG
 *
 * blk_params.vrf member is only valid in the blocks:
 *   BLK_VRF_DELETE_PORTS, BLK_VRF_RECONFIGURE_PORTS, BLK_VRF_ADD_PORTS,
 *   BLK_VRF_ADD_NEIGHBORS, BLK_VRF_RECONFIGURE_NEIGHBORS
 *
 *
 * Reconfigure Blocks API
 *
 * register_reconfigure_callback: registers a plugin callback handler into a specified block.
 * This function receives a priority level that is used to execute all registered callbacks
 * in a block in an ascending order (NO_PRIORITY can be used when ordering is not important
 * or needed).
 *
 * execute_reconfigure_block: executes all registered callbacks on the given
 * block_id with the given block parameters.
 */


#define NO_PRIORITY  UINT_MAX

enum block_id {
    BLK_INIT_RECONFIGURE = 0,
    BLK_BR_DELETE_PORTS,
    BLK_VRF_DELETE_PORTS,
    BLK_BR_RECONFIGURE_PORTS,
    BLK_VRF_RECONFIGURE_PORTS,
    BLK_BR_ADD_PORTS,
    BLK_VRF_ADD_PORTS,
    BLK_BR_FEATURE_RECONFIG,
    BLK_VRF_ADD_NEIGHBORS,
    BLK_RECONFIGURE_NEIGHBORS,
    /* Add more blocks here*/

    /* MAX_BLOCKS_NUM marks the end of the list of reconfigure blocks.
     * Do not add other reconfigure blocks ids after this. */
    MAX_BLOCKS_NUM,
};

/* The reconfigure callbacks will be provided with this structure that holds
 * references to ovsdb IDL and ofproto handler required by external plugins to
 * properly process the reconfigure events */
struct blk_params{
    struct ovsdb_idl *idl;   /* OVSDB IDL handler */
    struct ofproto *ofproto; /* Ofproto handler */
};

int execute_reconfigure_block(struct blk_params *params, enum block_id blk_id);
int register_reconfigure_callback(void (*callback_handler)(struct blk_params*),
                                  enum block_id blk_id, unsigned int priority);

#endif /* reconfigure-blocks.h */
