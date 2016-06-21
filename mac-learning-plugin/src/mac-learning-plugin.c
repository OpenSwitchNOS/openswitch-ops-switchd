/* (c) Copyright 2016 Hewlett Packard Enterprise Development LP
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

#include <stdio.h>
#include <hash.h>
#include <ovsdb-idl.h>
#include <openvswitch/vlog.h>
#include <openvswitch/types.h>
#include <ovs-thread.h>
#include <openswitch-idl.h>
#include <vswitch-idl.h>
#include "plugins.h"
#include "seq.h"
#include "mac-learning-plugin.h"
#include "asic-plugin.h"
#include "reconfigure-blocks.h"
#include "plugin-extensions.h"
#include "bridge.h"
#include "unixctl.h"
#include "dynamic-string.h"
#include "hash.h"
#include "hmap.h"
#include "list.h"


VLOG_DEFINE_THIS_MODULE(mac_learning);

/* OVSDB IDL used to obtain configuration. */
struct ovsdb_idl *idl = NULL;

/* Forward references */
static void mac_learning_update_db(void);
static void mlearn_plugin_db_add_local_mac_entry (
                                  struct mlearn_hmap_node *mlearn_node,
                                  struct ovsdb_idl_txn *mac_txn);
static void mlearn_plugin_db_del_local_mac_entry (
                                  struct mlearn_hmap_node *mlearn_node);
static void mac_learning_table_monitor (struct blk_params *blk_params);
static void mac_learning_wait_seq (void);
static void mac_learning_reconfigure (void);

/* Global variables */
static uint64_t mlearn_seqno = LLONG_MIN;
static struct seq *mlearn_trigger_seq = NULL;

static struct asic_plugin_interface *p_asic_plugin_interface = NULL;

/* A flag to suggest if Product Driver Layer (PDL) has registered
 *      (via asic plugin's update_l2_mac_table API) to get notification
 *      about L2 MAC table updates?
 * For every change in MAC table schema from OVSDB, this flag determines
 *      if we create Updated MAC entries's list or not.
 */
static bool notify_pd_for_changes = false;

/* Updated MAC entries' list to be send down to PD, only if PD has registered
 *  API to get notification.
 * As we process each L2 MAC table update from OVSDB, we will add MAC entry
 *  details to this list along with action (create/update/delete) taken.
 * At the end of each L2 MAC table update from OVSDB, we reset this list.
 */
static struct ovs_list mac_list;

/* Function: test_update_l2_mac_table
 * Purpose:  Debug only function to verify correct information sent down to PD
 *              for L2 MAC table changes
 * In:       ofproto - pointer to ofproto - unused
 *           mac_entry_list - pointer to Updated MAC entries' list
 * Return:   0 if success, errno value otherwise
 * Notes:    For debug purposes, this function can be used as
 *              PD's registered API
 */
/* static int
test_update_l2_mac_table(const struct ofproto *ofproto,
                         struct ovs_list *mac_entry_list)
{
    struct l2_mac_tbl_update_entry *entry;

    if (! mac_entry_list) {
        return 1;
    }

    VLOG_DBG("%s Processing Updated MAC list with %d MAC entries.",
             __FUNCTION__, (uint32_t)list_size(mac_entry_list));

    // Process Updated MAC list
    LIST_FOR_EACH (entry, node, mac_entry_list) {
        VLOG_DBG("%s: Entry from list - mac:" ETH_ADDR_FMT
                 " vlan:%d port:%s action:%d\n",
                 __FUNCTION__, ETH_ADDR_ARGS(entry->mac), entry->vlan,
                 entry->port_name, entry->action);
    }
    return 0;
}
*/

/* Function: mac_hash_table_calc_hash
 * Purpose:  calculate hash value for PI MAC Hash table
 * In:       mac - mac address
 * In:       vlan - VLAN ID
 * Return:   32-bit hash value
 */
static uint32_t
mac_hash_table_calc_hash(const struct eth_addr mac, const uint16_t vlan)
{
    uint32_t hash = hash_uint64(eth_addr_vlan_to_uint64(mac, vlan));
    return (hash);
}

/* Function: mac_hash_table_lookup_by_mac_vlan
 * Purpose:  find hash table entry matching {mac+vlan} pair
 * In:       mac - mac address
 * In:       vlan - VLAN ID
 * Return:   pointer to hash table entry if entry found, NULL otherwise
 */
