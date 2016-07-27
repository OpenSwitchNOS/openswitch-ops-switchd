/* Copyright (C) 2016 Hewlett-Packard Enterprise Development Company, L.P.
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
#include <ovs-thread.h>
#include <openswitch-idl.h>
#include <vswitch-idl.h>
#include <netinet/ether.h>
#include "plugins.h"
#include "seq.h"
#include "mac-learning-plugin.h"
#include "asic-plugin.h"
#include "reconfigure-blocks.h"
#include "plugin-extensions.h"
#include "poll-loop.h"
#include "bridge.h"
#include "timeval.h"

VLOG_DEFINE_THIS_MODULE(mac_learning);

struct port;

static struct ovsdb_idl_index_cursor cursor;

/* OVSDB IDL used to obtain configuration. */
static struct ovsdb_idl *idl = NULL;
static unsigned int maclearn_idl_seqno;

/* MAC Flush Retry time in msec */
#define MAC_FLUSH_RETRY_MSEC 1000

static void mac_learning_update_db(void);
static void mlearn_plugin_db_add_local_mac_entry (
                                  struct mlearn_hmap_node *mlearn_node,
                                  struct ovsdb_idl_txn *mac_txn);
static void mlearn_plugin_db_del_local_mac_entry (
                                  struct mlearn_hmap_node *mlearn_node);
struct asic_plugin_interface* get_plugin_asic_interface (void);
static void mac_learning_table_monitor (struct blk_params *blk_params);
static void mac_learning_wait_seq (void);
static void mac_learning_reconfigure (void);
static void mac_flush_monitor(struct blk_params *blk_params);

static struct asic_plugin_interface *p_asic_plugin_interface = NULL;
static uint64_t maclearn_seqno = LLONG_MIN;

static struct seq *mlearn_trigger_seq = NULL;

struct asic_plugin_interface *
get_plugin_asic_interface (void)
{
    struct plugin_extension_interface *p_extension = NULL;

    if (p_asic_plugin_interface) {
        return (p_asic_plugin_interface);
    }

    if (!find_plugin_extension(ASIC_PLUGIN_INTERFACE_NAME,
                               ASIC_PLUGIN_INTERFACE_MAJOR,
                               ASIC_PLUGIN_INTERFACE_MINOR,
                               &p_extension)) {
        if (p_extension) {
            p_asic_plugin_interface = p_extension->plugin_interface;
            return (p_extension->plugin_interface);
        }
    }
    return NULL;
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
    VLOG_DBG("in mac learning plugin init, phase_id: %d", phase_id);
    register_plugin_extension(&mac_learning_extension);

    VLOG_INFO("in mac learning plugin init, registering BLK_BRIDGE_INIT");

    register_reconfigure_callback(&mac_learning_table_monitor,
                                  BLK_BRIDGE_INIT,
                                  NO_PRIORITY);

    /* register_callback for port del */
    register_reconfigure_callback(&mac_flush_monitor,
                                  BLK_BR_DELETE_PORTS,
                                  NO_PRIORITY);
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
    struct ovsdb_idl_index *index = NULL;
    bool cursor_initialized = false;

    /*
     * MAC table related
     */
    if (blk_params->idl) {
        idl = blk_params->idl;
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_status);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_bridge);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_from);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_mac_vlan);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_mac_addr);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_port);
    } else {
        VLOG_ERR("%s: idl is not initialized in bridge_init", __FUNCTION__);
        return;
    }

    /* Initialize Compound Indexes */
    index = ovsdb_idl_create_index(idl, &ovsrec_table_mac, "by_macFrom");
    ovs_assert(index);

    /* add indexing columns */
    ovsdb_idl_index_add_column(index, &ovsrec_mac_col_mac_addr,
                               OVSDB_INDEX_ASC, ovsrec_mac_index_mac_addr_cmp);
    ovsdb_idl_index_add_column(index, &ovsrec_mac_col_from,
                               OVSDB_INDEX_ASC, ovsrec_mac_index_from_cmp);

    cursor_initialized = ovsdb_idl_initialize_cursor(idl, &ovsrec_table_mac,
                                                     "by_macFrom", &cursor);
    ovs_assert(cursor_initialized == true);
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

    if (seq != maclearn_seqno) {
        maclearn_seqno = seq;
        mac_learning_update_db();
    }
}

