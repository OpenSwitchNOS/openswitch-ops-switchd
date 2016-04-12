/*
 * Copyright (c) 2016 Hewlett-Packard Enterprise Development, LP
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

#ifndef STATS_BLOCKS_H
#define STATS_BLOCKS_H

/* Stats Blocks allow an external SwitchD plugin to register callback handlers
 * to be triggered in the bridge statistics-gathering path. This enables the
 * external plugin to
 * be able to listen and make changes in the SwitchD main loop without having
 * to be compiled into SwitchD.
 *
 * Periodically, switchd polls for statistics gathering at these segments:
 *
 * In bridge.c:run_stats_update:
 * - At the start of the polling loop:
 * - <STATS ENTRY POINT STATS_BEGIN>
 * - For each bridge:
 * - <STATS ENTRY POINT STATS_PER_BRIDGE>
 * - For each VRF:
 * - <STATS ENTRY POINT STATS_PER_VRF>
 * - For each port in a given bridge
 * - <STATS ENTRY POINT STATS_PER_BRIDGE_PORT>
 * - For each port in a given VRF
 * - <STATS ENTRY POINT STATS_PER_VRF_PORT>
 * - For each netdev (interface) in a given bridge
 * - <STATS ENTRY POINT STATS_PER_BRIDGE_NETDEV>
 * - For each netdev (interface) in a given VRF
 * - <STATS ENTRY POINT STATS_PER_VRF_NETDEV>
 * - At the end of the polling loop:
 * - <STATS ENTRY POINT STATS_END>
 *
 * In subsystem.c:run_stats_update:
 * - At the start of the polling loop:
 * - <STATS ENTRY POINT STATS_SUBSYSTEM_BEGIN>
 * - For each subsystem:
 * - <STATS ENTRY POINT STATS_PER_SUBSYSTEM>
 * - For each netdev (interface) in a given subsystem
 * - <STATS ENTRY POINT STATS_PER_SUBSYSTEM_NETDEV>
 * - At the end of the polling loop:
 * - <STATS ENTRY POINT STATS_SUBSYSTEM_END>
 */
enum stats_block_id {
    STATS_BRIDGE_CREATE_NETDEV = 0,
    STATS_BEGIN,
    STATS_PER_BRIDGE,
    STATS_PER_BRIDGE_PORT,
    STATS_PER_BRIDGE_NETDEV,
    STATS_PER_VRF,
    STATS_PER_VRF_PORT,
    STATS_PER_VRF_NETDEV,
    STATS_END,
    STATS_SUBSYSTEM_CREATE_NETDEV,
    STATS_SUBSYSTEM_BEGIN,
    STATS_PER_SUBSYSTEM,
    STATS_PER_SUBSYSTEM_NETDEV,
    STATS_SUBSYSTEM_END,
    /* Add more blocks here*/

    /* MAX_STATS_BLOCKS_NUM marks the end of the list of stats blocks.
     * Do not add other stats blocks ids after this. */
    MAX_STATS_BLOCKS_NUM,
};

struct stats_blk_params {
    unsigned int idl_seqno;   /* Current transaction's sequence number */
    struct ovsdb_idl *idl;    /* OVSDB IDL */
    struct bridge *br;        /* Reference to current bridge. Only valid for
                                 blocks parsing bridge instances */
    struct vrf *vrf;          /* Reference to current vrf. Only valid for
                                 blocks parsing vrf instances */
    struct port *port;        /* Reference to current port. Only valid for
                                 blocks parsing port instances */
    struct netdev *netdev;    /* Reference to current iface's netdev. Only valid for
                                 blocks parsing iface instances */
    const struct ovsrec_interface *cfg; /* Reference to current iface's OVSDB record.
                                           Only valid for blocks parsing iface instances */
};

/*
 * Stats Blocks API
 *
 * register_stats_callback: registers a plugin callback handler into a specified block.
 * This function receives a priority level that is used to execute all registered callbacks
 * in a block in an ascending order (STATS_NO_PRIORITY can be used when ordering is not
 * important or needed).
 *
 * execute_stats_block: executes all registered callbacks on the given
 * block_id with the given block parameters.
 */
#define STATS_NO_PRIORITY  UINT_MAX

int register_stats_callback(void (*callback_handler)(struct stats_blk_params *sblk,
                                                     enum stats_block_id blk_id),
                            enum stats_block_id blk_id, unsigned int priority);

/*
 * execute_stats_block: executes all registered callbacks on the given
 * block id with the additional "sblk" parameter, that contains various
 * pointers, based on block id:
 *
 * from bridge.c:
 *  STATS_BEGIN             IDL, idl_seqno
 *  STATS_PER_BRIDGE        current bridge (struct bridge *), idl_seqno, IDL
 *  STATS_PER_BRIDGE_PORT   current port (struct port *), bridge, idl_seqno, IDL
 *  STATS_PER_BRIDGE_NETDEV current interface's underlying netdev (struct netdev *),
 *                              port, bridge, idl_seqno, IDL
 *  STATS_PER_VRF           current VRF (struct vrf *), idl_seqno, IDL
 *  STATS_PER_VRF_PORT      current port (struct port *), vrf, idl_seqno, IDL
 *  STATS_PER_VRF_NETDEV    current interface's underlying netdev (struct netdev *),
 *                              port, vrf, idl_seqno, IDL
 *  STATS_END               IDL, idl_seqno
 *  STATS_CREATE_NETDEV     IDL, idl_seqno, netdev
 *
 * from subsystem.c:
 *  STATS_SUBSYSTEM_BEGIN           IDL, idl_seqno
 *  STATS_PER_SUBSYSTEM             IDL, idl_seqno
 *  STATS_PER_SUBSYSTEM_NETDEV      IDL, idl_seqno, netdev
 *  STATS_SUBSYSTEM_END             IDL, idl_seqno
 *  STATS_SUBSYSTEM_CREATE_NETDEV   IDL, idl_seqno, netdev
 */
int execute_stats_block(struct stats_blk_params *sblk, enum stats_block_id blk_id);

#endif /* STATS_BLOCKS_H */