static struct mac_hash_table_entry*
mac_hash_table_lookup_by_mac_vlan(struct eth_addr mac, uint16_t vlan)
{
    uint32_t hash;
    struct mac_hash_table_entry *entry;

    hash = mac_hash_table_calc_hash(mac, vlan);

    HMAP_FOR_EACH_WITH_HASH(entry, mac_hash_table_node, hash, &mac_hash_table) {
        if ((vlan == entry->vlan) &&
            eth_addr_equals(mac, entry->mac)) {
            return entry;
        }
    }
    return NULL;
}

/* Function: mac_hash_table_lookup_by_row
 * Purpose:  find hash table entry matching OVSDB MAC table row
 * In:       ovsdb_row - pointer to OVSDB MAC table row
 * Return:   pointer to hash table entry if entry found, NULL otherwise
 */
static struct mac_hash_table_entry*
mac_hash_table_lookup_by_row(const struct ovsrec_mac *ovsdb_row)
{
    struct eth_addr mac;
    uint16_t vlan;

    if ((! ovsdb_row) || (! hmap_count(&mac_hash_table))) {
        return NULL;
    }

    if (! ovs_scan(ovsdb_row->mac_addr, ETH_ADDR_SCAN_FMT,
                   ETH_ADDR_SCAN_ARGS(mac))) {
        return NULL;
    }
    vlan = ovsdb_row->vlan;

    return (mac_hash_table_lookup_by_mac_vlan(mac, vlan));
}

/* Function: mac_hash_table_entry_dump
 * Purpose:  prints details for given hash table entry
 * In/Out:   ds - pointer to dynamic string where entry details are written
 * In:       entry - pointer to hash table entry
 * Return:   none
 * Notes:    function is called by mac_hash_table_unixctl_show() to
 *              dump a single entry in PI MAC hash table.
 */
static void
mac_hash_table_entry_dump(struct ds *ds, struct mac_hash_table_entry *entry)
{
    if ((! ds) || (! entry)) {
        return;
    }
    ds_put_format(ds, "mac:" ETH_ADDR_FMT " vlan:%-4d from:%-10s dest:%s\n",
                  ETH_ADDR_ARGS(entry->mac), entry->vlan,
                  entry->from, entry->dest);
}

/* Function: mac_hash_table_unixctl_show
 * Purpose:  parse cli arguments for "ovs-appctl mac/show" commands
 * In:       unixctl_conn - pointer to unixctl connection
 * In:       argc - argument count
 * In:       argv - arguments
 * In:       aux - unused
 * Return:   none
 * Notes:    This function is registered as handler function for unixctl
 *              command "mac/show". Command shows up under
 *              "ops-appctl list-commands".
 */