/*
 * Function: mlearn_plugin_db_add_local_mac_entry
 *
 * This function takes the hmap node and inserts/updates the corresponding entry
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
    struct ovsrec_mac mac_val;
    char str[18];
    bool found = false;

    if (mlearn_node == NULL) {
        VLOG_ERR("%s: mlearn_node is null", __FUNCTION__);
        return;
    }

    br = get_bridge_from_port_name(mlearn_node->port_name, &port);

    if (!port) {
        VLOG_DBG("%s: port not found %s "ETH_ADDR_FMT, __FUNCTION__,
                 mlearn_node->port_name, ETH_ADDR_ARGS(mlearn_node->mac));
        return;
    }

    memset(str, 0, sizeof(str));
    sprintf(str, ETH_ADDR_FMT, ETH_ADDR_ARGS(mlearn_node->mac));

    /* initialize the indexes with values to be compared  */
    memset(&mac_val, 0, sizeof(mac_val));
    mac_val.mac_addr = str;
    mac_val.from = OVSREC_MAC_FROM_DYNAMIC;

    OVSREC_MAC_FOR_EACH_EQUAL (mac_e, &cursor, &mac_val) {
        /* MAC entry found and update the move state*/
        if ( mac_e->mac_vlan && mlearn_node->vlan == mac_e->mac_vlan->id) {
            ovsrec_mac_set_bridge(mac_e, br->cfg);
            ovsrec_mac_set_port(mac_e, port->cfg);
            found = true;
            VLOG_DBG("%s: MAC:%s update vlan: %d, bridge: %s, port: %s, from: %s",
                     __FUNCTION__, str, mlearn_node->vlan, br->name, port->name,
                     OVSREC_MAC_FROM_DYNAMIC);
        }
    }

    /* MAC Entry not found, consider as new entry */
    if (!found) {
        mac_e = ovsrec_mac_insert(mac_txn);
        ovsrec_mac_set_bridge(mac_e, br->cfg);
        ovsrec_mac_set_from(mac_e, OVSREC_MAC_FROM_DYNAMIC);
        ovsrec_mac_set_mac_addr(mac_e, str);
        ovsrec_mac_set_port(mac_e, port->cfg);
        ops_mac_set_vlan(mlearn_node->vlan, mac_e, idl);
        VLOG_DBG("%s: %s: insert vlan: %d, bridge: %s, port: %s, from: %s",
                  __FUNCTION__, str, mlearn_node->vlan, br->name, port->name,
                  OVSREC_MAC_FROM_DYNAMIC);
    }
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
    struct ovsrec_mac mac_val;
    char str[18] = {0};

    if (mlearn_node == NULL) {
        VLOG_ERR("%s: mlearn_node is null", __FUNCTION__);
        return;
    }
    snprintf(str, sizeof(str), ETH_ADDR_FMT,
                                ETH_ADDR_ARGS(mlearn_node->mac));

    VLOG_DBG("%s: deleting mac: %s, vlan: %id, from: %s",
             __FUNCTION__, str, mlearn_node->vlan, OVSREC_MAC_FROM_DYNAMIC);

    memset(&mac_val, 0, sizeof(mac_val));

    /* initialize the indexes with values to be comapred  */
    mac_val.mac_addr = str;
    mac_val.from = OVSREC_MAC_FROM_DYNAMIC;

    OVSREC_MAC_FOR_EACH_EQUAL (mac_e, &cursor, &mac_val) {
        /* mac row found and if it match same vlan, now delete */
        if ( mac_e->mac_vlan && mlearn_node->vlan == mac_e->mac_vlan->id) {
            ovsrec_mac_delete(mac_e);
        }
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

    struct asic_plugin_interface *p_asic_interface = NULL;

    if (!idl) {
        VLOG_ERR("%s: mac learning init hasn't happened yet", __FUNCTION__);
        return;
    }

    p_asic_interface = get_plugin_asic_interface();
    if (!p_asic_interface) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    } else if (!p_asic_interface->get_mac_learning_hmap) {
        VLOG_ERR("%s: get_mac_learning_hmap is null", __FUNCTION__);
        return;
    } else {
        p_asic_interface->get_mac_learning_hmap(&mhmap);
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
            if (mlearn_node->oper == MLEARN_ADD
                || mlearn_node->oper == MLEARN_MOVE) {
                /* add/move learnt mac to MAC table */
                mlearn_plugin_db_add_local_mac_entry(mlearn_node, mac_txn);
            } else {
                /* delete mac from the MAC table */
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
    seq_wait(mac_learning_trigger_seq_get(), maclearn_seqno);
}

/*
 * Function: mac_flush_deleted_vlans
 *
 * Monitoring deleting ports to flush MAC entries.
 *
 */
static void
mac_flush_deleted_vlans(struct blk_params *blk_params)
{
    mac_flush_params_t settings;
    size_t i = 0;
    struct vlan *vlan = NULL, *next = NULL;
    struct shash sh_idl_vlans;
    struct bridge *br = NULL;
    struct asic_plugin_interface *p_asic_interface = NULL;
    int rc = 0;

    p_asic_interface = get_plugin_asic_interface();

    if(p_asic_interface == NULL) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    }

    br = blk_params->br;

    if(br == NULL) {
        VLOG_ERR("%s: unable to find bridge", __FUNCTION__);
        return;
    }

    /* Collect all the VLANs present in the DB. */
     shash_init(&sh_idl_vlans);
     for (i = 0; i < br->cfg->n_vlans; i++) {
         const char *name = br->cfg->vlans[i]->name;
         if (!shash_add_once(&sh_idl_vlans, name, br->cfg->vlans[i])) {
             VLOG_WARN("bridge %s: %s specified twice as bridge VLAN",
                       br->name, name);
         }
     }

     /* Trigger mac flush on the deleted vlans */
     HMAP_FOR_EACH_SAFE (vlan, next, hmap_node, &br->vlans) {
         const struct ovsrec_vlan *vlan_cfg;

         vlan_cfg = shash_find_data(&sh_idl_vlans, vlan->name);
         if (!vlan_cfg) {
             VLOG_DBG("Found a deleted VLAN %s", vlan->name);
             memset(&settings, 0, sizeof(mac_flush_params_t));
             settings.options = L2MAC_FLUSH_BY_VLAN;
             settings.vlan = vlan->vid;
             rc = (p_asic_interface->l2_addr_flush
                      ? p_asic_interface->l2_addr_flush(&settings)
                      : -1);
             if (rc < 0) {
                 VLOG_ERR("%s: %s rc %d", __FUNCTION__,
                          vlan->name, rc);
             }
         }
     }

     /* Destroy the shash of the IDL vlans */
     shash_destroy(&sh_idl_vlans);
} /* mac_flush_deleted_vlans */

