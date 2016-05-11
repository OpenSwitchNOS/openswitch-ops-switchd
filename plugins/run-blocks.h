/* Copyright (C) 2016 Hewlett Packard Enterprise Development LP
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

#ifndef RUN_BLOCKS_H
#define RUN_BLOCKS_H

/* Run Blocks allows an external SwitchD plugin to register callback handlers
 * to be triggered in the bridge run event. This enables the external plugin to
 * be able to listen and make changes in the SwitchD main loop without having
 * to be compiled into SwitchD.
 *
 * Once a change in the switch configuration or other event that needs
 * processing is detected, the bridge_run() and bridge_wait() functions are
 * called from the main loop, and the source of the callbacks is indicated as
 * follows:
 *
 * - For each bridge run event
 * - <RUN ENTRY POINT BLK_RUN_COMPLETE>
 * - For each bridge wait event
 * - <RUN ENTRY POINT BLK_WAIT_COMPLETE>
 *
 * Run Blocks API
 *
 * register_run_callback: registers a plugin callback handler into a specified
 * block.  This function receives a priority level that is used to execute all
 * registered callbacks in a block in an ascending order (NO_PRIORITY can be
 * used when ordering is not important or needed).
 *
 * execute_run_block: executes all registered callbacks on the given block_id
 * with the given block parameters.
 */

enum run_block_id {
    BLK_INIT_RUN = 0,
    BLK_RUN_COMPLETE,
    BLK_WAIT_COMPLETE,
    /* Add more blocks here*/

    /* MAX_BLOCKS_NUM marks the end of the list of run blocks.
     * Do not add other run blocks ids after this. */
    MAX_RUN_BLOCKS_NUM,
};

/* The run callbacks will be provided with this structure that holds references
 * to ovsdb IDL and IDL sequence number required by external plugins to
 * properly process the events */
struct run_blk_params{
    struct ovsdb_idl *idl;   /* OVSDB IDL handler */
    unsigned int idl_seqno;  /* Current transaction's sequence number */
};

int execute_run_block(struct run_blk_params *params, enum run_block_id blk_id);
int register_run_callback(void (*callback_handler)(struct run_blk_params*),
                                  enum run_block_id blk_id, unsigned int priority);

#endif /* run-blocks.h */