static void
mac_hash_table_unixctl_show(struct unixctl_conn *conn, int argc,
                            const char *argv[], void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct mac_hash_table_entry *entry;

    if ((!conn) || (!argv)) {
        VLOG_ERR("%s:unixctl command handler function called without "
                "setting up parameters", __FUNCTION__);
        return;
    }

    switch(argc) {
        case 1:
            /* show entire table */
            ds_put_format(&ds, "PI MAC hash table %u entries\n",
                          (uint32_t)hmap_count(&mac_hash_table));

            HMAP_FOR_EACH (entry, mac_hash_table_node, &mac_hash_table) {
                mac_hash_table_entry_dump(&ds, entry);
            }
            break;
        default:
            ds_put_format(&ds, "Usage: %s\n", argv[0]);
            break;
    }

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

/* Function: mac_entry_add_to_updated_list
 * Purpose:  add hash entry's details to Updated MAC entries's list,
 *              to be send down to PD later
 * In:       entry - pointer to hash table entry
 * In:       action - how this entry has changed? create, update or delete
 * Return:   none
 * Notes:    entries added to global list: mac_list
 */
static void
mac_entry_add_to_updated_list(const struct mac_hash_table_entry *entry,
                              mac_table_action action)
{
    struct l2_mac_tbl_update_entry *list_entry;

    if (! entry) {
        return;
    }

    list_entry = xmalloc(sizeof *list_entry);

    memcpy(&list_entry->mac, &entry->mac, sizeof(list_entry->mac));
    list_entry->vlan = entry->vlan;
    list_entry->action = action;
    strncpy(list_entry->port_name, entry->dest, sizeof(list_entry->port_name));

    /* Add it to the tail of the list */
    list_push_back(&mac_list, &list_entry->node);

    VLOG_DBG("%s: Added MAC entry to updated MAC list - mac:" ETH_ADDR_FMT
             " vlan:%d port:%s action:%d",
             __FUNCTION__, ETH_ADDR_ARGS(list_entry->mac),
             list_entry->vlan, list_entry->port_name, action);
}


/* Function: mac_entry_add
 * Purpose:  function to handle newly added OVSDB MAC table row.
 *           add this entry in PI MAC Hash table
 *           add this entry in Updated MAC entries' list to be send
 *              down to PD, if needed
 * In:       ovsdb_row - pointer to OVSDB MAC table row
 * In:       seqno - current IDL seqno
 * Return:   pointer to hash table entry if entry was added, NULL otherwise
 */
static struct mac_hash_table_entry*
mac_entry_add(const struct ovsrec_mac *ovsdb_row, unsigned int seqno)
{
    uint32_t hash;
    struct mac_hash_table_entry *hash_entry;
    char *port_name;

    if ((! ovsdb_row) || (! ovsdb_row->port)) {
        return NULL;
    }
    port_name = ovsdb_row->port->name;

    /* Add this entry in PI MAC Hash table */
    hash_entry = xmalloc(sizeof(*hash_entry));

    if (! ovs_scan(ovsdb_row->mac_addr, ETH_ADDR_SCAN_FMT,
                   ETH_ADDR_SCAN_ARGS(hash_entry->mac))) {

       VLOG_ERR("%s: OVSDB MAC table row has incorrect MAC address",
               __FUNCTION__);
       free(hash_entry);
       return NULL;
    }

    hash_entry->vlan = ovsdb_row->vlan;
    hash_entry->idl_seqno = seqno;
    strncpy(hash_entry->from, ovsdb_row->from, sizeof(hash_entry->from));
    strncpy(hash_entry->dest, port_name, sizeof(hash_entry->dest));

    hash = mac_hash_table_calc_hash(hash_entry->mac, hash_entry->vlan);
    hmap_insert(&mac_hash_table, &hash_entry->mac_hash_table_node, hash);

    VLOG_DBG("%s: Added MAC entry to MAC hash table - mac:"
             ETH_ADDR_FMT " vlan:%d from:%s dest:%s\n",
             __FUNCTION__, ETH_ADDR_ARGS(hash_entry->mac), hash_entry->vlan,
             hash_entry->from, hash_entry->dest);

    /* Add this entry in Updated MAC entries' list to be
     * send down to PD, if needed
     */
    if (notify_pd_for_changes) {
        mac_entry_add_to_updated_list(hash_entry, MAC_TBL_ADD);
    }

    return (hash_entry);
}

/* Function: mac_entry_check_for_conflicts
 * Purpose:  function to check any possible conflicts by updating current mac
 *              hash entry with new information from OVSDB
 * In:       ovsdb_row - pointer to OVSDB MAC table row
 * In:       entry - pointer to hash table entry
 * Return:   true if allowing this change from OVSDB would cause conflicts,
 *           false otherwise
 * Notes:    Need for conflict checks.
 *           OVSDB MAC table schema's index key:{bridge,mac,vlan,from}
 *           PI MAC table's index key: {mac,vlan}
 *           Since we only support single instance of bridge, not having
 *              bridge in PI table's key is OK.
 *           Ideally "from" should be removed from OVSDB MAC table schema's
 *              index and have public API available in ovs_util repo, that
 *              can be used by any other repo, to check for conflicts before
 *              making changes to OVSDB MAC table. After that, PI MAC table
 *              could simply be a reflection of the OVSDB table.
 *              Until we have that in place, each update from OVSDB needs
 *              to be checked for possible conflicts.
 *
 *           Do not allow MAC entry, learned from one source, and be updated
 *              to another source later.
 *           For example, entry added by local learn (dynamic) can NOT be
 *              overwritten by remote learn (hw-vtep).
 *          We are implementing FIFO, so who ever gets in first will remain
 *              in table until it ages out or been removed.
 */
static bool
mac_entry_check_for_conflicts(const struct ovsrec_mac *ovsdb_row,
                              struct mac_hash_table_entry *entry)
{
    if ((! ovsdb_row) || (! entry)) {
        return true;
    }

    if (strncmp(entry->from, ovsdb_row->from, sizeof(entry->from)) == 0) {
        return false;
    }

    return true;
}

/* Function: mac_entry_update
 * Purpose:  function to handle updated OVSDB MAC table row.
 *           update this entry in PI MAC Hash table
 *           add this entry in Updated MAC entries' list to be
 *              send down to PD, if needed
 * In:       ovsdb_row - pointer to OVSDB MAC table row
 * In:       seqno - current IDL seqno
 * In:       entry - pointer to hash table entry
 * Return:   none
 * Notes:    Updating this entry should NOT cause as conflicts, as
 *              all possible conflicts should have been checked for already
 *              before this function call.
 *           Note about fields that can be updated or not.
 *              {mac+vlan} fields are used for hash-key calculation.
 *              'from' field, source from where we learned about this host is
 *                  not allowed to be changed.
 *              Always update 'idl_seqno' field.
 *              Field that can also be updated is 'dest' meaning a host has
 *                  moved from one port to the other.
 */
static void
mac_entry_update(const struct ovsrec_mac *ovsdb_row, unsigned int seqno,
                 struct mac_hash_table_entry *entry)
{
        if ((! ovsdb_row) || (! ovsdb_row->port) || (! entry)) {
        return;
    }

    /* Update this entry in PI MAC Hash table */
    entry->idl_seqno = seqno;
    strncpy(entry->dest, ovsdb_row->port->name, sizeof(entry->dest));

    VLOG_DBG("%s: Updated MAC entry to MAC hash table - mac:" ETH_ADDR_FMT
             " vlan:%d from:%s dest:%s\n",
             __FUNCTION__, ETH_ADDR_ARGS(entry->mac), entry->vlan,
             entry->from, entry->dest);

    /* Add this entry in Updated MAC entries' list to be
     * send down to PD, if needed
     */
    if (notify_pd_for_changes) {
        mac_entry_add_to_updated_list(entry, MAC_TBL_UPDATE);
    }

    return;
}

/* Function: mac_entry_delete
 * Purpose:  function to handle deleted OVSDB MAC table row.
 *           add this entry in Updated MAC entries' list to be
 *              send down to PD, if needed
 *           delete this entry from PI MAC Hash table
 * In:       ovsdb_row - pointer to OVSDB MAC table row
 * In:       seqno - current IDL seqno
 * In:       entry - pointer to hash table entry
 * Return:   none
 */
static void
mac_entry_delete(const struct ovsrec_mac *ovsdb_row, unsigned int seqno,
                 struct mac_hash_table_entry *entry)
{
    if (! entry) {
        return;
    }

    /* Add this entry to Updated MAC entries' list to be
     * send down to PD, if needed
     */
    if (notify_pd_for_changes) {
        mac_entry_add_to_updated_list(entry, MAC_TBL_DELETE);
    }

    /* Remove this entry from PI MAC Hash table */
    hmap_remove(&mac_hash_table, &entry->mac_hash_table_node);

    VLOG_DBG("%s: Deleted MAC entry from MAC hash table - mac:" ETH_ADDR_FMT
             " vlan:%d from:%s dest:%s\n",
             __FUNCTION__, ETH_ADDR_ARGS(entry->mac), entry->vlan,
             entry->from, entry->dest);

    return;
}

/* Function: mac_learning_callback_bridge_reconfig
 * Purpose:  function to handle changes in L2 MAC table from OVSDB
 *              this function is from bridge_reconfigure()
 * In:       blk_params - need at least following elements: br, ofproto,
 *              idl, idl_seqno
 * Return:   none
 */
static void
mac_learning_callback_bridge_reconfig (struct blk_params *blk_params)
{
    /* Quick check for L2 MAC table changes */
    bool mac_entries_created;
    bool mac_entries_modified;
    bool mac_entries_deleted;

    bool have_mac_entries = !hmap_is_empty(&mac_hash_table);
    const struct ovsrec_mac *mac_entry_row = NULL;

    const struct ovsdb_idl *idl;
    unsigned int idl_seqno;

    unsigned int entries_changed = 0;

    if ((! blk_params) || (! blk_params->br) ||
        (! blk_params->ofproto) || (!blk_params->idl)) {

        VLOG_ERR("%s:feature plugin callback called without "
                "setting up parameters", __FUNCTION__);
        return;
    }

    idl = blk_params->idl;
    idl_seqno = blk_params->idl_seqno;

    mac_entry_row = ovsrec_mac_first(idl);

    VLOG_DBG("%s: Beginning to process L2 MAC table updates from "
             "OVSDB for idl-seq=%d, bridge=%s",
             __FUNCTION__, idl_seqno, blk_params->br->name);

    if (mac_entry_row) {
        mac_entries_created =
                OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(mac_entry_row, idl_seqno);
        mac_entries_modified =
                OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(mac_entry_row, idl_seqno);

        /* We only care about mac_entries_deleted if we already have some mac entries in PI MAC Hash table */
        mac_entries_deleted = have_mac_entries &&
                OVSREC_IDL_ANY_TABLE_ROWS_DELETED(mac_entry_row, idl_seqno);
    } else {
        /* There are no MAC table rows in OVSDB. */
        mac_entries_created = false;
        mac_entries_modified = false;
        mac_entries_deleted = have_mac_entries;
    }

    /* Check if we need to process any MAC entries */
    if (mac_entries_created || mac_entries_modified || mac_entries_deleted) {

        const struct ovsrec_mac *mac_entry_row_next;
        OVSREC_MAC_FOR_EACH_SAFE(mac_entry_row, mac_entry_row_next, idl) {

            struct mac_hash_table_entry *entry;

            /* Make sure entry belongs to this bridge */
            if (strcmp(mac_entry_row->bridge->name,
                       blk_params->br->name) != 0) {
                continue;
            }

            /* Check to see if this entry is present in PI MAC Hash table */
            entry = mac_hash_table_lookup_by_row(mac_entry_row);

            if (! entry) {

                /* Entry not found in local MAC table. Add this entry. */
                entry = mac_entry_add(mac_entry_row, idl_seqno);
                entries_changed++;

            } else {
                /* Entry found in local MAC table. */

                /* Check if this entry has changed since we last saw it. */
                bool row_changed =
                    (OVSREC_IDL_IS_ROW_MODIFIED(mac_entry_row, idl_seqno) ||
                     OVSREC_IDL_IS_ROW_INSERTED(mac_entry_row, idl_seqno));

                if (row_changed) {

                    /* Check for conflicts */
                    bool is_conflict;

                    is_conflict = mac_entry_check_for_conflicts(mac_entry_row,
                                                                entry);

                    if (! is_conflict) {

                        /* No conflicts, so update this entry local MAC table */
                        mac_entry_update(mac_entry_row, idl_seqno, entry);
                        entries_changed++;

                    } else {

                        /* TODO: We can possibly delete OVSDB row since it's
                         * causing a conflict.
                         */
                        VLOG_INFO("%s: MAC entry (mac:%s vlan:%d from:%s) from "
                                  "OVSDB rejected because of conflict.",
                                  __FUNCTION__, mac_entry_row->mac_addr,
                                  (int)mac_entry_row->vlan,
                                  mac_entry_row->from);

                    }

                } else if (strncmp(entry->from, mac_entry_row->from,
                                   sizeof(entry->from)) == 0) {

                    /* Update idl_seqno for matching entry. idl_seqno is use as
                     * mark/sweep to delete unused MAC entries. We always
                     * update these, even if OVSDB row's content has not
                     * changed. This is to find OVSDB rows that got completely
                     * got deleted.
                     */
                    entry->idl_seqno = idl_seqno;
                }
            }
        }
    } else {

        VLOG_DBG("%s: No changes in L2 MAC table from OVSDB for idl-seq=%d, "
                 "bridge=%s", __FUNCTION__, idl_seqno, blk_params->br->name);
        return;
    }

    /* Detect any deleted MAC entries by sweeping looking for old seqno. */
    if (mac_entries_deleted) {
        struct mac_hash_table_entry *entry, *next_entry;
        HMAP_FOR_EACH_SAFE (entry, next_entry, mac_hash_table_node,
                            &mac_hash_table) {
            if (entry->idl_seqno < idl_seqno) {
                mac_entry_delete(mac_entry_row, idl_seqno, entry);
                entries_changed++;
            }
        }
    }

    /* If PD has registered API to notify about MAC table changes, and
     * something has really changed, notify PD about all changes.
     */
    if (notify_pd_for_changes && entries_changed) {

        int ret;
        struct l2_mac_tbl_update_entry *list_entry;

        /* Notify PD about Updated MAC list */
        ret = p_asic_plugin_interface->update_l2_mac_table(blk_params->ofproto,
                                                           &mac_list);

        if (ret) {
            VLOG_WARN("%s: L2 MAC table update from PD failed with %d changed"
                     "MAC entries, idl-seq=%d",
                     __FUNCTION__, entries_changed, idl_seqno);
        }

        /* Destroy all entries from Updated MAC list */
        LIST_FOR_EACH_POP (list_entry, node, &mac_list) {
            free(list_entry);
        }

        /* Reset Updated MAC list */
        list_init(&mac_list);

    }
}

/* Function: mac_hash_table_init
 * Purpose:  function to initialize PI MAC hash table's related data structs
 * In:       none
 * Return:   none
 */
static void
mac_hash_table_init(void)
{
    /* Register unixctl command to dump PI MAC hash table */
    unixctl_command_register("mac/show", "", 0, 0,
                             mac_hash_table_unixctl_show, NULL);

    /* Check to see if PDL has registered API to get notification about
     * updates in L2 MAC table
     */
    if (p_asic_plugin_interface &&
        p_asic_plugin_interface->update_l2_mac_table) {
        notify_pd_for_changes = true;
        list_init(&mac_list);
    }

    VLOG_INFO("%s: asic_plugin=%p notify_pd=%d", __FUNCTION__,
            p_asic_plugin_interface, notify_pd_for_changes);
}

/*
 * Provides a global seq for mac learning trigger notifications.
 * mac learning module in plugin should call seq_change() on the returned
 * object whenever the event trigger notification from the callback is called.
 *
 * seq_wait() monitor on this object will get trigger notification changes to
 * collect the MAC learning notifications.
 */
struct seq *
mac_learning_trigger_seq_get(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        mlearn_trigger_seq = seq_create();
        ovsthread_once_done(&once);
    }

    return mlearn_trigger_seq;
}/* mac_learning_trigger_seq_get */