/*
 * Function: mac_flush_deleted_ports
 *
 * Monitoring deleting ports to flush MAC entries.
 *
 */
static void
mac_flush_deleted_ports(struct blk_params *blk_params)
{
    mac_flush_params_t settings;
    struct port *port = NULL, *next = NULL;
    struct bridge *br = NULL;
    struct asic_plugin_interface *p_asic_interface = NULL;
    int rc = 0;

    p_asic_interface = get_plugin_asic_interface();

    if(p_asic_interface == NULL) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    }

    br = blk_params->br;

    if(br == NULL) {
        VLOG_ERR("%s: unable to find bridge", __FUNCTION__);
        return;
    }

    /* Trigger mac flush on the deleted ports */
    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &br->ports) {
        port->cfg = shash_find_data(&br->wanted_ports, port->name);
        if (!port->cfg) {
            memset(&settings, 0, sizeof(mac_flush_params_t));
            /* is it LAG port ?*/
            if(!strncmp(port->name,
                        LAG_PORT_NAME_PREFIX,
                        LAG_PORT_NAME_PREFIX_LENGTH))    {
                if (port->bond_hw_handle != -1)   {
                    strncpy(settings.port_name, port->name, PORT_NAME_SIZE);
                    settings.options = L2MAC_FLUSH_BY_TRUNK;
                    settings.tgid = port->bond_hw_handle;
                    rc = (p_asic_interface->l2_addr_flush
                            ? p_asic_interface->l2_addr_flush(&settings)
                            : -1);
                }   else    {
                    VLOG_ERR("%s: %s invalid bond_hw_handle %d", __FUNCTION__,
                         port->name, rc);
                }

            }   else {
                settings.options = L2MAC_FLUSH_BY_PORT;
                strncpy(settings.port_name, port->name, PORT_NAME_SIZE);
                rc = (p_asic_interface->l2_addr_flush
                         ? p_asic_interface->l2_addr_flush(&settings)
                         : -1);
            }

            if (rc < 0) {
                VLOG_ERR("%s: %s rc %d", __FUNCTION__,
                         settings.port_name, rc);
            }
        }
    }
} /* mac_flush_deleted_ports */

/*
 * Function: mac_flush_monitor
 *
 * Monitoring deleting ports/vlan to flush MAC entries.
 *
 */
