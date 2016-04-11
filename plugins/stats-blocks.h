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
 * - At the start of the polling loop:
 * - <STATS ENTRY POINT STATS_BEGIN>
 * - For each bridge:
 * - <STATS ENTRY POINT STATS_PER_BRIDGE>
 * - For each VRF:
 * - <STATS ENTRY POINT STATS_PER_VRF>
 * - For each port in a given bridge or VRF
 * - <STATS ENTRY POINT STATS_PER_PORT>
 * - For each interface in a given bridge or VRF
 * - <STATS ENTRY POINT STATS_PER_IFACE>
 * - At the end of the polling loop:
 * - <STATS ENTRY POINT STATS_END>
 *
 */
enum stats_block_id {
    STATS_INIT = 0,
    STATS_BEGIN,
    STATS_PER_BRIDGE,
    STATS_PER_PORT,
    STATS_PER_IFACE,
    STATS_PER_VRF,
    STATS_END,
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
    struct iface *iface;      /* Reference to current iface. Only valid for
                                 blocks parsing iface instances */
};

/*
 * Stats Blocks API
 *
 * register_stats_callback: registers a plugin callback handler into a specified block.
 * This function receives a priority level that is used to execute all registered callbacks
 * in a block in an ascending order (NO_PRIORITY can be used when ordering is not important
 * or needed).
 *
 * execute_stats_block: executes all registered callbacks on the given
 * block_id with the given block parameters.
 */
int register_stats_callback(void (*callback_handler)(struct stats_blk_params *sblk),
                            enum stats_block_id blk_id, unsigned int priority);

/*
 * execute_stats_block: executes all registered callbacks on the given
 * block id with the additional "aux" parameter, whose underlying type
 * is based on block id:
 *
 *  STATS_INIT          pointer to IDL (struct ovsdb_idl *)
 *  STATS_BEGIN         NULL
 *  STATS_PER_BRIDGE    pointer to current bridge (struct bridge *)
 *  STATS_PER_PORT      pointer to current port (struct port *)
 *  STATS_PER_IFACE     pointer to current interface (struct iface *)
 *  STATS_PER_VRF       pointer to current VRF (struct vrf *)
 *  STATS_END           NULL
 */
int execute_stats_block(struct stats_blk_params *sblk, enum stats_block_id blk_id);

#endif /* STATS_BLOCKS_H */