void
mac_learning_trigger_callback(void)
{
    seq_change(mac_learning_trigger_seq_get());
}

/*
 * mac_learning_plugin
 *
 * This is an object of struct mac_learning_plugin_interface
 * This is used to specify the API function pointers.
 */
static struct mac_learning_plugin_interface mac_learning_plugin = {
    .mac_learning_trigger_callback = &mac_learning_trigger_callback,
};

static struct plugin_extension_interface mac_learning_extension = {
    MAC_LEARNING_PLUGIN_INTERFACE_NAME,
    MAC_LEARNING_PLUGIN_INTERFACE_MAJOR,
    MAC_LEARNING_PLUGIN_INTERFACE_MINOR,
    (void *)&mac_learning_plugin
};

/*
 * This is the init function called from plugins_init.
 *
 * It has the capability of multiple phase initialization
 * but for mac learning it's not needed.
 */
void init (int phase_id)
{
    int ret = 0;
    struct plugin_extension_interface *p_extension = NULL;

    /* Initialize MAC learning ASIC plugin,
     * it will find its ASIC provider APIs.
     */
    ret = find_plugin_extension(ASIC_PLUGIN_INTERFACE_NAME,
                                ASIC_PLUGIN_INTERFACE_MAJOR,
                                ASIC_PLUGIN_INTERFACE_MINOR,
                                &p_extension);
    if (ret == 0) {
        VLOG_INFO("Found [%s] asic plugin extension.",
                  ASIC_PLUGIN_INTERFACE_NAME);
        p_asic_plugin_interface = p_extension->plugin_interface;
    } else {
        VLOG_WARN("%s (v%d.%d) not found", ASIC_PLUGIN_INTERFACE_NAME,
                                           ASIC_PLUGIN_INTERFACE_MAJOR,
                                           ASIC_PLUGIN_INTERFACE_MINOR);
    }

    VLOG_INFO("[%s] Registering switchd plugin - phase_id: %d",
              MAC_LEARNING_PLUGIN_INTERFACE_NAME, phase_id);
    register_plugin_extension(&mac_learning_extension);

    VLOG_INFO("[%s] Registering BLK_BRIDGE_INIT",
              MAC_LEARNING_PLUGIN_INTERFACE_NAME);
    register_reconfigure_callback(&mac_learning_table_monitor,
                                  BLK_BRIDGE_INIT,
                                  NO_PRIORITY);

    /*
     * call register_reconfigure_callback for port del (flush),
     * vlan delete (flush) ...
     */

    VLOG_INFO("[%s] Registering BLK_BR_FEATURE_RECONFIG",
              MAC_LEARNING_PLUGIN_INTERFACE_NAME);
    register_reconfigure_callback(&mac_learning_callback_bridge_reconfig,
                                  BLK_BR_FEATURE_RECONFIG,
                                  NO_PRIORITY);

    /* Initialize PI MAC Hash table */
    mac_hash_table_init();
}