static void
mac_flush_monitor(struct blk_params *blk_params)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_port *port_row = NULL;

    vlan_row = ovsrec_vlan_first(idl);
    if (vlan_row && OVSREC_IDL_ANY_TABLE_ROWS_DELETED(vlan_row, maclearn_idl_seqno)) {
        mac_flush_deleted_vlans(blk_params);
    }

    port_row = ovsrec_port_first(idl);
    if (port_row && OVSREC_IDL_ANY_TABLE_ROWS_DELETED(port_row, maclearn_idl_seqno)) {
        mac_flush_deleted_ports(blk_params);
    }
} /* mac_flush_monitor */

/*
 * Function: mac_flush_on_vlan
 *
 * This function handles MAC flush requests on the specified VLAN
 */
static void
mac_flush_on_vlan(const struct ovsrec_vlan *row)
{
    mac_flush_params_t  settings;
    int rc = 0;
    struct ovsdb_idl_txn *txn = NULL;
    enum ovsdb_idl_txn_status status = TXN_SUCCESS;
    struct asic_plugin_interface *p_asic_interface = NULL;

    p_asic_interface = get_plugin_asic_interface();

    if(p_asic_interface == NULL) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    }

    memset(&settings, 0, sizeof(mac_flush_params_t));

    /* Handle only VLAN flush requests */
    settings.options = L2MAC_FLUSH_BY_VLAN;
    settings.vlan = (int)row->id;

    txn = ovsdb_idl_txn_create(idl);

    rc = (p_asic_interface->l2_addr_flush
             ? p_asic_interface->l2_addr_flush(&settings)
             : -1);

    if (rc < 0) {
        VLOG_ERR("%s: VLAN %d plugin error vlan ", __FUNCTION__,
                 (int)row->id);
    }

    ovsrec_vlan_set_macs_invalid(row, NULL, 0);
    status = ovsdb_idl_txn_commit(txn);

    if (status == TXN_ERROR)    {
        ovsdb_idl_txn_abort(txn);
        VLOG_ERR("%s: VLAN %s flush commit failed\n",
                 __FUNCTION__, row->name);
    }

    ovsdb_idl_txn_destroy(txn);
    return;
}

/*
 * Function: mac_flush_on_port
 *
 * This function handles MAC flush requests on the specified Port
 */
