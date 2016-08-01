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

#include <config.h>

#include "logical_switch_plugin.h"

#include <errno.h>

#include "openvswitch/vlog.h"
#include "plugin-extensions.h"
#include "logical-switch.h"
#include "asic-plugin.h"

VLOG_DEFINE_THIS_MODULE(logical_switch_plugin);

static struct asic_plugin_interface *plugin = NULL;

/** @fn int init(int phase_id)
    @brief Initialization of the plugin.
    @param[in] phase_id Indicates the number of times a plugin has been initialized.
    @param[out] ret Check if the plugin was correctly registered.
    @return 0 if success, errno value otherwise.
*/
int init(int phase_id)
{
    int ret = 0;
    static struct plugin_extension_interface *extension = NULL;

    /**
     * Initialize the Logical Switch API -- it will find its ASIC provider APIs.
     *
     * Must run after ASIC provider plugin initializes
     * Plugin load order is configured in plugins.yaml file
     * in ops-hw-config platform-dependent directory.
     */
    ret = find_plugin_extension(ASIC_PLUGIN_INTERFACE_NAME,
                                ASIC_PLUGIN_INTERFACE_MAJOR,
                                ASIC_PLUGIN_INTERFACE_MINOR,
                                &extension);
    if (ret == 0) {
        VLOG_INFO("Found [%s] asic plugin extension...", ASIC_PLUGIN_INTERFACE_NAME);
        plugin = (struct asic_plugin_interface *)extension->plugin_interface;
    }
    else {
        VLOG_WARN("%s (v%d.%d) not found", ASIC_PLUGIN_INTERFACE_NAME,
                  ASIC_PLUGIN_INTERFACE_MAJOR,
                  ASIC_PLUGIN_INTERFACE_MINOR);
    }

    VLOG_DBG("[%s] Registering BLK_BRIDGE_INIT", LOGICAL_SWITCH_PLUGIN_NAME);
    register_reconfigure_callback(&log_switch_callback_bridge_init,
                                  BLK_BRIDGE_INIT, LOGICAL_SWITCH_PRIORITY);

    VLOG_DBG("[%s] Registering BLK_BR_FEATURE_RECONFIG", LOGICAL_SWITCH_PLUGIN_NAME);
    register_reconfigure_callback(&log_switch_callback_bridge_reconfig,
                                  BLK_BR_FEATURE_RECONFIG, LOGICAL_SWITCH_PRIORITY);

    return ret;
}

/** @fn int run()
    @brief Run function plugin
    @return ret if success, errno value otherwise
*/
int run(void)
{
    return 0;
}

/** @fn int wait()
    @brief Wait function plugin
    @return ret if success, errno value otherwise
*/
int wait(void)
{
    return 0;
}

/** @fn int destroy()
    @brief Destroy function plugin
    @return ret if success, errno value otherwise
*/
int destroy(void)
{
    //unregister_plugin_extension(LOGICAL_SWITCH_PLUGIN_NAME);
    //VLOG_DBG("[%s] was destroyed...", LOGICAL_SWITCH_PLUGIN_NAME);
    return 0;
}

/* Plugin functions
 *
 * Here you can add the definition of your functions previously defined in
 * plugin header. This functions can be used as extensions or be registered in
 * bridge.c blocks executions.
 *
 * For example, you can define the registered function here.
 *
 * void logical-switch_foo(struct blk_params *params)
 * {
 *
 * }
 *
 */
/**
 * bridge_reconfigure BLK_BRIDGE_INIT callback handler
 */
void log_switch_callback_bridge_init(struct blk_params *blk_params)
{
    /* Enable writes into various Logical Switch columns. */
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_tunnel_key);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_bridge);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_from);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_description);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_name);
}

/**
 * add Logical Switch
 */
