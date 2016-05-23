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

#include <stdlib.h>
#include <errno.h>
#include "run-blocks.h"
#include "openvswitch/vlog.h"
#include "list.h"
#include "vswitch-idl.h"

VLOG_DEFINE_THIS_MODULE(run_blocks);

/* Node for a registered callback handler in a run block list */
struct run_blk_list_node{
    void (*callback_handler)(struct run_blk_params*);
    unsigned int priority;
    struct ovs_list node;
};

static bool blocks_init = false;
static struct ovs_list** blk_list = NULL;

static int init_r_blocks(void);
static int insert_node_on_blk(struct run_blk_list_node *new_node,
                               struct ovs_list *func_list);

/* Register a callback function for the given run block with a given priority.
 * Callbacks are executed in ascending order of priority 0 for maximum priority and
 * NO_PRIORITY for minimum priority
 */
int
register_run_callback(void (*callback_handler)(struct run_blk_params*),
                           enum run_block_id blk_id, unsigned int priority)
{
    struct run_blk_list_node *new_node;

    /* Initialize run lists */
    if (!blocks_init) {
        if(init_r_blocks()) {
            VLOG_ERR("Cannot initialize blocks");
            goto error;
        }
    }

    if (callback_handler == NULL) {
        VLOG_ERR("NULL callback function");
        goto error;
    }

    if ((blk_id < 0) || (blk_id >= MAX_RUN_BLOCKS_NUM)) {
        VLOG_ERR("Invalid blk_id passed as parameter");
        goto error;
    }

    VLOG_INFO("Registering callback for blk_id %d", blk_id);
    new_node = (struct run_blk_list_node *) xmalloc (sizeof(struct run_blk_list_node));
    new_node->callback_handler = callback_handler;
    new_node->priority = priority;
    if (insert_node_on_blk(new_node, blk_list[blk_id])) {
        VLOG_ERR("Failed to add node in block");
        free(new_node);
        goto error;
    }
    return 0;

error:
    return EINVAL;
}

/* Insert a new block list node in the given run block list. Node is
 * ordered by priority
 */
static int
insert_node_on_blk(struct run_blk_list_node *new_node, struct ovs_list *func_list)
{
    struct run_blk_list_node *blk_node;
    struct ovs_list *last_node;

    if (!func_list){
        VLOG_ERR("Invalid list passed as parameter");
        goto error;
    }

    /* If list is empty, insert node at the top */
    if (list_is_empty(func_list)) {
        list_push_back(func_list, &new_node->node);
        return 0;
    }

    /* If priority is higher than bottom element, insert node at the bottom */
    last_node = list_back(func_list);
    if (!last_node) {
        VLOG_ERR("Cannot get bottom element of list");
        goto error;
    }
    blk_node = CONTAINER_OF(last_node, struct run_blk_list_node, node);
    if ((new_node->priority) >= (blk_node->priority)) {
        list_push_back(func_list, &new_node->node);
        return 0;
    }

    /* Walk the list and insert node in between nodes */
    LIST_FOR_EACH(blk_node, node, func_list) {
        if ((blk_node->priority) > (new_node->priority)) {
            list_insert(&blk_node->node, &new_node->node);
            return 0;
        }
    }

 error:
    return EINVAL;
}

/* Initialize the list of blocks */
static int
init_r_blocks(void)
{
    int blk_counter;
    blk_list = (struct ovs_list**) xcalloc (MAX_RUN_BLOCKS_NUM,
                                            sizeof(struct ovs_list*));

    /* Initialize each of the Blocks */
    for (blk_counter = 0; blk_counter < MAX_RUN_BLOCKS_NUM; blk_counter++) {
        blk_list[blk_counter] = (struct ovs_list *) xmalloc (sizeof(struct ovs_list));
        list_init(blk_list[blk_counter]);
    }

    blocks_init = true;
    return 0;
}

/* Execute all registered callbacks for a given Run Block ordered by
 * priority
 */
int
execute_run_block(struct run_blk_params *params, enum run_block_id blk_id)
{
    struct run_blk_list_node *actual_node;

    /* Initialize run lists */
    if (!blocks_init) {
        if(init_r_blocks()) {
            VLOG_ERR("Cannot initialize blocks");
            goto error;
        }
    }

    if (!params) {
        VLOG_ERR("Invalid NULL params structure");
        goto error;
    }

    if ((blk_id < 0) || (blk_id >= MAX_RUN_BLOCKS_NUM)) {
        VLOG_ERR("Invalid blk_id passed as parameter");
        goto error;
    }

    LIST_FOR_EACH(actual_node, node, blk_list[blk_id]) {
        if (!actual_node->callback_handler) {
            VLOG_ERR("Invalid function callback_handler found");
            goto error;
        }
        actual_node->callback_handler(params);
    }

    return 0;

 error:
    return EINVAL;
}