static void
mac_flush_on_port(const struct ovsrec_port *row)
{
    mac_flush_params_t settings;
    int rc = 0, i = 0;
    struct ovsdb_idl_txn *txn;
    enum ovsdb_idl_txn_status status = TXN_SUCCESS;
    bool modified = false;
    struct asic_plugin_interface *p_asic_interface = NULL;
    int bond_hw_handle = -1;

    p_asic_interface = get_plugin_asic_interface();

    if(p_asic_interface == NULL) {
        VLOG_ERR("%s: unable to find asic interface", __FUNCTION__);
        return;
    }

    /* Handle port mac flush requests */
    for (i = 0; i < row->n_macs_invalid; i++) {
        if (row->macs_invalid[i] == true) {
            memset(&settings, 0, sizeof(mac_flush_params_t));
            /* is it LAG port ?*/
            if(!strncmp(row->name,
                        LAG_PORT_NAME_PREFIX,
                        LAG_PORT_NAME_PREFIX_LENGTH))    {
                bond_hw_handle = smap_get_int(&row->status,
                                PORT_STATUS_BOND_HW_HANDLE, -1);
                if (bond_hw_handle != -1)   {
                    strncpy(settings.port_name, row->name, PORT_NAME_SIZE);
                    settings.options = L2MAC_FLUSH_BY_TRUNK;
                    settings.tgid = bond_hw_handle;
                    rc = (p_asic_interface->l2_addr_flush
                            ? p_asic_interface->l2_addr_flush(&settings)
                            : -1);
                }   else    {
                    VLOG_ERR("%s: %s invalid bond_hw_handle %d", __FUNCTION__,
                         row->name, rc);
                }

            } else  {
                settings.options = L2MAC_FLUSH_BY_PORT;
                strncpy(settings.port_name, row->name, PORT_NAME_SIZE);
                rc = (p_asic_interface->l2_addr_flush
                         ? p_asic_interface->l2_addr_flush(&settings)
                         : -1);
            }
            modified = true;
            if (rc < 0) {
                VLOG_ERR("%s: %s rc %d", __FUNCTION__,
                         settings.port_name, rc);
            }
        }
    }

    /* Handle port with vlans mac flush requests */
    for (i = 0; i < row->n_macs_invalid_on_vlans; i++) {
        if (row->macs_invalid_on_vlans[i]) {
            memset(&settings, 0, sizeof(mac_flush_params_t));
            /* is it LAG port ?*/
            if(!strncmp(row->name,
                        LAG_PORT_NAME_PREFIX,
                        LAG_PORT_NAME_PREFIX_LENGTH))    {
                bond_hw_handle = smap_get_int(&row->status,
                                PORT_STATUS_BOND_HW_HANDLE, -1);
                if (bond_hw_handle != -1)   {
                    strncpy(settings.port_name, row->name, PORT_NAME_SIZE);
                    settings.options = L2MAC_FLUSH_BY_TRUNK_VLAN;
                    settings.tgid = bond_hw_handle;
                    settings.vlan = row->macs_invalid_on_vlans[i]->id;
                    rc = (p_asic_interface->l2_addr_flush
                            ? p_asic_interface->l2_addr_flush(&settings)
                            : -1);
                }   else    {
                    VLOG_ERR("%s: %s invalid bond_hw_handle %d", __FUNCTION__,
                         row->name, rc);
                }
            } else  {
                settings.options = L2MAC_FLUSH_BY_PORT_VLAN;
                strncpy(settings.port_name, row->name, PORT_NAME_SIZE);
                settings.vlan = row->macs_invalid_on_vlans[i]->id;
                rc = (p_asic_interface->l2_addr_flush
                         ? p_asic_interface->l2_addr_flush(&settings)
                         : -1);
            }
            modified = true;
            if (rc < 0) {
                VLOG_ERR("%s: %s vlan %d rc %d", __FUNCTION__, settings.port_name,
                         settings.vlan, rc);
            }
        }
    }

    /* Check any modifications? */
    if (modified == false)   {
        return;
    }

    txn = ovsdb_idl_txn_create(idl);

    ovsrec_port_set_macs_invalid(row, NULL, 0);
    ovsrec_port_verify_macs_invalid(row);
    ovsrec_port_set_macs_invalid_on_vlans(row, NULL, 0);
    ovsrec_port_verify_macs_invalid_on_vlans(row);
    status = ovsdb_idl_txn_commit(txn);

    /* Switchd will get change and Later retry the transaction. */
    if (status == TXN_TRY_AGAIN)    {
        ovsdb_idl_txn_abort(txn);
        poll_timer_wait_until(time_msec() + MAC_FLUSH_RETRY_MSEC);
        VLOG_ERR("%s: Port %s flush update Try Again \n",
                 __FUNCTION__, row->name);
    }

    ovsdb_idl_txn_destroy(txn);
    return;
}

void
l2_addr_flush(void)
{
    const struct ovsrec_port *port_row;
    const struct ovsrec_vlan *vlan_row;

    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_macs_invalid,
                                       maclearn_idl_seqno)
        || OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_macs_invalid_on_vlans,
                                       maclearn_idl_seqno)) {
        /* Check Port row changed */
        OVSREC_PORT_FOR_EACH(port_row, idl)  {
            if (OVSREC_IDL_IS_ROW_MODIFIED(port_row, maclearn_idl_seqno)) {
                mac_flush_on_port(port_row);
                VLOG_DBG("%s: Port %s flush \n", __FUNCTION__, port_row->name);
            }
        }
    }

    /* Check VLAN flush requests */
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_vlan_col_macs_invalid,
                                       maclearn_idl_seqno)) {
        /* Check VLAN row changed */
        OVSREC_VLAN_FOR_EACH(vlan_row, idl)  {
            if (OVSREC_IDL_IS_ROW_MODIFIED(vlan_row, maclearn_idl_seqno)) {
                mac_flush_on_vlan(vlan_row);
                VLOG_DBG("%s: VLAN %d flush \n", __FUNCTION__, (int)vlan_row->id);
            }
        }
    }
}

static void
mac_config_update(void)
{
    /* Hanlde MAC table flush requests */
    l2_addr_flush();
}

/*
 * Function: run
 *
 * This function is called from plugins_run
 * It updates the MAC Table in ovsdb.
 */
int run (void)
{
    unsigned int new_idl_seqno = ovsdb_idl_get_seqno(idl);

    mac_learning_reconfigure();

    /* Check any change in the idl? */
     if (new_idl_seqno != maclearn_idl_seqno) {

        mac_config_update();

        /* Update IDL sequence # after we've handled everything. */
        maclearn_idl_seqno = new_idl_seqno;
    }

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