static void
logical_switch_create(struct bridge *br,
        const struct ovsrec_logical_switch *logical_switch_cfg)
{
    struct logical_switch *new_logical_switch = NULL;
    struct logical_switch_node ofp_log_switch;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if(!br || !logical_switch_cfg) {
        return;
    }

    /* Allocate structure to save state information for this logical_switch. */
    new_logical_switch = xzalloc(sizeof(struct logical_switch));

    /* The hash should really be bridge.name+type+tunnel_key */
    logical_switch_hash(hash_str, sizeof(hash_str), br->name,
                        logical_switch_cfg->tunnel_key);
    /* No need to check for uniqueness because
     * that's done before we call this function */
    hmap_insert(&br->logical_switches, &new_logical_switch->node,
                hash_string(hash_str, 0));

    new_logical_switch->br = br;
    new_logical_switch->cfg = logical_switch_cfg;
    new_logical_switch->tunnel_key = (int)logical_switch_cfg->tunnel_key;
    new_logical_switch->name = xstrdup(logical_switch_cfg->name);
    new_logical_switch->description = xstrdup(logical_switch_cfg->description);

    ofp_log_switch.name = logical_switch_cfg->name;
    ofp_log_switch.tunnel_key = (int)logical_switch_cfg->tunnel_key;
    ofp_log_switch.name = logical_switch_cfg->name;
    ofp_log_switch.description = logical_switch_cfg->description;

    ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_ADD,
            &ofp_log_switch);
}

/**
 * delete Logical Switch
 */
static void
logical_switch_delete(struct logical_switch *logical_switch)
{
    struct logical_switch_node ofp_log_switch;
    struct bridge *br;

    if (!logical_switch) {
        return;
    }

    ofp_log_switch.name = logical_switch->name;
    ofp_log_switch.description = logical_switch->description;
    ofp_log_switch.tunnel_key = logical_switch->tunnel_key;

    br = logical_switch->br;

    ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_DEL,
            &ofp_log_switch);

    hmap_remove(&br->logical_switches, &logical_switch->node);
    free(logical_switch->name);
    free(logical_switch->description);
    free(logical_switch);
}

/**
 * update Logical Switch
 */
static void
logical_switch_update(struct logical_switch *cur_logical_switch,
        const struct ovsrec_logical_switch *logical_switch)
{
    struct logical_switch_node ofp_log_switch;
    struct bridge *br;

    if (!cur_logical_switch || !logical_switch) {
        return;
    }

    br = cur_logical_switch->br;

    /* The tunnel key should not change. If it does, it will be seen
     * as a new logical switch */
    if((0 != strcmp(cur_logical_switch->description, logical_switch->description)) ||
       (0 != strcmp(cur_logical_switch->name, logical_switch->name)))
    {
        VLOG_DBG("Found a modified logical switch: "
                 "name=%s key=%ld description=%s",
                 logical_switch->name, logical_switch->tunnel_key,
                 logical_switch->description);

        cur_logical_switch->description = xstrdup(logical_switch->description);
        cur_logical_switch->name = xstrdup(logical_switch->name);

        ofp_log_switch.name = logical_switch->name;
        ofp_log_switch.description = logical_switch->description;
        ofp_log_switch.tunnel_key = logical_switch->tunnel_key;
        ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_MOD,
            &ofp_log_switch);
    }

}

/**
 * bridge_reconfigure BLK_BR_FEATURE_RECONFIG callback
 *
 * called after everything for a bridge has been add/deleted/updated
 */