/*
 * Function: mac_learning_table_monitor
 *
 * registers for monitoring, adding MAC table columns.
 *
 * Add code here for register_reconfigure_callback.
 */
static void mac_learning_table_monitor (struct blk_params *blk_params)
{
    /*
     * MAC table related
     */
    if (!blk_params || !blk_params->idl) {
        VLOG_ERR("%s: idl is not initialized in bridge_init", __FUNCTION__);
        return;
    }
    idl = blk_params->idl;
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_bridge);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_from);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_vlan);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_mac_addr);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_tunnel_key);
    ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_port);
} /* mac_learning_table_monitor */

/*
 * Function: mac_learning_reconfigure
 *
 * This function is called from plugins_run -> run
 *
 * This function checks if the sequence number is changed or not
 * If yes, it changes the MAC table for the new update.
 */
static void mac_learning_reconfigure (void)
{
    uint64_t seq = seq_read(mac_learning_trigger_seq_get());
    if (seq != mlearn_seqno) {
        mlearn_seqno = seq;
        mac_learning_update_db();
    }
}

/*
 * Function: mlearn_plugin_db_add_local_mac_entry
 *
 * This function takes the hmap node and inserts the corresponding entry
 * of MAC table in OVSDB.
 */
static void
mlearn_plugin_db_add_local_mac_entry (
                                  struct mlearn_hmap_node *mlearn_node,
                                  struct ovsdb_idl_txn *mac_txn)
{
    const struct ovsrec_mac *mac_e = NULL;
    struct bridge *br = NULL;
    struct port *port = NULL;
    char str[18];

