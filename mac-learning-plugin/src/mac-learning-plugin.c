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
#include "plugins.h"
#include "seq.h"
#include "mac-learning-plugin.h"
#include "asic-plugin.h"
#include "reconfigure-blocks.h"
#include "plugin-extensions.h"
#include "bridge.h"

VLOG_DEFINE_THIS_MODULE(mac_learning);

struct port;

/* OVSDB IDL used to obtain configuration. */
struct ovsdb_idl *idl = NULL;
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

static struct asic_plugin_interface *p_asic_plugin_interface = NULL;
static uint64_t mlearn_seqno = LLONG_MIN;

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

    /*
     * call register_reconfigure_callback for port del (flush),
     * vlan delete (flush) ...
     */
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
    if (blk_params->idl) {
        idl = blk_params->idl;
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_status);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_bridge);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_from);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_vlan);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_mac_addr);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_tunnel_key);
        ovsdb_idl_omit_alert(idl, &ovsrec_mac_col_port);
    } else {
        VLOG_ERR("%s: idl is not initialized in bridge_init", __FUNCTION__);
    }
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
