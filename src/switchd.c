/* Copyright (C) 2015, 2016 Hewlett-Packard Development Company, L.P.
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

#include "openvswitch/vlog.h"
#include "daemon.h"
#include "plugins.h"
#include "bridge.h"
#include "subsystem.h"
#include "bufmon-provider.h"
#include "memory.h"
#include "switchd-unixctl.h"
#include "netdev.h"
#include "status.h"
#include "system-stats.h"
#include "poll-loop.h"
#include "vlan.h"
#include "switchd-ofproto.h"
#include "ofproto/bond.h"
#include "ovs-numa.h"
#include "port.h"
#include "switchd.h"
#include "vrf.h"

VLOG_DEFINE_THIS_MODULE(switchd);

/* OVSDB IDL used to obtain configuration. */
struct ovsdb_idl *idl;

/* Most recently processed IDL sequence number. */
unsigned int idl_seqno;

struct ovsrec_system switchd_null_cfg;

/* We want to complete daemonization, fully detaching from our parent process,
 * only after we have completed our initial configuration, committed our state
 * to the database, and received confirmation back from the database server
 * that it applied the commit.  This allows our parent process to know that,
 * post-detach, ephemeral fields such as datapath-id and ofport are very likely
 * to have already been filled in.  (It is only "very likely" rather than
 * certain because there is always a slim possibility that the transaction will
 * fail or that some other client has added new bridges, ports, etc. while
 * ovs-vswitchd was configuring using an old configuration.)
 *
 * We only need to do this once for our initial configuration at startup, so
 * 'initial_config_done' tracks whether we've already done it.  While we are
 * waiting for a response to our commit, 'daemonize_txn' tracks the transaction
 * itself and is otherwise NULL. */
bool initial_config_done;
struct ovsdb_idl_txn *daemonize_txn;

static const struct ovsrec_system * wait_for_config_complete(void);

void
switchd_init(char *unixctl_path, char *plugins_path)
{
    const struct ovsrec_system *cfg;

    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, true, true);
    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "ovs_vswitchd");
    ovsdb_idl_verify_write_only(idl);

    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_cur_cfg);
    ovsdb_idl_omit_alert(idl, &ovsrec_system_col_statistics);
    ovsdb_idl_omit(idl, &ovsrec_system_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_system_col_db_version);

    switchd_unixctl_init(unixctl_path);
    plugins_init(plugins_path);
    bridge_init();
    port_init();
    vlan_init();
    subsystem_init();
    bufmon_init();
    lacp_init();
    bond_init();

    ovs_numa_init();

    cfg = wait_for_config_complete();

    /* Initialize the ofproto library. This needs to be done
     * after the configuration is set. */
    switchd_ofproto_init(cfg);
}

void
switchd_run()
{
    const struct ovsrec_system *cfg;

    memory_run();
    if (memory_should_report()) {
        struct simap usage;

        simap_init(&usage);
        bridge_get_memory_usage(&usage);
        memory_report(&usage);
        simap_destroy(&usage);
    }

    ovsrec_system_init(&switchd_null_cfg);

    ovsdb_idl_run(idl);

    if (ovsdb_idl_is_lock_contended(idl)
        || (!ovsdb_idl_has_lock(idl))) {
        switchd_exiting = true;
        return;
    }

    switchd_ofproto_run();

    cfg = ovsrec_system_first(idl);

    if  (ovsdb_idl_get_seqno(idl) != idl_seqno) {
        struct ovsdb_idl_txn *txn;

        txn = ovsdb_idl_txn_create(idl);

        bridge_reconfigure(cfg ? cfg : &switchd_null_cfg);

        idl_seqno = ovsdb_idl_get_seqno(idl);

        if (cfg) {
            ovsrec_system_set_cur_cfg(cfg, cfg->next_cfg);
        }

        /* If we are completing our initial configuration for this run
         * of ovs-vswitchd, then keep the transaction around to monitor
         * it for completion. */
        if (initial_config_done) {
            /* Always sets the 'status_txn_try_again' to check again,
             * in case that this transaction fails. */
            status_txn_try_again = true;
            ovsdb_idl_txn_commit(txn);
            ovsdb_idl_txn_destroy(txn);
        } else {
            initial_config_done = true;
            daemonize_txn = txn;
        }
    }

    if (daemonize_txn) {
        enum ovsdb_idl_txn_status status = ovsdb_idl_txn_commit(daemonize_txn);
        if (status != TXN_INCOMPLETE) {
            ovsdb_idl_txn_destroy(daemonize_txn);
            daemonize_txn = NULL;

            /* ovs-vswitchd has completed initialization, so allow the
             * process that forked us to exit successfully. */
            daemonize_complete();

            vlog_enable_async();

            VLOG_INFO_ONCE("%s (OpenSwitch Switch Daemon)", program_name);
        }
    }

    iface_stats_run();
    run_status_update();
    system_stats_run();
    neighbor_update();
    subsystem_run();
    bufmon_run();
    switchd_unixctl_run();
    netdev_run();
    plugins_run();
}

void
switchd_wait()
{
    ovsdb_idl_wait(idl);
    if (daemonize_txn) {
        ovsdb_idl_txn_wait(daemonize_txn);
    }

    memory_wait();

    bridge_wait();
    iface_stats_wait();
    status_update_wait();
    system_stats_wait();
    subsystem_wait();
    bufmon_wait();
    switchd_unixctl_wait();
    netdev_wait();
    plugins_wait();
}

void
switchd_exit()
{
    bridge_exit();
    subsystem_exit();
    switchd_unixctl_exit();
    plugins_destroy();

    ovsdb_idl_destroy(idl);
}

/* This function waits for SYSd and CONFIGd to complete their system
 * initialization before proceeding.  This means waiting for
 * Open_vSwitch table 'cur_cfg' column to become >= 1.
 */
static const struct ovsrec_system *
wait_for_config_complete(void)
{
    int system_configured = false;
    const struct ovsrec_system *cfg = NULL;

    while (!ovsdb_idl_has_lock(idl)) {
        ovsdb_idl_run(idl);
        ovsdb_idl_wait(idl);
    }

    while (!system_configured) {
        cfg = ovsrec_system_first(idl);
        system_configured = (cfg && (cfg->cur_cfg >= 1));
        if (!system_configured) {
            poll_block();;
            ovsdb_idl_run(idl);
            ovsdb_idl_wait(idl);
        } else {
            VLOG_INFO("System is now configured (cur_cfg=%d).",
                      (int)cfg->cur_cfg);
        }
    }

    return cfg;
}