    br = get_bridge_from_port_name(mlearn_node->port_name, &port);

    if (!port) {
        VLOG_ERR("No port found for: %s", mlearn_node->port_name);
        return;
    }

    memset(str, 0, sizeof(str));
    sprintf(str, ETH_ADDR_FMT, ETH_ADDR_ARGS(mlearn_node->mac));

    VLOG_DBG("%s: adding mac: %s, vlan: %d, bridge: %s, port: %s, from: %s",
              __FUNCTION__, str, mlearn_node->vlan, br->name, port->name,
              OVSREC_MAC_FROM_DYNAMIC);

    mac_e = ovsrec_mac_insert(mac_txn);
    ovsrec_mac_set_bridge(mac_e, br->cfg);
    ovsrec_mac_set_from(mac_e, OVSREC_MAC_FROM_DYNAMIC);
    ovsrec_mac_set_mac_addr(mac_e, str);
    ovsrec_mac_set_port(mac_e, port->cfg);
    ovsrec_mac_set_vlan(mac_e, mlearn_node->vlan);
}

/*
 * Function: mlearn_plugin_db_del_local_mac_entry
 *
 * This function takes the hmap node and deletes the corresponding entry
 * of MAC table in OVSDB.
 */
static void
mlearn_plugin_db_del_local_mac_entry (struct mlearn_hmap_node *mlearn_node)
{
    const struct ovsrec_mac *mac_e = NULL;
    struct bridge *br = NULL;
    struct port *port = NULL;
    char str[18];

    br = get_bridge_from_port_name(mlearn_node->port_name, &port);

    if (port) {
        memset(str, 0, sizeof(str));
        sprintf(str, ETH_ADDR_FMT, ETH_ADDR_ARGS(mlearn_node->mac));

        VLOG_DBG("%s: deleting mac: %s, vlan: %id, bridge: %s, port: %s, from: %s",
                 __FUNCTION__, str, mlearn_node->vlan, br->name, port->name,
                 OVSREC_MAC_FROM_DYNAMIC);

        OVSREC_MAC_FOR_EACH(mac_e, idl) {
            if ((strcmp(str, mac_e->mac_addr) == 0) &&
                (strcmp(OVSREC_MAC_FROM_DYNAMIC, mac_e->from) == 0) &&
                (mlearn_node->vlan == mac_e->vlan) &&
                (mac_e->bridge == br->cfg) &&
                (mac_e->port == port->cfg)) {
                /*
                 * row found, now delete
                 */
                ovsrec_mac_delete(mac_e);
            }
        }
    } else {
        VLOG_ERR("%s: No port found for: %s", __FUNCTION__, mlearn_node->port_name);
    }
}