void
log_switch_callback_bridge_reconfig(struct blk_params *blk_params)
{
    struct logical_switch *logical_switch, *next, *found;
    struct shash current_idl_logical_switches;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if(!blk_params->br) {
        return;
    }

    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (!logical_switch_row) {
        VLOG_DBG("No rows in Logical Switch table, delete all in local hash");

        /* Maybe all the Logical Switches got deleted */
        HMAP_FOR_EACH_SAFE(logical_switch, next, node, &blk_params->br->logical_switches) {
            logical_switch_delete(logical_switch);
        }
        return;
    }

    if ((!OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_DELETED(logical_switch_row, blk_params->idl_seqno)) &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(logical_switch_row, blk_params->idl_seqno))) {
        VLOG_DBG("No modification in Logical Switch table");
        return;
    }

    /* Collect all the logical_switches present on this Bridge */
    shash_init(&current_idl_logical_switches);
    OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch_row, blk_params->idl) {
        if (strcmp(blk_params->br->cfg->name, logical_switch_row->bridge->name) == 0) {
            logical_switch_hash(hash_str, sizeof(hash_str), blk_params->br->name,
                                logical_switch_row->tunnel_key);
            if (!shash_add_once(&current_idl_logical_switches,
                    hash_str, logical_switch_row)) {
                VLOG_WARN("logical switch %s (key %ld) specified twice",
                          logical_switch_row->name, logical_switch_row->tunnel_key);
            }
        }
    }

    /* Delete old logical_switches. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_DELETED(logical_switch_row, blk_params->idl_seqno) ||
        OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        HMAP_FOR_EACH_SAFE (logical_switch, next, node, &blk_params->br->logical_switches) {
            found = logical_switch_lookup_by_key(
                &current_idl_logical_switches,
                blk_params->br->name, logical_switch->tunnel_key);
            if (!found) {
                VLOG_DBG("Found a deleted logical_switch %s (key %d)",
                         logical_switch->name, logical_switch->tunnel_key);
                /* Need to update ofproto now since this logical_switch
                 * won't be around for the "check for changes"
                 * loop below. */
                logical_switch_delete(logical_switch);
            }
        }
    }

    /* Add new logical_switches. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(logical_switch_row, blk_params->idl_seqno) ||
        OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch_row, blk_params->idl) {
            found = logical_switch_lookup_by_key(&current_idl_logical_switches,
                                                 blk_params->br->name,
                                                 logical_switch_row->tunnel_key);
            if (!found) {
                VLOG_DBG("Found an added logical_switch %s %ld",
                    logical_switch_row->name, logical_switch_row->tunnel_key);
                logical_switch_create(blk_params->br, logical_switch_row);
            }
        }
    }

    /* Check for changes in the logical_switch row entries. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        HMAP_FOR_EACH (logical_switch, node, &blk_params->br->logical_switches) {
            const struct ovsrec_logical_switch *row = logical_switch->cfg;

            /* Check for changes to row. */
            if (!OVSREC_IDL_IS_ROW_INSERTED(row, blk_params->idl_seqno) &&
                 OVSREC_IDL_IS_ROW_MODIFIED(row, blk_params->idl_seqno)) {
                    logical_switch_update(logical_switch, row);
            }
       }
    }

    /* Destroy the shash of the IDL logical_switches */
    shash_destroy(&current_idl_logical_switches);
}

/* sets (add/delete/update) Logical Switch parameters in an ofproto. */
int
ofproto_set_logical_switch(const struct ofproto *ofproto, void *aux,
                           enum logical_switch_action action,
                           struct logical_switch_node *log_switch)
{
    int rc;

    if (plugin == NULL) {
        return EOPNOTSUPP;
    }

    rc = plugin->set_logical_switch ?
         plugin->set_logical_switch(ofproto, aux, action, log_switch) :
         EOPNOTSUPP;

    VLOG_DBG("%s rc (%d) op(%d) name (%s) key (%d)",
             __func__, rc, action, log_switch->name, log_switch->tunnel_key);
    return rc;
}

struct logical_switch *
logical_switch_lookup_by_key(const struct shash *hmap, const char *br_name, const int key)
{
    struct logical_switch *logical_switch = NULL;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if((NULL != hmap) && (NULL != br_name)) {
        logical_switch_hash(hash_str, sizeof(hash_str), br_name, key);
        logical_switch = shash_find_data(hmap, hash_str);
    }

    return logical_switch;
}