/*
 * Function: mac_learning_update_db
 *
 * This function is invoked in bridge_run, it checks if the sequence number
 * for MAC learning has changed or not. If changed, it correspondingly calls
 * function to get the hmap populated during MAC learning.
 *
 * It creates, removes entries in MAC table depending on the operation.
 */
static void
mac_learning_update_db(void)
{
    struct mlearn_hmap *mhmap = NULL;
    struct mlearn_hmap_node *mlearn_node = NULL;
    enum ovsdb_idl_txn_status status;
    struct ovsdb_idl_txn *mac_txn = NULL;

    if (!idl) {
        VLOG_ERR("%s: mac learning init hasn't happened yet", __FUNCTION__);
        return;
    }

    if (!p_asic_plugin_interface) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    } else if (!p_asic_plugin_interface->get_mac_learning_hmap) {
        VLOG_ERR("%s: get_mac_learning_hmap is null", __FUNCTION__);
        return;
    } else {
        p_asic_plugin_interface->get_mac_learning_hmap(&mhmap);
    }

    if (mhmap) {
        if (!mac_txn) {
            mac_txn = ovsdb_idl_txn_create(idl);
            if (!mac_txn) {
                VLOG_ERR("%s: Unable to create transaction", __FUNCTION__);
                return;
            }
        }
        HMAP_FOR_EACH(mlearn_node, hmap_node, &(mhmap->table)) {
            if (mlearn_node->oper == MLEARN_ADD) {
                /*
                 * add
                 */
                mlearn_plugin_db_add_local_mac_entry(mlearn_node, mac_txn);
            } else {
                /*
                 * del
                 */
                mlearn_plugin_db_del_local_mac_entry(mlearn_node);
            }
        }
        if (mac_txn) {
            status = ovsdb_idl_txn_commit(mac_txn);
            if (status == TXN_ERROR) {
                VLOG_ERR("%s: commit failed, status: %d", __FUNCTION__, status);
            }
            ovsdb_idl_txn_destroy(mac_txn);
            mac_txn = NULL;
        }
    } else {
        VLOG_ERR("%s: hash map is NULL", __FUNCTION__);
    }
}

/*
 * Function: mac_learning_wait_seq
 *
 * This function waits on the new sequence number for MAC learning.
 */
static void mac_learning_wait_seq(void)
{
    seq_wait(mac_learning_trigger_seq_get(), mlearn_seqno);
}

/*
 * Function: run
 *
 * This function is called from plugins_run
 * It updates the MAC Table in ovsdb.
 */
int run (void)
{
    mac_learning_reconfigure();
    return 0;
}

/*
 * Function: wait
 *
 * This function is called from plugins_wait
 * It waits on the sequence number.
 */
int wait (void)
{
    mac_learning_wait_seq();
    return 0;
}

/*
 * Function: destroy
 *
 * This function is called from plugins_destroy when switchd process is
 * terminated.
 *
 * It unregisters the plugin.
 */
int destroy (void)
{
    unregister_plugin_extension(MAC_LEARNING_PLUGIN_INTERFACE_NAME);
    return 0;
}
