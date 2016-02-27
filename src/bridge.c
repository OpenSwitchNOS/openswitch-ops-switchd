/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014 Nicira, Inc.
 * Copyright (C) 2015, 2016 Hewlett-Packard Development Company, L.P.
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

#include <config.h>
#include "bridge.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include "async-append.h"
#include "bfd.h"
#include "bitmap.h"
#ifndef OPS_TEMP
#include "cfm.h"
#endif
#include "connectivity.h"
#include "coverage.h"
#include "daemon.h"
#include "dirs.h"
#include "dynamic-string.h"
#include "hash.h"
#include "hmap.h"
#include "hmapx.h"
#include "jsonrpc.h"
#include "lacp.h"
#include "list.h"
#include "mac-learning.h"
#include "mcast-snooping.h"
#include "meta-flow.h"
#include "netdev.h"
#include "nx-match.h"
#include "ofp-print.h"
#include "ofp-util.h"
#include "ofpbuf.h"
#include "ofproto/bond.h"
#include "ofproto/ofproto.h"
#include "ovs-numa.h"
#include "poll-loop.h"
#include "seq.h"
#include "sha1.h"
#include "shash.h"
#include "smap.h"
#include "socket-util.h"
#include "stream.h"
#include "stream-ssl.h"
#include "sset.h"
#include "system-stats.h"
#include "timeval.h"
#include "util.h"
#include "unixctl.h"
#include "vlandev.h"
#include "vswitch-idl.h"
#include "openvswitch/vlog.h"
#include "sflow_api.h"
#include "vlan-bitmap.h"
#include "packets.h"

#ifdef OPS
#include <string.h>
#include <netinet/ether.h>
#include "vrf.h"
#include "openswitch-idl.h"
#include "openswitch-dflt.h"
#include "reconfigure-blocks.h"
#endif

VLOG_DEFINE_THIS_MODULE(bridge);

COVERAGE_DEFINE(bridge_reconfigure);

struct iface {
    /* These members are always valid.
     *
     * They are immutable: they never change between iface_create() and
     * iface_destroy(). */
    struct ovs_list port_elem;  /* Element in struct port's "ifaces" list. */
    struct hmap_node name_node; /* In struct bridge's "iface_by_name" hmap. */
    struct hmap_node ofp_port_node; /* In struct bridge's "ifaces" hmap. */
    struct port *port;          /* Containing port. */
    char *name;                 /* Host network device name. */
    struct netdev *netdev;      /* Network device. */
    ofp_port_t ofp_port;        /* OpenFlow port number. */
    uint64_t change_seq;

    /* These members are valid only within bridge_reconfigure(). */
    const char *type;           /* Usually same as cfg->type. */
    const struct ovsrec_interface *cfg;
};

#ifndef OPS_TEMP
struct mirror {
    struct uuid uuid;           /* UUID of this "mirror" record in database. */
    struct hmap_node hmap_node; /* In struct bridge's "mirrors" hmap. */
    struct bridge *bridge;
    char *name;
    const struct ovsrec_mirror *cfg;
};
#endif

#ifndef OPS /* Moved to bridge.h, to access in vrf.c */
struct port {
    struct hmap_node hmap_node; /* Element in struct bridge's "ports" hmap. */
    struct bridge *bridge;
    char *name;

    const struct ovsrec_port *cfg;

    /* An ordinary bridge port has 1 interface.
     * A bridge port for bonding has at least 2 interfaces. */
    struct ovs_list ifaces;    /* List of "struct iface"s. */
#ifdef OPS
    int bond_hw_handle;        /* Hardware bond identifier. */
#endif
};
#endif


#ifdef OPS
struct vlan {
    struct hmap_node hmap_node;  /* In struct bridge's "vlans" hmap. */
    struct bridge *bridge;
    char *name;
    int vid;
    const struct ovsrec_vlan *cfg;

    bool enable;
};
#endif

struct bridge {
    struct hmap_node node;      /* In 'all_bridges'. */
    char *name;                 /* User-specified arbitrary name. */
    char *type;                 /* Datapath type. */
    struct eth_addr ea;         /* Bridge Ethernet Address. */
    struct eth_addr default_ea; /* Default MAC. */
    const struct ovsrec_bridge *cfg;

    /* OpenFlow switch processing. */
    struct ofproto *ofproto;    /* OpenFlow switch. */

    /* Bridge ports. */
    struct hmap ports;          /* "struct port"s indexed by name. */
    struct hmap ifaces;         /* "struct iface"s indexed by ofp_port. */
    struct hmap iface_by_name;  /* "struct iface"s indexed by name. */

#ifdef OPS
    /* Bridge VLANs. */
    struct hmap vlans;          /* "struct vlan"s indexed by VID. */
#endif

#ifndef OPS_TEMP
    /* Port mirroring. */
    struct hmap mirrors;        /* "struct mirror" indexed by UUID. */
#endif
    /* Used during reconfiguration. */
    struct shash wanted_ports;

    /* Synthetic local port if necessary. */
    struct ovsrec_port synth_local_port;
    struct ovsrec_interface synth_local_iface;
    struct ovsrec_interface *synth_local_ifacep;
};

/* All bridges, indexed by name. */
static struct hmap all_bridges = HMAP_INITIALIZER(&all_bridges);

#ifdef OPS
/* Even though VRF is a separate entity from a user and schema
 * perspective, it's essentially very similar to bridge. It has ports,
 * bundles, mirros, might provide sFlow, NetFLow etc.
 *
 * In the future, it may also provide OpenFlow datapath, with OFP_NORMAL
 * falling back to the regular routing. Current code makes basic preparation
 * for this option by establising ofproto, and managing ports through it,
 * but not taking care of Openflow configuration itself. The use of ofproto
 * also allows ofproto providers to share common port/bundle/mirrors/etc
 * code more easily.
 *
 * VRFs also have quite a few principal differences like routes, neightbours,
 * routing protocols and not having VLANs.
 * In order to reuse as much of Bridge code as possible, struct vrf
 * "inherits" struct bridge. While configuration of VRF has to read
 * from a different table, port_configure, mirror_configure and may
 * other functions would be shared with the bridge. */
/* All vrfs, indexed by name. */
static struct hmap all_vrfs = HMAP_INITIALIZER(&all_vrfs);

static void vrf_add_neighbors(struct vrf *vrf);
static void vrf_reconfigure_neighbors(struct vrf *vrf);
static void vrf_delete_all_neighbors(struct vrf *vrf);
static void vrf_delete_port_neighbors(struct vrf *vrf, struct port *port);

/* Each time this timer expires, go through Neighbor table and query th
** ASIC for data-path hit-bit for each and update DB. */
static int neighbor_timer_interval;
static long long int neighbor_timer = LLONG_MIN;
#define NEIGHBOR_HIT_BIT_UPDATE_INTERVAL   10000
static void run_neighbor_update(void);
#endif

/* OVSDB IDL used to obtain configuration. */
#ifdef OPS
struct ovsdb_idl *idl;
#else
static struct ovsdb_idl *idl;
#endif

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
static bool initial_config_done;
static struct ovsdb_idl_txn *daemonize_txn;

/* Most recently processed IDL sequence number. */
#ifdef OPS
unsigned int idl_seqno;
#else
static unsigned int idl_seqno;
#endif

/* Track changes to port connectivity. */
static uint64_t connectivity_seqno = LLONG_MIN;

/* Status update to database.
 *
 * Some information in the database must be kept as up-to-date as possible to
 * allow controllers to respond rapidly to network outages.  Those status are
 * updated via the 'status_txn'.
 *
 * We use the global connectivity sequence number to detect the status change.
 * Also, to prevent the status update from sending too much to the database,
 * we check the return status of each update transaction and do not start new
 * update if the previous transaction status is 'TXN_INCOMPLETE'.
 *
 * 'statux_txn' is NULL if there is no ongoing status update.
 *
 * If the previous database transaction was failed (is not 'TXN_SUCCESS',
 * 'TXN_UNCHANGED' or 'TXN_INCOMPLETE'), 'status_txn_try_again' is set to true,
 * which will cause the main thread wake up soon and retry the status update.
 */
static struct ovsdb_idl_txn *status_txn;
static bool status_txn_try_again;

/* When the status update transaction returns 'TXN_INCOMPLETE', should register a
 * timeout in 'STATUS_CHECK_AGAIN_MSEC' to check again. */
#define STATUS_CHECK_AGAIN_MSEC 100

/* Each time this timer expires, the bridge fetches interface and mirror
 * statistics and pushes them into the database. */
static int stats_timer_interval;
static long long int stats_timer = LLONG_MIN;

/* In some datapaths, creating and destroying OpenFlow ports can be extremely
 * expensive.  This can cause bridge_reconfigure() to take a long time during
 * which no other work can be done.  To deal with this problem, we limit port
 * adds and deletions to a window of OFP_PORT_ACTION_WINDOW milliseconds per
 * call to bridge_reconfigure().  If there is more work to do after the limit
 * is reached, 'need_reconfigure', is flagged and it's done on the next loop.
 * This allows the rest of the code to catch up on important things like
 * forwarding packets. */
#define OFP_PORT_ACTION_WINDOW 10

static void add_del_bridges(const struct ovsrec_open_vswitch *);
static void bridge_run__(void);
static void bridge_create(const struct ovsrec_bridge *);
static void bridge_destroy(struct bridge *);
static struct bridge *bridge_lookup(const char *name);
static unixctl_cb_func bridge_unixctl_dump_flows;
static unixctl_cb_func bridge_unixctl_reconnect;
#ifndef OPS_TEMP
static size_t bridge_get_controllers(const struct bridge *br,
                                     struct ovsrec_controller ***controllersp);
#endif
static void bridge_collect_wanted_ports(struct bridge *,
                                        const unsigned long *splinter_vlans,
                                        struct shash *wanted_ports);
static void bridge_delete_ofprotos(void);
static void bridge_delete_or_reconfigure_ports(struct bridge *);
static void bridge_del_ports(struct bridge *,
                             const struct shash *wanted_ports);
static void bridge_add_ports(struct bridge *,
                             const struct shash *wanted_ports);

#ifdef OPS
static void add_del_vrfs(const struct ovsrec_open_vswitch *);
static void vrf_create(const struct ovsrec_vrf *);
static void vrf_destroy(struct vrf *);
static struct vrf *vrf_lookup(const char *name);
static void vrf_collect_wanted_ports(struct vrf *,
                                        struct shash *wanted_ports);
static void vrf_delete_or_reconfigure_ports(struct vrf *);
static void vrf_del_ports(struct vrf *,
                             const struct shash *wanted_ports);
static bool enable_lacp(struct port *port, bool *activep);
static void bridge_configure_vlans(struct bridge *br);
static unixctl_cb_func vlan_unixctl_show;
#endif
static void bridge_configure_datapath_id(struct bridge *);
#ifndef OPS_TEMP
static void bridge_configure_netflow(struct bridge *);
static void bridge_configure_forward_bpdu(struct bridge *);
#endif
static void bridge_configure_mac_table(struct bridge *);
#ifndef OPS_TEMP
static void bridge_configure_mcast_snooping(struct bridge *);
static void bridge_configure_sflow(struct bridge *, int *sflow_bridge_number);
static void bridge_configure_ipfix(struct bridge *);
static void bridge_configure_stp(struct bridge *);
static void bridge_configure_rstp(struct bridge *);
static void bridge_configure_tables(struct bridge *);
#endif
static void bridge_configure_dp_desc(struct bridge *);
#ifndef OPS_TEMP
static void bridge_configure_remotes(struct bridge *,
                                     const struct sockaddr_in *managers,
                                     size_t n_managers);
#endif
static void bridge_pick_local_hw_addr(struct bridge *,
                                      struct eth_addr *ea,
                                      struct iface **hw_addr_iface);
static uint64_t bridge_pick_datapath_id(struct bridge *,
                                        const struct eth_addr bridge_ea,
                                        struct iface *hw_addr_iface);
static uint64_t dpid_from_hash(const void *, size_t nbytes);
#ifndef OPS_TEMP
static bool bridge_has_bond_fake_iface(const struct bridge *,
                                       const char *name);
static bool port_is_bond_fake_iface(const struct port *);

static unixctl_cb_func qos_unixctl_show;
#endif
static struct port *port_create(struct bridge *, const struct ovsrec_port *);
static void port_del_ifaces(struct port *);
static void port_destroy(struct port *);
static struct port *port_lookup(const struct bridge *, const char *name);
static void port_configure(struct port *);
#ifndef OPS
static struct lacp_settings *port_configure_lacp(struct port *,
                                                 struct lacp_settings *);
#endif
static void port_configure_bond(struct port *, struct bond_settings *);
#ifndef OPS_TEMP
static bool port_is_synthetic(const struct port *);
#endif
static void reconfigure_system_stats(const struct ovsrec_open_vswitch *);
static void run_system_stats(void);

#ifndef OPS_TEMP
static void bridge_configure_mirrors(struct bridge *);
static struct mirror *mirror_create(struct bridge *,
                                    const struct ovsrec_mirror *);
static void mirror_destroy(struct mirror *);
static bool mirror_configure(struct mirror *);
static void mirror_refresh_stats(struct mirror *);
#endif
#ifndef OPS
static void iface_configure_lacp(struct iface *, struct lacp_slave_settings *);
#endif
static bool iface_create(struct bridge *, const struct ovsrec_interface *,
                         const struct ovsrec_port *);
static bool iface_is_internal(const struct ovsrec_interface *iface,
                              const struct ovsrec_bridge *br);
static const char *iface_get_type(const struct ovsrec_interface *,
                                  const struct ovsrec_bridge *);
static void iface_destroy(struct iface *);
static void iface_destroy__(struct iface *);
static struct iface *iface_lookup(const struct bridge *, const char *name);
#ifndef OPS_TEMP
static struct iface *iface_find(const char *name);
#endif
static struct iface *iface_from_ofp_port(const struct bridge *,
                                         ofp_port_t ofp_port);
#ifndef OPS_TEMP
static void iface_set_mac(const struct bridge *, const struct port *, struct iface *);
static void iface_set_ofport(const struct ovsrec_interface *, ofp_port_t ofport);
#endif
static void iface_clear_db_record(const struct ovsrec_interface *if_cfg, char *errp);
#ifndef OPS_TEMP
static void iface_configure_qos(struct iface *, const struct ovsrec_qos *);
static void iface_configure_cfm(struct iface *);
static void iface_refresh_cfm_stats(struct iface *);
#endif
static void iface_refresh_stats(struct iface *);
static void iface_refresh_netdev_status(struct iface *);
static void iface_refresh_ofproto_status(struct iface *);
static bool iface_is_synthetic(const struct iface *);
#ifndef OPS_TEMP
static ofp_port_t iface_get_requested_ofp_port(
    const struct ovsrec_interface *);
#endif
static ofp_port_t iface_pick_ofport(const struct ovsrec_interface *);
#ifndef OPS_TEMP
/* Linux VLAN device support (e.g. "eth0.10" for VLAN 10.)
 *
 * This is deprecated.  It is only for compatibility with broken device drivers
 * in old versions of Linux that do not properly support VLANs when VLAN
 * devices are not used.  When broken device drivers are no longer in
 * widespread use, we will delete these interfaces. */

/* True if VLAN splinters are enabled on any interface, false otherwise.*/
static bool vlan_splinters_enabled_anywhere;

static bool vlan_splinters_is_enabled(const struct ovsrec_interface *);
static unsigned long int *collect_splinter_vlans(
    const struct ovsrec_open_vswitch *);
static void configure_splinter_port(struct port *);
static void add_vlan_splinter_ports(struct bridge *,
                                    const unsigned long int *splinter_vlans,
                                    struct shash *ports);
#endif

#ifdef OPS
/* This function waits for SYSd and CONFIGd to complete their system
 * initialization before proceeding.  This means waiting for
 * Open_vSwitch table 'cur_cfg' column to become >= 1.
 */
void
wait_for_config_complete(void)
{
    int system_configured = false;
    const struct ovsrec_open_vswitch *ovs_vsw = NULL;

    while (!ovsdb_idl_has_lock(idl)) {
        ovsdb_idl_run(idl);
        ovsdb_idl_wait(idl);
    }

    while (!system_configured) {
        ovs_vsw = ovsrec_open_vswitch_first(idl);
        system_configured = (ovs_vsw && (ovs_vsw->cur_cfg >= 1));
        if (!system_configured) {
            poll_block();
            ovsdb_idl_run(idl);
            ovsdb_idl_wait(idl);
        } else {
            VLOG_INFO("System is now configured (cur_cfg=%d).",
                      (int)ovs_vsw->cur_cfg);
        }
    }
}
#endif

static void
bridge_init_ofproto(const struct ovsrec_open_vswitch *cfg)
{
    struct shash iface_hints;
    static bool initialized = false;
    int i;

    if (initialized) {
        return;
    }

    shash_init(&iface_hints);

    if (cfg) {
        for (i = 0; i < cfg->n_bridges; i++) {
            const struct ovsrec_bridge *br_cfg = cfg->bridges[i];
            int j;

            for (j = 0; j < br_cfg->n_ports; j++) {
                struct ovsrec_port *port_cfg = br_cfg->ports[j];
                int k;

                for (k = 0; k < port_cfg->n_interfaces; k++) {
                    struct ovsrec_interface *if_cfg = port_cfg->interfaces[k];
                    struct iface_hint *iface_hint;

                    iface_hint = xmalloc(sizeof *iface_hint);
                    iface_hint->br_name = br_cfg->name;
                    iface_hint->br_type = br_cfg->datapath_type;
                    iface_hint->ofp_port = iface_pick_ofport(if_cfg);
                    shash_add(&iface_hints, if_cfg->name, iface_hint);
                }
            }
        }

#ifdef OPS
        for (i = 0; i < cfg->n_vrfs; i++) {
            const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];
            int j;

            for (j = 0; j < vrf_cfg->n_ports; j++) {
                struct ovsrec_port *port_cfg = vrf_cfg->ports[j];
                int k;

                for (k = 0; k < port_cfg->n_interfaces; k++) {
                    struct ovsrec_interface *if_cfg = port_cfg->interfaces[k];
                    struct iface_hint *iface_hint;

                    iface_hint = xmalloc(sizeof *iface_hint);
                    iface_hint->br_name = vrf_cfg->name;
                    iface_hint->br_type = "vrf";
                    iface_hint->ofp_port = iface_pick_ofport(if_cfg);
                    shash_add(&iface_hints, if_cfg->name, iface_hint);
                }
            }
        }

#endif
    }

    ofproto_init(&iface_hints);

    shash_destroy_free_data(&iface_hints);
    initialized = true;
}

/* Public functions. */

/* Initializes the bridge module, configuring it to obtain its configuration
 * from an OVSDB server accessed over 'remote', which should be a string in a
 * form acceptable to ovsdb_idl_create(). */
void
bridge_init(const char *remote)
{
    /* Create connection to database. */
    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, true, true);
    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "ovs_vswitchd");
    ovsdb_idl_verify_write_only(idl);

    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_cur_cfg);
    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_statistics);
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_external_ids);
#ifndef OPS_TEMP
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_ovs_version);
#endif
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_db_version);
#ifndef OPS_TEMP
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_system_type);
    ovsdb_idl_omit(idl, &ovsrec_open_vswitch_col_system_version);
#endif

    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_datapath_id);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_datapath_version);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_status);
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_rstp_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_stp_enable);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_rstp_enable);
#endif
    ovsdb_idl_omit(idl, &ovsrec_bridge_col_external_ids);

    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_status);
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_rstp_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_rstp_statistics);
#endif
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_statistics);
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_bond_active_slave);
    ovsdb_idl_omit(idl, &ovsrec_port_col_external_ids);

    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_admin_state);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_duplex);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_speed);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_state);
#ifdef OPS
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_pause);
    ovsdb_idl_omit_alert(idl, &ovsrec_neighbor_col_status);
#endif
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_resets);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_mac_in_use);
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_ifindex);
#endif
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_mtu);
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_ofport);
#endif
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_statistics);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_status);
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_fault);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_fault_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_remote_mpids);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_flap_count);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_health);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_cfm_remote_opstate);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_bfd_status);
#endif
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_lacp_current);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_error);
    ovsdb_idl_omit(idl, &ovsrec_interface_col_external_ids);
#ifdef OPS
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_hw_intf_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_pm_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_user_config);
#endif
#ifndef OPS_TEMP
    ovsdb_idl_omit_alert(idl, &ovsrec_controller_col_is_connected);
    ovsdb_idl_omit_alert(idl, &ovsrec_controller_col_role);
    ovsdb_idl_omit_alert(idl, &ovsrec_controller_col_status);
    ovsdb_idl_omit(idl, &ovsrec_controller_col_external_ids);

    ovsdb_idl_omit(idl, &ovsrec_qos_col_external_ids);

    ovsdb_idl_omit(idl, &ovsrec_queue_col_external_ids);

    ovsdb_idl_omit(idl, &ovsrec_mirror_col_external_ids);
    ovsdb_idl_omit_alert(idl, &ovsrec_mirror_col_statistics);

    ovsdb_idl_omit(idl, &ovsrec_netflow_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_sflow_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_ipfix_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_flow_sample_collector_set_col_external_ids);
#endif
    ovsdb_idl_omit(idl, &ovsrec_manager_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_inactivity_probe);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_is_connected);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_max_backoff);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_status);

    ovsdb_idl_omit(idl, &ovsrec_ssl_col_external_ids);

#ifdef OPS
    /* VLAN table related. */
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_admin);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_description);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_oper_state);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_oper_state_reason);

    /* Nexthop table */
    ovsdb_idl_omit_alert(idl, &ovsrec_nexthop_col_status);
#endif

#ifdef OPS
    ovsdb_idl_omit(idl, &ovsrec_fan_col_status);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_direction);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_name);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_rpm);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_other_config);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_hw_config);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_fan_col_speed);

    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_status);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_name);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_min);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_fan_state);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_max);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_other_config);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_location);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_hw_config);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_temp_sensor_col_temperature);
#endif

    /* Register unixctl commands. */
#ifndef OPS_TEMP
    unixctl_command_register("qos/show", "interface", 1, 1,
                             qos_unixctl_show, NULL);
#endif
    unixctl_command_register("bridge/dump-flows", "bridge", 1, 1,
                             bridge_unixctl_dump_flows, NULL);
    unixctl_command_register("bridge/reconnect", "[bridge]", 0, 1,
                             bridge_unixctl_reconnect, NULL);
#ifdef OPS
    unixctl_command_register("vlan/show", "[vid]", 0, 1,
                             vlan_unixctl_show, NULL);
#endif
    lacp_init();
    bond_init();
#ifndef OPS_TEMP
    cfm_init();
#endif
    ovs_numa_init();
#ifndef OPS_TEMP
    stp_init();
    rstp_init();
#endif
}

void
bridge_exit(void)
{
    struct bridge *br, *next_br;

    HMAP_FOR_EACH_SAFE (br, next_br, node, &all_bridges) {
        bridge_destroy(br);
    }
    ovsdb_idl_destroy(idl);
}

#ifndef OPS_TEMP
/* Looks at the list of managers in 'ovs_cfg' and extracts their remote IP
 * addresses and ports into '*managersp' and '*n_managersp'.  The caller is
 * responsible for freeing '*managersp' (with free()).
 *
 * You may be asking yourself "why does ovs-vswitchd care?", because
 * ovsdb-server is responsible for connecting to the managers, and ovs-vswitchd
 * should not be and in fact is not directly involved in that.  But
 * ovs-vswitchd needs to make sure that ovsdb-server can reach the managers, so
 * it has to tell in-band control where the managers are to enable that.
 * (Thus, only managers connected in-band are collected.)
 */
static void
collect_in_band_managers(const struct ovsrec_open_vswitch *ovs_cfg,
                         struct sockaddr_in **managersp, size_t *n_managersp)
{
    struct sockaddr_in *managers = NULL;
    size_t n_managers = 0;
    struct sset targets;
    size_t i;

    /* Collect all of the potential targets from the "targets" columns of the
     * rows pointed to by "manager_options", excluding any that are
     * out-of-band. */
    sset_init(&targets);
    for (i = 0; i < ovs_cfg->n_manager_options; i++) {
        struct ovsrec_manager *m = ovs_cfg->manager_options[i];

        if (m->connection_mode && !strcmp(m->connection_mode, "out-of-band")) {
            sset_find_and_delete(&targets, m->target);
        } else {
            sset_add(&targets, m->target);
        }
    }

    /* Now extract the targets' IP addresses. */
    if (!sset_is_empty(&targets)) {
        const char *target;

        managers = xmalloc(sset_count(&targets) * sizeof *managers);
        SSET_FOR_EACH (target, &targets) {
            union {
                struct sockaddr_storage ss;
                struct sockaddr_in in;
            } sa;

            if (stream_parse_target_with_default_port(target, OVSDB_OLD_PORT,
                                                      &sa.ss)
                && sa.ss.ss_family == AF_INET) {
                managers[n_managers++] = sa.in;
            }
        }
    }
    sset_destroy(&targets);

    *managersp = managers;
    *n_managersp = n_managers;
}
#endif

static void
bridge_reconfigure(const struct ovsrec_open_vswitch *ovs_cfg)
{
#ifndef OPS_TEMP
    unsigned long int *splinter_vlans;
    struct sockaddr_in *managers;
#endif
    struct bridge *br, *next;

#ifdef OPS
    struct vrf *vrf, *vrf_next;
    struct blk_params bridge_blk_params;
#else
    int sflow_bridge_number;
    size_t n_managers;
#endif

    COVERAGE_INC(bridge_reconfigure);

#ifdef OPS
    bridge_blk_params.idl = idl;
    bridge_blk_params.ofproto = NULL;
#endif

#ifndef OPS_TEMP
    ofproto_set_flow_limit(smap_get_int(&ovs_cfg->other_config, "flow-limit",
                                        OFPROTO_FLOW_LIMIT_DEFAULT));
    ofproto_set_max_idle(smap_get_int(&ovs_cfg->other_config, "max-idle",
                                      OFPROTO_MAX_IDLE_DEFAULT));
    ofproto_set_n_dpdk_rxqs(smap_get_int(&ovs_cfg->other_config,
                                         "n-dpdk-rxqs", 0));
#endif
    ofproto_set_cpu_mask(smap_get(&ovs_cfg->other_config, "pmd-cpu-mask"));

    ofproto_set_threads(
        smap_get_int(&ovs_cfg->other_config, "n-handler-threads", 0),
        smap_get_int(&ovs_cfg->other_config, "n-revalidator-threads", 0));

    /* Destroy "struct bridge"s, "struct port"s, and "struct iface"s according
     * to 'ovs_cfg', with only very minimal configuration otherwise.
     *
     * This is mostly an update to bridge data structures. Nothing is pushed
     * down to ofproto or lower layers. */
    add_del_bridges(ovs_cfg);

#ifdef OPS
    add_del_vrfs(ovs_cfg);

    /* Execute the reconfigure for block BLK_INIT_RECONFIGURE */
    execute_reconfigure_block(&bridge_blk_params, BLK_INIT_RECONFIGURE);
#endif

#ifndef OPS_TEMP
    splinter_vlans = collect_splinter_vlans(ovs_cfg);
#endif
    HMAP_FOR_EACH (br, node, &all_bridges) {
#ifndef OPS_TEMP
        bridge_collect_wanted_ports(br, splinter_vlans, &br->wanted_ports);
#else
        bridge_collect_wanted_ports(br, NULL, &br->wanted_ports);
#endif

#ifdef OPS
        /* Execute the reconfigure for block BLK_BR_DELETE_PORTS */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_DELETE_PORTS);
#endif
        bridge_del_ports(br, &br->wanted_ports);
    }
#ifndef OPS_TEMP
    free(splinter_vlans);
#endif

#ifdef OPS
    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        vrf_collect_wanted_ports(vrf, &vrf->up->wanted_ports);

        /* Execute the reconfigure for block BLK_VRF_DELETE_PORTS */
        bridge_blk_params.ofproto = vrf->up->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_VRF_DELETE_PORTS);

        /* Inside vrf_del_ports, delete neighbors refering the
        ** deleted ports */

        vrf_del_ports(vrf, &vrf->up->wanted_ports);
    }
#endif
    /* Start pushing configuration changes down to the ofproto layer:
     *
     *   - Delete ofprotos that are no longer configured.
     *
     *   - Delete ports that are no longer configured.
     *
     *   - Reconfigure existing ports to their desired configurations, or
     *     delete them if not possible.
     *
     * We have to do all the deletions before we can do any additions, because
     * the ports to be added might require resources that will be freed up by
     * deletions (they might especially overlap in name). */
    bridge_delete_ofprotos();
    HMAP_FOR_EACH (br, node, &all_bridges) {
        if (br->ofproto) {
            bridge_delete_or_reconfigure_ports(br);

#ifdef OPS
            /* Execute the reconfigure for block BLK_BR_RECONFIGURE_PORTS */
            bridge_blk_params.ofproto = br->ofproto;
            execute_reconfigure_block(&bridge_blk_params, BLK_BR_RECONFIGURE_PORTS);
#endif
        }
    }

#ifdef OPS
    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        if (vrf->up->ofproto) {

            /* Note: Already deleted the neighbors in vrf_del_ports */
            vrf_delete_or_reconfigure_ports(vrf);

            /* Execute the reconfigure for block BLK_VRF_RECONFIGURE_PORTS */
            bridge_blk_params.ofproto = vrf->up->ofproto;
            execute_reconfigure_block(&bridge_blk_params, BLK_VRF_RECONFIGURE_PORTS);
        }
    }
#endif


    /* Finish pushing configuration changes to the ofproto layer:
     *
     *     - Create ofprotos that are missing.
     *
     *     - Add ports that are missing. */
    HMAP_FOR_EACH_SAFE (br, next, node, &all_bridges) {
        if (!br->ofproto) {
            int error;

            error = ofproto_create(br->name, br->type, &br->ofproto);
            if (error) {
                VLOG_ERR("failed to create bridge %s: %s", br->name,
                         ovs_strerror(error));
                shash_destroy(&br->wanted_ports);
                bridge_destroy(br);
            } else {
                /* Trigger storing datapath version. */
                seq_change(connectivity_seq_get());
            }
        }
    }

#ifdef OPS
    HMAP_FOR_EACH_SAFE (vrf, vrf_next, node, &all_vrfs) {
        if (!vrf->up->ofproto) {
            int error;

            error = ofproto_create(vrf->up->name, "vrf", &vrf->up->ofproto);
            if (error) {
                VLOG_ERR("failed to create vrf %s: %s", vrf->up->name,
                         ovs_strerror(error));
                shash_destroy(&vrf->up->wanted_ports);
                vrf_destroy(vrf);
            } else {
                /* Trigger storing datapath version. */
                seq_change(connectivity_seq_get());
            }
        }
    }
#endif
    HMAP_FOR_EACH (br, node, &all_bridges) {
        bridge_add_ports(br, &br->wanted_ports);
#ifdef OPS
        /* Execute the reconfigure for block BLK_BR_ADD_PORTS */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_ADD_PORTS);
#endif
        shash_destroy(&br->wanted_ports);
    }

#ifdef OPS
    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        bridge_add_ports(vrf->up, &vrf->up->wanted_ports);

        /* Execute the reconfigure for block BLK_VRF_ADD_PORTS */
        bridge_blk_params.ofproto = vrf->up->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_VRF_ADD_PORTS);

        shash_destroy(&vrf->up->wanted_ports);
    }
#endif

    reconfigure_system_stats(ovs_cfg);

//CONTINUE

    /* Complete the configuration. */
#ifndef OPS_TEMP
    sflow_bridge_number = 0;
    collect_in_band_managers(ovs_cfg, &managers, &n_managers);
#endif
    HMAP_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        VLOG_DBG("config bridge - %s", br->name);
        /* We need the datapath ID early to allow LACP ports to use it as the
         * default system ID. */
        bridge_configure_datapath_id(br);

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            struct iface *iface;

#ifdef OPS
            /* For a bond port, reconfigure the port if any of the
               member interface rows change. */
            bool port_iface_changed = false;
            LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                if (OVSREC_IDL_IS_ROW_MODIFIED(iface->cfg, idl_seqno)) {
                    port_iface_changed = true;
                    break;
                }
            }
            if (OVSREC_IDL_IS_ROW_MODIFIED(port->cfg, idl_seqno) ||
                (port_iface_changed == true)) {
#endif
                VLOG_DBG("config port - %s", port->name);
                port_configure(port);

#ifndef OPS_TEMP
                LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                    iface_set_ofport(iface->cfg, iface->ofp_port);

                    /* Clear eventual previous errors */
                    ovsrec_interface_set_error(iface->cfg, NULL);

                    iface_configure_cfm(iface);
                    iface_configure_qos(iface, port->cfg->qos);
                    iface_set_mac(br, port, iface);
                    ofproto_port_set_bfd(br->ofproto, iface->ofp_port,
                                     &iface->cfg->bfd);
                }
#endif
#ifdef OPS
            }
#endif
        }
#ifdef OPS
        bridge_configure_vlans(br);
#endif
#ifndef OPS_TEMP
        bridge_configure_mirrors(br);
        bridge_configure_forward_bpdu(br);
#endif
        bridge_configure_mac_table(br);
#ifndef OPS_TEMP
        bridge_configure_mcast_snooping(br);
        bridge_configure_remotes(br, managers, n_managers);
        bridge_configure_netflow(br);
        bridge_configure_sflow(br, &sflow_bridge_number);
        bridge_configure_ipfix(br);
        bridge_configure_stp(br);
        bridge_configure_rstp(br);
        bridge_configure_tables(br);
#endif
        bridge_configure_dp_desc(br);

#ifdef OPS
        /* Execute the reconfigure for block BLK_BR_FEATURE_RECONFIG */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_FEATURE_RECONFIG);

#endif
    }

#ifdef OPS
    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        struct port *port;
        bool   is_port_configured = false;

        VLOG_DBG("config vrf - %s", vrf->up->name);
        HMAP_FOR_EACH (port, hmap_node, &vrf->up->ports) {
            struct iface *iface;

            /* For a bond port, reconfigure the port if any of the
               member interface rows change. */
            bool port_iface_changed = false;
            LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                if (OVSREC_IDL_IS_ROW_MODIFIED(iface->cfg, idl_seqno)) {
                    port_iface_changed = true;
                    break;
                }
            }
            if (OVSREC_IDL_IS_ROW_MODIFIED(port->cfg, idl_seqno) ||
                (port_iface_changed == true)) {
                VLOG_DBG("config port - %s", port->name);
                port_configure(port);

                is_port_configured = true;
            }
        }

        /* Add any exisiting neighbors refering this vrf and ports after
        ** port_configure */
        if( is_port_configured ) {
            vrf_add_neighbors(vrf);

            /* Execute the reconfigure for block BLK_VRF_ADD_NEIGHBORS */
            bridge_blk_params.ofproto = vrf->up->ofproto;
            execute_reconfigure_block(&bridge_blk_params, BLK_VRF_ADD_NEIGHBORS);
        }
        /* Check for any other new addition/deletion/modifications to neighbor
        ** table. */
        vrf_reconfigure_neighbors(vrf);
        vrf_reconfigure_routes(vrf);

        /* Execute the reconfigure for block BLK_RECONFIGURE_NEIGHBORS */
        bridge_blk_params.ofproto = vrf->up->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_RECONFIGURE_NEIGHBORS);

    }
#endif


#ifndef OPS_TEMP
    free(managers);
#endif
    /* The ofproto-dpif provider does some final reconfiguration in its
     * ->type_run() function.  We have to call it before notifying the database
     * client that reconfiguration is complete, otherwise there is a very
     * narrow race window in which e.g. ofproto/trace will not recognize the
     * new configuration (sometimes this causes unit test failures). */
    bridge_run__();
}

/* Delete ofprotos which aren't configured or have the wrong type.  Create
 * ofprotos which don't exist but need to. */
static void
bridge_delete_ofprotos(void)
{
    struct bridge *br;
#ifdef OPS
    struct vrf *vrf;
#endif
    struct sset names;
    struct sset types;
    const char *type;

    /* Delete ofprotos with no bridge or with the wrong type. */
    sset_init(&names);
    sset_init(&types);
    ofproto_enumerate_types(&types);
    SSET_FOR_EACH (type, &types) {
        const char *name;

        ofproto_enumerate_names(type, &names);
        SSET_FOR_EACH (name, &names) {
            br = bridge_lookup(name);
#ifndef OPS
            if (!br || strcmp(type, br->type)) {
                ofproto_delete(name, type);
            }
#else
            vrf = vrf_lookup(name);
            if ((!br || strcmp(type, br->type)) &&
                (!vrf || strcmp(type, "vrf"))) {
                ofproto_delete(name, type);
            }
#endif
        }
    }
    sset_destroy(&names);
    sset_destroy(&types);
}

static ofp_port_t *
add_ofp_port(ofp_port_t port, ofp_port_t *ports, size_t *n, size_t *allocated)
{
    if (*n >= *allocated) {
        ports = x2nrealloc(ports, allocated, sizeof *ports);
    }
    ports[(*n)++] = port;
    return ports;
}

static void
bridge_delete_or_reconfigure_ports(struct bridge *br)
{
    struct ofproto_port ofproto_port;
    struct ofproto_port_dump dump;

    struct sset ofproto_ports;
    struct port *port, *port_next;

    /* List of "ofp_port"s to delete.  We make a list instead of deleting them
     * right away because ofproto implementations aren't necessarily able to
     * iterate through a changing list of ports in an entirely robust way. */
    ofp_port_t *del;
    size_t n, allocated;
    size_t i;

    del = NULL;
    n = allocated = 0;
    sset_init(&ofproto_ports);

    /* Main task: Iterate over the ports in 'br->ofproto' and remove the ports
     * that are not configured in the database.  (This commonly happens when
     * ports have been deleted, e.g. with "ovs-vsctl del-port".)
     *
     * Side tasks: Reconfigure the ports that are still in 'br'.  Delete ports
     * that have the wrong OpenFlow port number (and arrange to add them back
     * with the correct OpenFlow port number). */
    OFPROTO_PORT_FOR_EACH (&ofproto_port, &dump, br->ofproto) {
#ifndef OPS_TEMP
        ofp_port_t requested_ofp_port;
#endif
        struct iface *iface;

        sset_add(&ofproto_ports, ofproto_port.name);

        iface = iface_lookup(br, ofproto_port.name);
        if (!iface) {
            /* No such iface is configured, so we should delete this
             * ofproto_port.
             *
             * As a corner case exception, keep the port if it's a bond fake
             * interface. */
#ifndef OPS_TEMP
            if (bridge_has_bond_fake_iface(br, ofproto_port.name)
                && !strcmp(ofproto_port.type, "internal")) {
                continue;
            }
#endif
            goto delete;
        }

        if  (strcmp(ofproto_port.type, iface->type)
            || netdev_set_config(iface->netdev, &iface->cfg->options, NULL)
            ) {
            /* The interface is the wrong type or can't be configured.
             * Delete it. */
            goto delete;
        }

        /* If the requested OpenFlow port for 'iface' changed, and it's not
         * already the correct port, then we might want to temporarily delete
         * this interface, so we can add it back again with the new OpenFlow
         * port number. */
#ifndef OPS_TEMP
        requested_ofp_port = iface_get_requested_ofp_port(iface->cfg);
        if (iface->ofp_port != OFPP_LOCAL &&
            requested_ofp_port != OFPP_NONE &&
            requested_ofp_port != iface->ofp_port) {
            ofp_port_t victim_request;
            struct iface *victim;

            /* Check for an existing OpenFlow port currently occupying
             * 'iface''s requested port number.  If there isn't one, then
             * delete this port.  Otherwise we need to consider further. */
            victim = iface_from_ofp_port(br, requested_ofp_port);
            if (!victim) {
                goto delete;
            }

            /* 'victim' is a port currently using 'iface''s requested port
             * number.  Unless 'victim' specifically requested that port
             * number, too, then we can delete both 'iface' and 'victim'
             * temporarily.  (We'll add both of them back again later with new
             * OpenFlow port numbers.)
             *
             * If 'victim' did request port number 'requested_ofp_port', just
             * like 'iface', then that's a configuration inconsistency that we
             * can't resolve.  We might as well let it keep its current port
             * number. */
            victim_request = iface_get_requested_ofp_port(victim->cfg);
            if (victim_request != requested_ofp_port) {
                del = add_ofp_port(victim->ofp_port, del, &n, &allocated);
                iface_destroy(victim);
                goto delete;
            }
        }
#endif
        /* Keep it. */
        continue;

    delete:
        iface_destroy(iface);
        del = add_ofp_port(ofproto_port.ofp_port, del, &n, &allocated);
    }
    for (i = 0; i < n; i++) {
        ofproto_port_del(br->ofproto, del[i]);
    }
    free(del);

    /* Iterate over this module's idea of interfaces in 'br'.  Remove any ports
     * that we didn't see when we iterated through the datapath, i.e. ports
     * that disappeared underneath use.  This is an unusual situation, but it
     * can happen in some cases:
     *
     *     - An admin runs a command like "ovs-dpctl del-port" (which is a bad
     *       idea but could happen).
     *
     *     - The port represented a device that disappeared, e.g. a tuntap
     *       device destroyed via "tunctl -d", a physical Ethernet device
     *       whose module was just unloaded via "rmmod", or a virtual NIC for a
     *       VM whose VM was just terminated. */
    HMAP_FOR_EACH_SAFE (port, port_next, hmap_node, &br->ports) {
        struct iface *iface, *iface_next;

        VLOG_DBG("Iterating over port: %s", port->name);
        LIST_FOR_EACH_SAFE (iface, iface_next, port_elem, &port->ifaces) {
            VLOG_DBG("Iterating over interface: %s", iface->name);
            if (!sset_contains(&ofproto_ports, iface->name)) {
                iface_destroy__(iface);
            }
        }

        if (list_is_empty(&port->ifaces)) {
            port_destroy(port);
        }
    }
    sset_destroy(&ofproto_ports);
}

#ifdef OPS
static void
vrf_delete_or_reconfigure_subintf(struct smap *sub_intf_info,
                                  const struct ovsrec_interface *iface_cfg)
{
    const struct ovsrec_interface *parent_intf_cfg = NULL;
    int sub_intf_vlan = 0;

    if (iface_cfg->n_subintf_parent > 0) {
        parent_intf_cfg = iface_cfg->value_subintf_parent[0];
        sub_intf_vlan = iface_cfg->key_subintf_parent[0];
    }

    smap_add(sub_intf_info,
             "parent_intf_name",
             parent_intf_cfg ? parent_intf_cfg->name : "");

    smap_add_format(sub_intf_info, "vlan", "%d", sub_intf_vlan);

    VLOG_DBG("parent_intf_name %s\n", parent_intf_cfg->name);
    VLOG_DBG("vlan %d\n", sub_intf_vlan);
}
#endif

#ifdef OPS
static void
vrf_delete_or_reconfigure_ports(struct vrf *vrf)
{
    struct ofproto_port ofproto_port;
    struct ofproto_port_dump dump;

    struct sset ofproto_ports;
    struct port *port, *port_next;
#ifdef OPS
    struct iface *iface;
    struct smap sub_intf_info;
    int ret = 0;
#endif

    /* List of "ofp_port"s to delete.  We make a list instead of deleting them
     * right away because ofproto implementations aren't necessarily able to
     * iterate through a changing list of ports in an entirely robust way. */
    ofp_port_t *del;
    size_t n, allocated;
    size_t i;

    del = NULL;
    n = allocated = 0;
    sset_init(&ofproto_ports);

    /* Main task: Iterate over the ports in 'br->ofproto' and remove the ports
     * that are not configured in the database.  (This commonly happens when
     * ports have been deleted, e.g. with "ovs-vsctl del-port".)
     *
     * Side tasks: Reconfigure the ports that are still in 'br'.  Delete ports
     * that have the wrong OpenFlow port number (and arrange to add them back
     * with the correct OpenFlow port number). */
    OFPROTO_PORT_FOR_EACH (&ofproto_port, &dump, vrf->up->ofproto) {
#ifndef OPS_TEMP
        ofp_port_t requested_ofp_port;
#endif
        sset_add(&ofproto_ports, ofproto_port.name);

        iface = iface_lookup(vrf->up, ofproto_port.name);
        if (!iface) {
            /* No such iface is configured, so we should delete this
             * ofproto_port. */
            goto delete;
        }
#ifdef OPS
        if (!strcmp(iface->cfg->type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
           smap_init(&sub_intf_info);
           vrf_delete_or_reconfigure_subintf(&sub_intf_info, iface->cfg);
           ret = netdev_set_config(iface->netdev, &sub_intf_info, NULL);
           smap_destroy(&sub_intf_info);
           if (ret)
              goto delete;
           continue;
        }
#endif

        if  (strcmp(ofproto_port.type, iface->type)
            || netdev_set_config(iface->netdev, &iface->cfg->options, NULL)
            ) {
            /* The interface is the wrong type or can't be configured.
             * Delete it. */
            goto delete;
        }

        /* Keep it. */
        continue;

    delete:
        iface_destroy(iface);
        del = add_ofp_port(ofproto_port.ofp_port, del, &n, &allocated);
    }
    for (i = 0; i < n; i++) {
        ofproto_port_del(vrf->up->ofproto, del[i]);
    }
    free(del);

    /* Iterate over this module's idea of interfaces in 'br'.  Remove any ports
     * that we didn't see when we iterated through the datapath, i.e. ports
     * that disappeared underneath use.  This is an unusual situation, but it
     * can happen in some cases:
     *
     *     - An admin runs a command like "ovs-dpctl del-port" (which is a bad
     *       idea but could happen).
     *
     *     - The port represented a device that disappeared, e.g. a tuntap
     *       device destroyed via "tunctl -d", a physical Ethernet device
     *       whose module was just unloaded via "rmmod", or a virtual NIC for a
     *       VM whose VM was just terminated. */
    HMAP_FOR_EACH_SAFE (port, port_next, hmap_node, &vrf->up->ports) {
        struct iface *iface, *iface_next;

        VLOG_DBG("Iterating over port: %s", port->name);
        LIST_FOR_EACH_SAFE (iface, iface_next, port_elem, &port->ifaces) {
            VLOG_DBG("Iterating over interface: %s", iface->name);
            if (!sset_contains(&ofproto_ports, iface->name)) {
                iface_destroy__(iface);
            }
        }

        if (list_is_empty(&port->ifaces)) {
            port_destroy(port);
        }
    }
    sset_destroy(&ofproto_ports);
}
#endif

static void
bridge_add_ports__(struct bridge *br, const struct shash *wanted_ports
#ifndef OPS_TEMP
                   , bool with_requested_port
#endif
    )
{
    struct shash_node *port_node;

    SHASH_FOR_EACH (port_node, wanted_ports) {
        const struct ovsrec_port *port_cfg = port_node->data;
        size_t i;

        VLOG_DBG("bridge_add_ports__ adding port %s", port_node->name);
        for (i = 0; i < port_cfg->n_interfaces; i++) {
            const struct ovsrec_interface *iface_cfg = port_cfg->interfaces[i];
#ifndef OPS_TEMP
            ofp_port_t requested_ofp_port;

            requested_ofp_port = iface_get_requested_ofp_port(iface_cfg);
            if ((requested_ofp_port != OFPP_NONE) == with_requested_port) {
#endif
                struct iface *iface = iface_lookup(br, iface_cfg->name);

                if (!iface) {
                    iface_create(br, iface_cfg, port_cfg);
                }
#ifndef OPS_TEMP
            }
#endif
        }
    }
}

static void
bridge_add_ports(struct bridge *br, const struct shash *wanted_ports)
{
#ifndef OPS_TEMP
    /* First add interfaces that request a particular port number. */
    bridge_add_ports__(br, wanted_ports, true);
#endif
    /* Then add interfaces that want automatic port number assignment.
     * We add these afterward to avoid accidentally taking a specifically
     * requested port number. */
#ifndef OPS_TEMP
    bridge_add_ports__(br, wanted_ports, false);
#else
    bridge_add_ports__(br, wanted_ports);
#endif
}

static void
port_configure(struct port *port)
{
    const struct ovsrec_port *cfg = port->cfg;
    struct bond_settings bond_settings;
#ifndef OPS
    struct lacp_settings lacp_settings;
#endif
    struct ofproto_bundle_settings s;
    struct iface *iface;
#ifdef OPS
    int prev_bond_handle = port->bond_hw_handle;
    int cfg_slave_count;
    bool lacp_enabled = false;
    bool lacp_active = false;   /* Not used. */
#endif
#ifndef OPS_TEMP
    if (cfg->vlan_mode && !strcmp(cfg->vlan_mode, "splinter")) {
        configure_splinter_port(port);
        return;
    }
#endif
    /* Get name. */
    s.name = port->name;

    /* Get slaves. */
    s.n_slaves = 0;
    s.slaves = xmalloc(list_size(&port->ifaces) * sizeof *s.slaves);
#ifdef OPS
    cfg_slave_count = list_size(&port->ifaces);
    s.slaves_entered = cfg_slave_count;
    s.n_slaves_tx_enable = 0;
    s.slaves_tx_enable = xmalloc(cfg_slave_count * sizeof *s.slaves);

    s.enable = smap_get_bool(&cfg->hw_config,
            PORT_HW_CONFIG_MAP_ENABLE,
            PORT_HW_CONFIG_MAP_ENABLE_DEFAULT);

    /* Determine if bond mode is dynamic (LACP). */
    lacp_enabled  = enable_lacp(port, &lacp_active);
#endif
    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
#ifndef OPS_TEMP
        s.slaves[s.n_slaves++] = iface->ofp_port;
#else
        if ((cfg_slave_count > 1) || lacp_enabled) {
            /* Static LAG with 2 or more interfaces, or LACP has been enabled
             * for this bond.  A bond should exist in h/w. */
            s.hw_bond_should_exist = true;

            /* Add only the interfaces with hw_bond_config:rx_enabled set. */
            if (smap_get_bool(&iface->cfg->hw_bond_config,
                              INTERFACE_HW_BOND_CONFIG_MAP_RX_ENABLED,
                              false)) {
                s.slaves[s.n_slaves++] = iface->ofp_port;
            }
            if (smap_get_bool(&iface->cfg->hw_bond_config,
                              INTERFACE_HW_BOND_CONFIG_MAP_TX_ENABLED,
                              false)) {
                s.slaves_tx_enable[s.n_slaves_tx_enable++] = iface->ofp_port;
            }
        } else {
            /* Port has only one interface and not running LACP.
             * Need to destroy LAG in h/w if it was created.
             * E.g. static LAG previously with 2 or more interfaces
             * now only has 1 interface need to have LAG destroyed. */
            s.hw_bond_should_exist = false;
            s.slaves[s.n_slaves++] = iface->ofp_port;
        }
#endif
    }
#ifdef OPS
    VLOG_DBG("port %s has %d configured interfaces, %d eligible "
             "interfaces, lacp_enabled=%d",
             s.name, cfg_slave_count, (int)s.n_slaves, lacp_enabled);
    s.bond_handle_alloc_only = false;
    if (((cfg_slave_count > 1) && (s.n_slaves < 1)) ||
        (lacp_enabled && (s.n_slaves < 1))) {
        if (port->bond_hw_handle == -1) {
            s.bond_handle_alloc_only = true;
        }
    }
#endif
    /* Get VLAN tag. */
    s.vlan = -1;
#ifdef OPS
    if (cfg->tag && *cfg->tag >= 1 && *cfg->tag <= 4094) {
#else
    if (cfg->tag && *cfg->tag >= 0 && *cfg->tag <= 4095) {
#endif
        s.vlan = *cfg->tag;
    }
    VLOG_DBG("Configure port %s on vlan %d", s.name, s.vlan);

    /* Get VLAN trunks. */
    s.trunks = NULL;
    if (cfg->n_trunks) {
        s.trunks = vlan_bitmap_from_array(cfg->trunks, cfg->n_trunks);
    }

    /* Get VLAN mode. */
    if (cfg->vlan_mode) {
        if (!strcmp(cfg->vlan_mode, "access")) {
            s.vlan_mode = PORT_VLAN_ACCESS;
        } else if (!strcmp(cfg->vlan_mode, "trunk")) {
            s.vlan_mode = PORT_VLAN_TRUNK;
        } else if (!strcmp(cfg->vlan_mode, "native-tagged")) {
            s.vlan_mode = PORT_VLAN_NATIVE_TAGGED;
        } else if (!strcmp(cfg->vlan_mode, "native-untagged")) {
            s.vlan_mode = PORT_VLAN_NATIVE_UNTAGGED;
        } else {
            /* This "can't happen" because ovsdb-server should prevent it. */
            VLOG_WARN("port %s: unknown VLAN mode %s, falling "
                      "back to trunk mode", port->name, cfg->vlan_mode);
            s.vlan_mode = PORT_VLAN_TRUNK;
        }
    } else {
        if (s.vlan >= 0) {
            s.vlan_mode = PORT_VLAN_ACCESS;
            if (cfg->n_trunks) {
                VLOG_WARN("port %s: ignoring trunks in favor of implicit vlan",
                          port->name);
            }
        } else {
            s.vlan_mode = PORT_VLAN_TRUNK;
        }
    }
#ifdef OPS
    /* If port is in TRUNK mode, VLAN tag needs to be ignored. */
    if (s.vlan_mode == PORT_VLAN_TRUNK) {
        s.vlan = -1;
    }
#endif
    s.use_priority_tags = smap_get_bool(&cfg->other_config, "priority-tags",
                                        false);

/* For OPS, LACP support is handled by lacpd. */
#ifndef OPS
    /* Get LACP settings. */
    s.lacp = port_configure_lacp(port, &lacp_settings);
    if (s.lacp) {
        size_t i = 0;

        s.lacp_slaves = xmalloc(s.n_slaves * sizeof *s.lacp_slaves);
        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            iface_configure_lacp(iface, &s.lacp_slaves[i++]);
        }
    } else {
        s.lacp_slaves = NULL;
    }
#endif

    /* Get bond settings. */
    if (s.n_slaves > 1) {
        s.bond = &bond_settings;
        port_configure_bond(port, &bond_settings);
    } else {
        s.bond = NULL;
        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            netdev_set_miimon_interval(iface->netdev, 0);
        }
    }

#ifdef OPS_TEMP
    /* Setup port configuration option array and save
       its address in bundle setting */
    s.port_options[PORT_OPT_VLAN] = &cfg->vlan_options;
    s.port_options[PORT_OPT_BOND] = &cfg->bond_options;
    s.port_options[PORT_HW_CONFIG] = &cfg->hw_config;
#endif

#ifdef OPS
    /* Check for port L3 ip changes */
    vrf_port_reconfig_ipaddr(port, &s);
#endif

    /* Register. */
    ofproto_bundle_register(port->bridge->ofproto, port, &s);
#ifdef OPS
    ofproto_bundle_get(port->bridge->ofproto, port, &port->bond_hw_handle);
    if (prev_bond_handle != port->bond_hw_handle) {
        struct smap smap;

        /* Write the bond handle to port's status column if
           handle is valid.  Otherwise, remove it. */
        smap_clone(&smap, &port->cfg->status);
        if (port->bond_hw_handle != -1) {
            char buf[20];
            snprintf(buf, sizeof(buf), "%d", port->bond_hw_handle);
            smap_replace(&smap, PORT_STATUS_BOND_HW_HANDLE, buf);
        } else {
            smap_remove(&smap, PORT_STATUS_BOND_HW_HANDLE);
        }
        ovsrec_port_set_status(port->cfg, &smap);
        smap_destroy(&smap);
    }
#endif
    /* Clean up. */
    free(s.slaves);
#ifdef OPS
    free(s.slaves_tx_enable);
#endif
    free(s.trunks);
#ifndef OPS
    free(s.lacp_slaves);
#endif
}

/* Pick local port hardware address and datapath ID for 'br'. */
static void
bridge_configure_datapath_id(struct bridge *br)
{
    struct eth_addr ea;
    uint64_t dpid;
    struct iface *local_iface;
    struct iface *hw_addr_iface;
    char *dpid_string;

    bridge_pick_local_hw_addr(br, &ea, &hw_addr_iface);
    local_iface = iface_from_ofp_port(br, OFPP_LOCAL);
    if (local_iface) {
        int error = netdev_set_etheraddr(local_iface->netdev, ea);
        if (error) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
            VLOG_ERR_RL(&rl, "bridge %s: failed to set bridge "
                        "Ethernet address: %s",
                        br->name, ovs_strerror(error));
        }
    }
    br->ea = ea;

    dpid = bridge_pick_datapath_id(br, ea, hw_addr_iface);
    if (dpid != ofproto_get_datapath_id(br->ofproto)) {
        VLOG_DBG("bridge %s: using datapath ID %016"PRIx64, br->name, dpid);
        ofproto_set_datapath_id(br->ofproto, dpid);
    }

    dpid_string = xasprintf("%016"PRIx64, dpid);
    ovsrec_bridge_set_datapath_id(br->cfg, dpid_string);
    free(dpid_string);
}

#ifndef OPS_TEMP
/* Returns a bitmap of "enum ofputil_protocol"s that are allowed for use with
 * 'br'. */
static uint32_t
bridge_get_allowed_versions(struct bridge *br)
{
    if (!br->cfg->n_protocols)
        return 0;

    return ofputil_versions_from_strings(br->cfg->protocols,
                                         br->cfg->n_protocols);
}

/* Set NetFlow configuration on 'br'. */
static void
bridge_configure_netflow(struct bridge *br)
{
    struct ovsrec_netflow *cfg = br->cfg->netflow;
    struct netflow_options opts;

    if (!cfg) {
        ofproto_set_netflow(br->ofproto, NULL);
        return;
    }

    memset(&opts, 0, sizeof opts);

    /* Get default NetFlow configuration from datapath.
     * Apply overrides from 'cfg'. */
    ofproto_get_netflow_ids(br->ofproto, &opts.engine_type, &opts.engine_id);
    if (cfg->engine_type) {
        opts.engine_type = *cfg->engine_type;
    }
    if (cfg->engine_id) {
        opts.engine_id = *cfg->engine_id;
    }

    /* Configure active timeout interval. */
    opts.active_timeout = cfg->active_timeout;
    if (!opts.active_timeout) {
        opts.active_timeout = -1;
    } else if (opts.active_timeout < 0) {
        VLOG_WARN("bridge %s: active timeout interval set to negative "
                  "value, using default instead (%d seconds)", br->name,
                  NF_ACTIVE_TIMEOUT_DEFAULT);
        opts.active_timeout = -1;
    }

    /* Add engine ID to interface number to disambiguate bridgs? */
    opts.add_id_to_iface = cfg->add_id_to_interface;
    if (opts.add_id_to_iface) {
        if (opts.engine_id > 0x7f) {
            VLOG_WARN("bridge %s: NetFlow port mangling may conflict with "
                      "another vswitch, choose an engine id less than 128",
                      br->name);
        }
        if (hmap_count(&br->ports) > 508) {
            VLOG_WARN("bridge %s: NetFlow port mangling will conflict with "
                      "another port when more than 508 ports are used",
                      br->name);
        }
    }

    /* Collectors. */
    sset_init(&opts.collectors);
    sset_add_array(&opts.collectors, cfg->targets, cfg->n_targets);

    /* Configure. */
    if (ofproto_set_netflow(br->ofproto, &opts)) {
        VLOG_ERR("bridge %s: problem setting netflow collectors", br->name);
    }
    sset_destroy(&opts.collectors);
}

/* Set sFlow configuration on 'br'. */
static void
bridge_configure_sflow(struct bridge *br, int *sflow_bridge_number)
{
    const struct ovsrec_sflow *cfg = br->cfg->sflow;
    struct ovsrec_controller **controllers;
    struct ofproto_sflow_options oso;
    size_t n_controllers;
    size_t i;

    if (!cfg) {
        ofproto_set_sflow(br->ofproto, NULL);
        return;
    }

    memset(&oso, 0, sizeof oso);

    sset_init(&oso.targets);
    sset_add_array(&oso.targets, cfg->targets, cfg->n_targets);

    oso.sampling_rate = SFL_DEFAULT_SAMPLING_RATE;
    if (cfg->sampling) {
        oso.sampling_rate = *cfg->sampling;
    }

    oso.polling_interval = SFL_DEFAULT_POLLING_INTERVAL;
    if (cfg->polling) {
        oso.polling_interval = *cfg->polling;
    }

    oso.header_len = SFL_DEFAULT_HEADER_SIZE;
    if (cfg->header) {
        oso.header_len = *cfg->header;
    }

    oso.sub_id = (*sflow_bridge_number)++;
    oso.agent_device = cfg->agent;

    oso.control_ip = NULL;
    n_controllers = bridge_get_controllers(br, &controllers);
    for (i = 0; i < n_controllers; i++) {
        if (controllers[i]->local_ip) {
            oso.control_ip = controllers[i]->local_ip;
            break;
        }
    }
    ofproto_set_sflow(br->ofproto, &oso);
    sset_destroy(&oso.targets);
}

/* Returns whether a IPFIX row is valid. */
static bool
ovsrec_ipfix_is_valid(const struct ovsrec_ipfix *ipfix)
{
    return ipfix && ipfix->n_targets > 0;
}

/* Returns whether a Flow_Sample_Collector_Set row is valid. */
static bool
ovsrec_fscs_is_valid(const struct ovsrec_flow_sample_collector_set *fscs,
                     const struct bridge *br)
{
    return ovsrec_ipfix_is_valid(fscs->ipfix) && fscs->bridge == br->cfg;
}

/* Set IPFIX configuration on 'br'. */
static void
bridge_configure_ipfix(struct bridge *br)
{
    const struct ovsrec_ipfix *be_cfg = br->cfg->ipfix;
    bool valid_be_cfg = ovsrec_ipfix_is_valid(be_cfg);
    const struct ovsrec_flow_sample_collector_set *fe_cfg;
    struct ofproto_ipfix_bridge_exporter_options be_opts;
    struct ofproto_ipfix_flow_exporter_options *fe_opts = NULL;
    size_t n_fe_opts = 0;

    OVSREC_FLOW_SAMPLE_COLLECTOR_SET_FOR_EACH(fe_cfg, idl) {
        if (ovsrec_fscs_is_valid(fe_cfg, br)) {
            n_fe_opts++;
        }
    }

    if (!valid_be_cfg && n_fe_opts == 0) {
        ofproto_set_ipfix(br->ofproto, NULL, NULL, 0);
        return;
    }

    if (valid_be_cfg) {
        memset(&be_opts, 0, sizeof be_opts);

        sset_init(&be_opts.targets);
        sset_add_array(&be_opts.targets, be_cfg->targets, be_cfg->n_targets);

        if (be_cfg->sampling) {
            be_opts.sampling_rate = *be_cfg->sampling;
        } else {
            be_opts.sampling_rate = SFL_DEFAULT_SAMPLING_RATE;
        }
        if (be_cfg->obs_domain_id) {
            be_opts.obs_domain_id = *be_cfg->obs_domain_id;
        }
        if (be_cfg->obs_point_id) {
            be_opts.obs_point_id = *be_cfg->obs_point_id;
        }
        if (be_cfg->cache_active_timeout) {
            be_opts.cache_active_timeout = *be_cfg->cache_active_timeout;
        }
        if (be_cfg->cache_max_flows) {
            be_opts.cache_max_flows = *be_cfg->cache_max_flows;
        }

        be_opts.enable_tunnel_sampling = smap_get_bool(&be_cfg->other_config,
                                             "enable-tunnel-sampling", true);

        be_opts.enable_input_sampling = !smap_get_bool(&be_cfg->other_config,
                                              "enable-input-sampling", false);

        be_opts.enable_output_sampling = !smap_get_bool(&be_cfg->other_config,
                                              "enable-output-sampling", false);
    }

    if (n_fe_opts > 0) {
        struct ofproto_ipfix_flow_exporter_options *opts;
        fe_opts = xcalloc(n_fe_opts, sizeof *fe_opts);
        opts = fe_opts;
        OVSREC_FLOW_SAMPLE_COLLECTOR_SET_FOR_EACH(fe_cfg, idl) {
            if (ovsrec_fscs_is_valid(fe_cfg, br)) {
                opts->collector_set_id = fe_cfg->id;
                sset_init(&opts->targets);
                sset_add_array(&opts->targets, fe_cfg->ipfix->targets,
                               fe_cfg->ipfix->n_targets);
                opts->cache_active_timeout = fe_cfg->ipfix->cache_active_timeout
                    ? *fe_cfg->ipfix->cache_active_timeout : 0;
                opts->cache_max_flows = fe_cfg->ipfix->cache_max_flows
                    ? *fe_cfg->ipfix->cache_max_flows : 0;
                opts++;
            }
        }
    }

    ofproto_set_ipfix(br->ofproto, valid_be_cfg ? &be_opts : NULL, fe_opts,
                      n_fe_opts);

    if (valid_be_cfg) {
        sset_destroy(&be_opts.targets);
    }

    if (n_fe_opts > 0) {
        struct ofproto_ipfix_flow_exporter_options *opts = fe_opts;
        size_t i;
        for (i = 0; i < n_fe_opts; i++) {
            sset_destroy(&opts->targets);
            opts++;
        }
        free(fe_opts);
    }
}

static void
port_configure_stp(const struct ofproto *ofproto, struct port *port,
                   struct ofproto_port_stp_settings *port_s,
                   int *port_num_counter, unsigned long *port_num_bitmap)
{
    const char *config_str;
    struct iface *iface;

    if (!smap_get_bool(&port->cfg->other_config, "stp-enable", true)) {
        port_s->enable = false;
        return;
    } else {
        port_s->enable = true;
    }

    /* STP over bonds is not supported. */
    if (!list_is_singleton(&port->ifaces)) {
        VLOG_ERR("port %s: cannot enable STP on bonds, disabling",
                 port->name);
        port_s->enable = false;
        return;
    }

    iface = CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);

    /* Internal ports shouldn't participate in spanning tree, so
     * skip them. */
    if (!strcmp(iface->type, "internal")) {
        VLOG_DBG("port %s: disable STP on internal ports", port->name);
        port_s->enable = false;
        return;
    }

    /* STP on mirror output ports is not supported. */
    if (ofproto_is_mirror_output_bundle(ofproto, port)) {
        VLOG_DBG("port %s: disable STP on mirror ports", port->name);
        port_s->enable = false;
        return;
    }

    config_str = smap_get(&port->cfg->other_config, "stp-port-num");
    if (config_str) {
        unsigned long int port_num = strtoul(config_str, NULL, 0);
        int port_idx = port_num - 1;

        if (port_num < 1 || port_num > STP_MAX_PORTS) {
            VLOG_ERR("port %s: invalid stp-port-num", port->name);
            port_s->enable = false;
            return;
        }

        if (bitmap_is_set(port_num_bitmap, port_idx)) {
            VLOG_ERR("port %s: duplicate stp-port-num %lu, disabling",
                    port->name, port_num);
            port_s->enable = false;
            return;
        }
        bitmap_set1(port_num_bitmap, port_idx);
        port_s->port_num = port_idx;
    } else {
        if (*port_num_counter >= STP_MAX_PORTS) {
            VLOG_ERR("port %s: too many STP ports, disabling", port->name);
            port_s->enable = false;
            return;
        }

        port_s->port_num = (*port_num_counter)++;
    }

    config_str = smap_get(&port->cfg->other_config, "stp-path-cost");
    if (config_str) {
        port_s->path_cost = strtoul(config_str, NULL, 10);
    } else {
        enum netdev_features current;
        unsigned int mbps;

        netdev_get_features(iface->netdev, &current, NULL, NULL, NULL);
        mbps = netdev_features_to_bps(current, 100 * 1000 * 1000) / 1000000;
        port_s->path_cost = stp_convert_speed_to_cost(mbps);
    }

    config_str = smap_get(&port->cfg->other_config, "stp-port-priority");
    if (config_str) {
        port_s->priority = strtoul(config_str, NULL, 0);
    } else {
        port_s->priority = STP_DEFAULT_PORT_PRIORITY;
    }
}

static void
port_configure_rstp(const struct ofproto *ofproto, struct port *port,
        struct ofproto_port_rstp_settings *port_s, int *port_num_counter)
{
    const char *config_str;
    struct iface *iface;

    if (!smap_get_bool(&port->cfg->other_config, "rstp-enable", true)) {
        port_s->enable = false;
        return;
    } else {
        port_s->enable = true;
    }

    /* RSTP over bonds is not supported. */
    if (!list_is_singleton(&port->ifaces)) {
        VLOG_ERR("port %s: cannot enable RSTP on bonds, disabling",
                port->name);
        port_s->enable = false;
        return;
    }

    iface = CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);

    /* Internal ports shouldn't participate in spanning tree, so
     * skip them. */
    if (!strcmp(iface->type, "internal")) {
        VLOG_DBG("port %s: disable RSTP on internal ports", port->name);
        port_s->enable = false;
        return;
    }

    /* RSTP on mirror output ports is not supported. */
    if (ofproto_is_mirror_output_bundle(ofproto, port)) {
        VLOG_DBG("port %s: disable RSTP on mirror ports", port->name);
        port_s->enable = false;
        return;
    }

    config_str = smap_get(&port->cfg->other_config, "rstp-port-num");
    if (config_str) {
        unsigned long int port_num = strtoul(config_str, NULL, 0);
        if (port_num < 1 || port_num > RSTP_MAX_PORTS) {
            VLOG_ERR("port %s: invalid rstp-port-num", port->name);
            port_s->enable = false;
            return;
        }
        port_s->port_num = port_num;
    } else {
        if (*port_num_counter >= RSTP_MAX_PORTS) {
            VLOG_ERR("port %s: too many RSTP ports, disabling", port->name);
            port_s->enable = false;
            return;
        }
        /* If rstp-port-num is not specified, use 0.
         * rstp_port_set_port_number() will look for the first free one. */
        port_s->port_num = 0;
    }

    config_str = smap_get(&port->cfg->other_config, "rstp-path-cost");
    if (config_str) {
        port_s->path_cost = strtoul(config_str, NULL, 10);
    } else {
        enum netdev_features current;
        unsigned int mbps;

        netdev_get_features(iface->netdev, &current, NULL, NULL, NULL);
        mbps = netdev_features_to_bps(current, 100 * 1000 * 1000) / 1000000;
        port_s->path_cost = rstp_convert_speed_to_cost(mbps);
    }

    config_str = smap_get(&port->cfg->other_config, "rstp-port-priority");
    if (config_str) {
        port_s->priority = strtoul(config_str, NULL, 0);
    } else {
        port_s->priority = RSTP_DEFAULT_PORT_PRIORITY;
    }

    config_str = smap_get(&port->cfg->other_config, "rstp-admin-p2p-mac");
    if (config_str) {
        port_s->admin_p2p_mac_state = strtoul(config_str, NULL, 0);
    } else {
        port_s->admin_p2p_mac_state = RSTP_ADMIN_P2P_MAC_FORCE_TRUE;
    }

    port_s->admin_port_state = smap_get_bool(&port->cfg->other_config,
                                             "rstp-admin-port-state", true);

    port_s->admin_edge_port = smap_get_bool(&port->cfg->other_config,
                                            "rstp-port-admin-edge", false);
    port_s->auto_edge = smap_get_bool(&port->cfg->other_config,
                                      "rstp-port-auto-edge", true);
    port_s->mcheck = smap_get_bool(&port->cfg->other_config,
                                   "rstp-port-mcheck", false);
}

/* Set spanning tree configuration on 'br'. */
static void
bridge_configure_stp(struct bridge *br)
{
    struct ofproto_rstp_status rstp_status;

    ofproto_get_rstp_status(br->ofproto, &rstp_status);
    if (!br->cfg->stp_enable) {
        ofproto_set_stp(br->ofproto, NULL);
    } else if (rstp_status.enabled) {
        /* Do not activate STP if RSTP is enabled. */
        VLOG_ERR("STP cannot be enabled if RSTP is running.");
        ofproto_set_stp(br->ofproto, NULL);
        ovsrec_bridge_set_stp_enable(br->cfg, false);
    } else {
        struct ofproto_stp_settings br_s;
        const char *config_str;
        struct port *port;
        int port_num_counter;
        unsigned long *port_num_bitmap;

        config_str = smap_get(&br->cfg->other_config, "stp-system-id");
        if (config_str) {
            uint8_t ea[ETH_ADDR_LEN];

            if (eth_addr_from_string(config_str, ea)) {
                br_s.system_id = eth_addr_to_uint64(ea);
            } else {
                br_s.system_id = eth_addr_to_uint64(br->ea);
                VLOG_ERR("bridge %s: invalid stp-system-id, defaulting "
                         "to "ETH_ADDR_FMT, br->name, ETH_ADDR_ARGS(br->ea));
            }
        } else {
            br_s.system_id = eth_addr_to_uint64(br->ea);
        }

        config_str = smap_get(&br->cfg->other_config, "stp-priority");
        if (config_str) {
            br_s.priority = strtoul(config_str, NULL, 0);
        } else {
            br_s.priority = STP_DEFAULT_BRIDGE_PRIORITY;
        }

        config_str = smap_get(&br->cfg->other_config, "stp-hello-time");
        if (config_str) {
            br_s.hello_time = strtoul(config_str, NULL, 10) * 1000;
        } else {
            br_s.hello_time = STP_DEFAULT_HELLO_TIME;
        }

        config_str = smap_get(&br->cfg->other_config, "stp-max-age");
        if (config_str) {
            br_s.max_age = strtoul(config_str, NULL, 10) * 1000;
        } else {
            br_s.max_age = STP_DEFAULT_MAX_AGE;
        }

        config_str = smap_get(&br->cfg->other_config, "stp-forward-delay");
        if (config_str) {
            br_s.fwd_delay = strtoul(config_str, NULL, 10) * 1000;
        } else {
            br_s.fwd_delay = STP_DEFAULT_FWD_DELAY;
        }

        /* Configure STP on the bridge. */
        if (ofproto_set_stp(br->ofproto, &br_s)) {
            VLOG_ERR("bridge %s: could not enable STP", br->name);
            return;
        }

        /* Users must either set the port number with the "stp-port-num"
         * configuration on all ports or none.  If manual configuration
         * is not done, then we allocate them sequentially. */
        port_num_counter = 0;
        port_num_bitmap = bitmap_allocate(STP_MAX_PORTS);
        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            struct ofproto_port_stp_settings port_s;
            struct iface *iface;

            port_configure_stp(br->ofproto, port, &port_s,
                               &port_num_counter, port_num_bitmap);

            /* As bonds are not supported, just apply configuration to
             * all interfaces. */
            LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                if (ofproto_port_set_stp(br->ofproto, iface->ofp_port,
                                         &port_s)) {
                    VLOG_ERR("port %s: could not enable STP", port->name);
                    continue;
                }
            }
        }

        if (bitmap_scan(port_num_bitmap, 1, 0, STP_MAX_PORTS) != STP_MAX_PORTS
                    && port_num_counter) {
            VLOG_ERR("bridge %s: must manually configure all STP port "
                     "IDs or none, disabling", br->name);
            ofproto_set_stp(br->ofproto, NULL);
        }
        bitmap_free(port_num_bitmap);
    }
}

static void
bridge_configure_rstp(struct bridge *br)
{
    struct ofproto_stp_status stp_status;

    ofproto_get_stp_status(br->ofproto, &stp_status);
    if (!br->cfg->rstp_enable) {
        ofproto_set_rstp(br->ofproto, NULL);
    } else if (stp_status.enabled) {
        /* Do not activate RSTP if STP is enabled. */
        VLOG_ERR("RSTP cannot be enabled if STP is running.");
        ofproto_set_rstp(br->ofproto, NULL);
        ovsrec_bridge_set_rstp_enable(br->cfg, false);
    } else {
        struct ofproto_rstp_settings br_s;
        const char *config_str;
        struct port *port;
        int port_num_counter;

        config_str = smap_get(&br->cfg->other_config, "rstp-address");
        if (config_str) {
            uint8_t ea[ETH_ADDR_LEN];

            if (eth_addr_from_string(config_str, ea)) {
                br_s.address = eth_addr_to_uint64(ea);
            }
            else {
                br_s.address = eth_addr_to_uint64(br->ea);
                VLOG_ERR("bridge %s: invalid rstp-address, defaulting "
                        "to "ETH_ADDR_FMT, br->name, ETH_ADDR_ARGS(br->ea));
            }
        }
        else {
            br_s.address = eth_addr_to_uint64(br->ea);
        }

        config_str = smap_get(&br->cfg->other_config, "rstp-priority");
        if (config_str) {
            br_s.priority = strtoul(config_str, NULL, 0);
        } else {
            br_s.priority = RSTP_DEFAULT_PRIORITY;
        }

        config_str = smap_get(&br->cfg->other_config, "rstp-ageing-time");
        if (config_str) {
            br_s.ageing_time = strtoul(config_str, NULL, 0);
        } else {
            br_s.ageing_time = RSTP_DEFAULT_AGEING_TIME;
        }

        config_str = smap_get(&br->cfg->other_config,
                              "rstp-force-protocol-version");
        if (config_str) {
            br_s.force_protocol_version = strtoul(config_str, NULL, 0);
        } else {
            br_s.force_protocol_version = FPV_DEFAULT;
        }

        config_str = smap_get(&br->cfg->other_config, "rstp-max-age");
        if (config_str) {
            br_s.bridge_max_age = strtoul(config_str, NULL, 10);
        } else {
            br_s.bridge_max_age = RSTP_DEFAULT_BRIDGE_MAX_AGE;
        }

        config_str = smap_get(&br->cfg->other_config, "rstp-forward-delay");
        if (config_str) {
            br_s.bridge_forward_delay = strtoul(config_str, NULL, 10);
        } else {
            br_s.bridge_forward_delay = RSTP_DEFAULT_BRIDGE_FORWARD_DELAY;
        }

        config_str = smap_get(&br->cfg->other_config,
                              "rstp-transmit-hold-count");
        if (config_str) {
            br_s.transmit_hold_count = strtoul(config_str, NULL, 10);
        } else {
            br_s.transmit_hold_count = RSTP_DEFAULT_TRANSMIT_HOLD_COUNT;
        }

        /* Configure RSTP on the bridge. */
        if (ofproto_set_rstp(br->ofproto, &br_s)) {
            VLOG_ERR("bridge %s: could not enable RSTP", br->name);
            return;
        }

        port_num_counter = 0;
        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            struct ofproto_port_rstp_settings port_s;
            struct iface *iface;

            port_configure_rstp(br->ofproto, port, &port_s,
                    &port_num_counter);

            /* As bonds are not supported, just apply configuration to
             * all interfaces. */
            LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                if (ofproto_port_set_rstp(br->ofproto, iface->ofp_port,
                            &port_s)) {
                    VLOG_ERR("port %s: could not enable RSTP", port->name);
                    continue;
                }
            }
        }
    }
}

static bool
bridge_has_bond_fake_iface(const struct bridge *br, const char *name)
{
    const struct port *port = port_lookup(br, name);
    return port && port_is_bond_fake_iface(port);
}

static bool
port_is_bond_fake_iface(const struct port *port)
{
    return port->cfg->bond_fake_iface && !list_is_short(&port->ifaces);
}
#endif
static void
add_del_bridges(const struct ovsrec_open_vswitch *cfg)
{
    struct bridge *br, *next;
    struct shash new_br;
    size_t i;

    /* Collect new bridges' names and types. */
    shash_init(&new_br);
    for (i = 0; i < cfg->n_bridges; i++) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        const struct ovsrec_bridge *br_cfg = cfg->bridges[i];

        if (strchr(br_cfg->name, '/')) {
            /* Prevent remote ovsdb-server users from accessing arbitrary
             * directories, e.g. consider a bridge named "../../../etc/". */
            VLOG_WARN_RL(&rl, "ignoring bridge with invalid name \"%s\"",
                         br_cfg->name);
        } else if (!shash_add_once(&new_br, br_cfg->name, br_cfg)) {
            VLOG_WARN_RL(&rl, "bridge %s specified twice", br_cfg->name);
        }
    }

    /* Get rid of deleted bridges or those whose types have changed.
     * Update 'cfg' of bridges that still exist. */
    HMAP_FOR_EACH_SAFE (br, next, node, &all_bridges) {
        br->cfg = shash_find_data(&new_br, br->name);
        if (!br->cfg || strcmp(br->type, ofproto_normalize_type(
                                   br->cfg->datapath_type))) {
            bridge_destroy(br);
        }
    }

    /* Add new bridges. */
    for (i = 0; i < cfg->n_bridges; i++) {
        const struct ovsrec_bridge *br_cfg = cfg->bridges[i];
        struct bridge *br = bridge_lookup(br_cfg->name);
        if (!br) {
            bridge_create(br_cfg);
        }
    }

    shash_destroy(&new_br);
}

#ifdef OPS
static void
add_del_vrfs(const struct ovsrec_open_vswitch *cfg)
{
    struct vrf *vrf, *next;
    struct shash new_vrf;
    size_t i;

    /* Collect new vrfs' names */
    shash_init(&new_vrf);
    for (i = 0; i < cfg->n_vrfs; i++) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];

        if (strchr(vrf_cfg->name, '/')) {
            /* Prevent remote ovsdb-server users from accessing arbitrary
             * directories, e.g. consider a vrf named "../../../etc/". */
            VLOG_WARN_RL(&rl, "ignoring vrf with invalid name \"%s\"",
                         vrf_cfg->name);
        } else if (!shash_add_once(&new_vrf, vrf_cfg->name, vrf_cfg)) {
            VLOG_WARN_RL(&rl, "vrf %s specified twice", vrf_cfg->name);
        }
    }

    /* Get rid of deleted vrfs
     * Update 'cfg' of vrfs that still exist. */
    HMAP_FOR_EACH_SAFE (vrf, next, node, &all_vrfs) {
        vrf->cfg = shash_find_data(&new_vrf, vrf->up->name);
        if (!vrf->cfg) {
            vrf_destroy(vrf);
        }
    }

    /* Add new vrfs. */
    for (i = 0; i < cfg->n_vrfs; i++) {
        const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];
        struct vrf *vrf = vrf_lookup(vrf_cfg->name);
        if (!vrf) {
            vrf_create(vrf_cfg);
        }
    }

    shash_destroy(&new_vrf);
}
#endif

/* Configures 'netdev' based on the "options" column in 'iface_cfg'.
 * Returns 0 if successful, otherwise a positive errno value. */
static int
iface_set_netdev_config(const struct ovsrec_interface *iface_cfg,
                        struct netdev *netdev, char **errp)
{
    return netdev_set_config(netdev, &iface_cfg->options, errp);
}

/* Opens a network device for 'if_cfg' and configures it.  Adds the network
 * device to br->ofproto and stores the OpenFlow port number in '*ofp_portp'.
 *
 * If successful, returns 0 and stores the network device in '*netdevp'.  On
 * failure, returns a positive errno value and stores NULL in '*netdevp'. */
static int
iface_do_create(const struct bridge *br,
                const struct ovsrec_interface *iface_cfg,
#ifndef OPS_TEMP
                const struct ovsrec_port *port_cfg,
#endif
                ofp_port_t *ofp_portp,
                struct netdev **netdevp,
                char **errp)
{
    struct netdev *netdev = NULL;
#ifdef OPS
    struct smap sub_intf_info;
    int ret = 0;
#endif
    int error;

    if (netdev_is_reserved_name(iface_cfg->name)) {
        VLOG_WARN("could not create interface %s, name is reserved",
                  iface_cfg->name);
        error = EINVAL;
        goto error;
    }

    error = netdev_open(iface_cfg->name,
                        iface_get_type(iface_cfg, br->cfg), &netdev);
    if (error) {
        VLOG_WARN_BUF(errp, "could not open network device %s (%s)",
                      iface_cfg->name, ovs_strerror(error));
        goto error;
    }

#ifdef OPS
    /* Initialize mac to default system mac.
     * For internal interface system mac will be used.
     * For hw interfaces this will be changed to mac from hw_intf_info
     */
    error = netdev_set_etheraddr(netdev, br->default_ea);

    if (error) {
        goto error;
    }

    error = netdev_set_hw_intf_info(netdev, &(iface_cfg->hw_intf_info));

    if (error) {
        goto error;
    }

    if (!strcmp(iface_cfg->type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
          smap_init(&sub_intf_info);
          vrf_delete_or_reconfigure_subintf(&sub_intf_info, iface_cfg);
          smap_destroy(&sub_intf_info);
          if (ret) {
              goto error;
          }
    } else {
        error = iface_set_netdev_config(iface_cfg, netdev, errp);
        if (error) {
            goto error;
        }
    }
#endif
    *ofp_portp = iface_pick_ofport(iface_cfg);
    error = ofproto_port_add(br->ofproto, netdev, ofp_portp);
    if (error) {
        goto error;
    }

    VLOG_DBG("bridge %s: added interface %s on port %d",
              br->name, iface_cfg->name, *ofp_portp);

#ifndef OPS_TEMP
    if (port_cfg->vlan_mode && !strcmp(port_cfg->vlan_mode, "splinter")) {
        netdev_turn_flags_on(netdev, NETDEV_UP, NULL);
    }
#endif
    *netdevp = netdev;
    return 0;

error:
    *netdevp = NULL;
    netdev_close(netdev);
    return error;
}

/* Creates a new iface on 'br' based on 'if_cfg'.  The new iface has OpenFlow
 * port number 'ofp_port'.  If ofp_port is OFPP_NONE, an OpenFlow port is
 * automatically allocated for the iface.  Takes ownership of and
 * deallocates 'if_cfg'.
 *
 * Return true if an iface is successfully created, false otherwise. */
static bool
iface_create(struct bridge *br, const struct ovsrec_interface *iface_cfg,
             const struct ovsrec_port *port_cfg)
{
    struct netdev *netdev;
    struct iface *iface;
    ofp_port_t ofp_port;
    struct port *port;
    char *errp = NULL;
    int error;

    /* Do the bits that can fail up front. */
    ovs_assert(!iface_lookup(br, iface_cfg->name));
#ifndef OPS_TEMP
    error = iface_do_create(br, iface_cfg, port_cfg, &ofp_port, &netdev, &errp);
#else
    error = iface_do_create(br, iface_cfg,           &ofp_port, &netdev, &errp);
#endif
    if (error) {
        iface_clear_db_record(iface_cfg, errp);
        free(errp);
        return false;
    }

    /* Get or create the port structure. */
    port = port_lookup(br, port_cfg->name);
    if (!port) {
        port = port_create(br, port_cfg);
    }

    /* Create the iface structure. */
    iface = xzalloc(sizeof *iface);
    list_push_back(&port->ifaces, &iface->port_elem);
    hmap_insert(&br->iface_by_name, &iface->name_node,
                hash_string(iface_cfg->name, 0));
    iface->port = port;
    iface->name = xstrdup(iface_cfg->name);
    iface->ofp_port = ofp_port;
    iface->netdev = netdev;
    iface->type = iface_get_type(iface_cfg, br->cfg);
    iface->cfg = iface_cfg;
    hmap_insert(&br->ifaces, &iface->ofp_port_node,
                hash_ofp_port(ofp_port));

    /* Populate initial status in database. */
    iface_refresh_stats(iface);
    iface_refresh_netdev_status(iface);

#ifndef OPS_TEMP
    /* Add bond fake iface if necessary. */
    if (port_is_bond_fake_iface(port)) {
        struct ofproto_port ofproto_port;

        if (ofproto_port_query_by_name(br->ofproto, port->name,
                                       &ofproto_port)) {
            struct netdev *netdev;
            int error;

            error = netdev_open(port->name, "internal", &netdev);
            if (!error) {
                ofp_port_t fake_ofp_port = OFPP_NONE;
                ofproto_port_add(br->ofproto, netdev, &fake_ofp_port);
                netdev_close(netdev);
            } else {
                VLOG_WARN("could not open network device %s (%s)",
                          port->name, ovs_strerror(error));
            }
        } else {
            /* Already exists, nothing to do. */
            ofproto_port_destroy(&ofproto_port);
        }
    }
#endif
    return true;
}

#ifndef OPS_TEMP
/* Set forward BPDU option. */
static void
bridge_configure_forward_bpdu(struct bridge *br)
{
    ofproto_set_forward_bpdu(br->ofproto,
                             smap_get_bool(&br->cfg->other_config,
                                           "forward-bpdu",
                                           false));
}
#endif
/* Set MAC learning table configuration for 'br'. */
static void
bridge_configure_mac_table(struct bridge *br)
{
    const char *idle_time_str;
    int idle_time;

    const char *mac_table_size_str;
    int mac_table_size;

    idle_time_str = smap_get(&br->cfg->other_config, "mac-aging-time");
    idle_time = (idle_time_str && atoi(idle_time_str)
                 ? atoi(idle_time_str)
                 : MAC_ENTRY_DEFAULT_IDLE_TIME);

    mac_table_size_str = smap_get(&br->cfg->other_config, "mac-table-size");
    mac_table_size = (mac_table_size_str && atoi(mac_table_size_str)
                      ? atoi(mac_table_size_str)
                      : MAC_DEFAULT_MAX);

    ofproto_set_mac_table_config(br->ofproto, idle_time, mac_table_size);
}

#ifndef OPS_TEMP
/* Set multicast snooping table configuration for 'br'. */
static void
bridge_configure_mcast_snooping(struct bridge *br)
{
    if (!br->cfg->mcast_snooping_enable) {
        ofproto_set_mcast_snooping(br->ofproto, NULL);
    } else {
        struct port *port;
        struct ofproto_mcast_snooping_settings br_s;
        const char *idle_time_str;
        const char *max_entries_str;

        idle_time_str = smap_get(&br->cfg->other_config,
                                 "mcast-snooping-aging-time");
        br_s.idle_time = (idle_time_str && atoi(idle_time_str)
                          ? atoi(idle_time_str)
                          : MCAST_ENTRY_DEFAULT_IDLE_TIME);

        max_entries_str = smap_get(&br->cfg->other_config,
                                   "mcast-snooping-table-size");
        br_s.max_entries = (max_entries_str && atoi(max_entries_str)
                            ? atoi(max_entries_str)
                            : MCAST_DEFAULT_MAX_ENTRIES);

        br_s.flood_unreg = !smap_get_bool(&br->cfg->other_config,
                                    "mcast-snooping-disable-flood-unregistered",
                                    false);

        /* Configure multicast snooping on the bridge */
        if (ofproto_set_mcast_snooping(br->ofproto, &br_s)) {
            VLOG_ERR("bridge %s: could not enable multicast snooping",
                     br->name);
            return;
        }

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
            bool flood = smap_get_bool(&port->cfg->other_config,
                                       "mcast-snooping-flood", false);
            if (ofproto_port_set_mcast_snooping(br->ofproto, port, flood)) {
                VLOG_ERR("port %s: could not configure mcast snooping",
                         port->name);
            }
        }
    }
}
#endif
static void
find_local_hw_addr(const struct bridge *br, struct eth_addr *ea,
                   const struct port *fake_br, struct iface **hw_addr_iface)
{
#ifndef OPS_TEMP
    struct hmapx mirror_output_ports;
#endif
    struct port *port;
    bool found_addr = false;
    int error;
#ifndef OPS_TEMP
    int i;

    /* Mirror output ports don't participate in picking the local hardware
     * address.  ofproto can't help us find out whether a given port is a
     * mirror output because we haven't configured mirrors yet, so we need to
     * accumulate them ourselves. */
    hmapx_init(&mirror_output_ports);
    for (i = 0; i < br->cfg->n_mirrors; i++) {
        struct ovsrec_mirror *m = br->cfg->mirrors[i];
        if (m->output_port) {
            hmapx_add(&mirror_output_ports, m->output_port);
        }
    }
#endif
    /* Otherwise choose the minimum non-local MAC address among all of the
     * interfaces. */
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        struct eth_addr iface_ea;
        struct iface *candidate;
        struct iface *iface;

#ifndef OPS_TEMP
        /* Mirror output ports don't participate. */
        if (hmapx_contains(&mirror_output_ports, port->cfg)) {
            continue;
        }

#endif
        /* Choose the MAC address to represent the port. */
        iface = NULL;
        if (port->cfg->mac && eth_addr_from_string(port->cfg->mac, &iface_ea)) {
            /* Find the interface with this Ethernet address (if any) so that
             * we can provide the correct devname to the caller. */
            LIST_FOR_EACH (candidate, port_elem, &port->ifaces) {
                struct eth_addr candidate_ea;
                if (!netdev_get_etheraddr(candidate->netdev, &candidate_ea)
                    && eth_addr_equals(iface_ea, candidate_ea)) {
                    iface = candidate;
                }
            }
        } else {
            /* Choose the interface whose MAC address will represent the port.
             * The Linux kernel bonding code always chooses the MAC address of
             * the first slave added to a bond, and the Fedora networking
             * scripts always add slaves to a bond in alphabetical order, so
             * for compatibility we choose the interface with the name that is
             * first in alphabetical order. */
            LIST_FOR_EACH (candidate, port_elem, &port->ifaces) {
                if (!iface || strcmp(candidate->name, iface->name) < 0) {
                    iface = candidate;
                }
            }

            /* The local port doesn't count (since we're trying to choose its
             * MAC address anyway). */
            if (iface->ofp_port == OFPP_LOCAL) {
                continue;
            }

            /* For fake bridges we only choose from ports with the same tag */
            if (fake_br && fake_br->cfg && fake_br->cfg->tag) {
                if (!port->cfg->tag) {
                    continue;
                }
                if (*port->cfg->tag != *fake_br->cfg->tag) {
                    continue;
                }
            }

            /* Grab MAC. */
            error = netdev_get_etheraddr(iface->netdev, &iface_ea);
            if (error) {
                continue;
            }
        }

        /* Compare against our current choice. */
        if (!eth_addr_is_multicast(iface_ea) &&
            !eth_addr_is_local(iface_ea) &&
            !eth_addr_is_reserved(iface_ea) &&
            !eth_addr_is_zero(iface_ea) &&
            (!found_addr || eth_addr_compare_3way(iface_ea, *ea) < 0))
        {
            *ea = iface_ea;
            *hw_addr_iface = iface;
            found_addr = true;
        }
    }

    if (!found_addr) {
        *ea = br->default_ea;
        *hw_addr_iface = NULL;
    }

#ifndef OPS_TEMP
    hmapx_destroy(&mirror_output_ports);
#endif
}

static void
bridge_pick_local_hw_addr(struct bridge *br, struct eth_addr *ea,
                          struct iface **hw_addr_iface)
{
    const char *hwaddr;
    *hw_addr_iface = NULL;

    /* Did the user request a particular MAC? */
    hwaddr = smap_get(&br->cfg->other_config, "hwaddr");
    if (hwaddr && eth_addr_from_string(hwaddr, ea)) {
        if (eth_addr_is_multicast(*ea)) {
            VLOG_ERR("bridge %s: cannot set MAC address to multicast "
                     "address "ETH_ADDR_FMT, br->name, ETH_ADDR_ARGS(*ea));
        } else if (eth_addr_is_zero(*ea)) {
            VLOG_ERR("bridge %s: cannot set MAC address to zero", br->name);
        } else {
            return;
        }
    }

    /* Find a local hw address */
    find_local_hw_addr(br, ea, NULL, hw_addr_iface);
}

/* Choose and returns the datapath ID for bridge 'br' given that the bridge
 * Ethernet address is 'bridge_ea'.  If 'bridge_ea' is the Ethernet address of
 * an interface on 'br', then that interface must be passed in as
 * 'hw_addr_iface'; if 'bridge_ea' was derived some other way, then
 * 'hw_addr_iface' must be passed in as a null pointer. */
static uint64_t
bridge_pick_datapath_id(struct bridge *br,
                        const struct eth_addr bridge_ea,
                        struct iface *hw_addr_iface)
{
    /*
     * The procedure for choosing a bridge MAC address will, in the most
     * ordinary case, also choose a unique MAC that we can use as a datapath
     * ID.  In some special cases, though, multiple bridges will end up with
     * the same MAC address.  This is OK for the bridges, but it will confuse
     * the OpenFlow controller, because each datapath needs a unique datapath
     * ID.
     *
     * Datapath IDs must be unique.  It is also very desirable that they be
     * stable from one run to the next, so that policy set on a datapath
     * "sticks".
     */
    const char *datapath_id;
    uint64_t dpid;

    datapath_id = smap_get(&br->cfg->other_config, "datapath-id");
    if (datapath_id && dpid_from_string(datapath_id, &dpid)) {
        return dpid;
    }

#ifndef OPS
    if (!hw_addr_iface) {
        /*
         * A purely internal bridge, that is, one that has no non-virtual
         * network devices on it at all, is difficult because it has no
         * natural unique identifier at all.
         *
         * When the host is a XenServer, we handle this case by hashing the
         * host's UUID with the name of the bridge.  Names of bridges are
         * persistent across XenServer reboots, although they can be reused if
         * an internal network is destroyed and then a new one is later
         * created, so this is fairly effective.
         *
         * When the host is not a XenServer, we punt by using a random MAC
         * address on each run.
         */
        const char *host_uuid = xenserver_get_host_uuid();
        if (host_uuid) {
            char *combined = xasprintf("%s,%s", host_uuid, br->name);
            dpid = dpid_from_hash(combined, strlen(combined));
            free(combined);
            return dpid;
        }
    }
#endif

    return eth_addr_to_uint64(bridge_ea);
}

static uint64_t
dpid_from_hash(const void *data, size_t n)
{
    union {
        uint8_t bytes[SHA1_DIGEST_SIZE];
        struct eth_addr ea;
    } hash;

    sha1_bytes(data, n, hash.bytes);
    eth_addr_mark_random(&hash.ea);
    return eth_addr_to_uint64(hash.ea);
}

static void
iface_refresh_netdev_status(struct iface *iface)
{
    struct smap smap;

    enum netdev_features current;
    enum netdev_flags flags;
    const char *link_state;
    struct eth_addr mac;
    int64_t bps, mtu_64,
#ifndef OPS_TEMP
    ifindex64,
#endif
    link_resets;
    int mtu, error;
    if (iface_is_synthetic(iface)) {
        return;
    }

#ifdef OPS
    /* Interface status is updated from subsystem.c. */
    if (!iface->type
        || (!strcmp(iface->type, OVSREC_INTERFACE_TYPE_SYSTEM))
        || (!strcmp(iface->type, OVSREC_INTERFACE_TYPE_LOOPBACK))) {
        return;
    } else if (!iface->type
               || (!strcmp(iface->type, OVSREC_INTERFACE_TYPE_VLANSUBINT))) {
        error = netdev_get_flags(iface->netdev, &flags);
        if (!error) {
            const char *state = flags & NETDEV_UP
                                ? OVSREC_INTERFACE_LINK_STATE_UP
                                : OVSREC_INTERFACE_LINK_STATE_DOWN;
            ovsrec_interface_set_admin_state(iface->cfg, state);
        } else {
            ovsrec_interface_set_admin_state(iface->cfg, NULL);
        }
        return;
    }
#endif

    if (iface->change_seq == netdev_get_change_seq(iface->netdev)
        && !status_txn_try_again) {
        return;
    }

    iface->change_seq = netdev_get_change_seq(iface->netdev);

    smap_init(&smap);

    if (!netdev_get_status(iface->netdev, &smap)) {
        ovsrec_interface_set_status(iface->cfg, &smap);
    } else {
        ovsrec_interface_set_status(iface->cfg, NULL);
    }

    smap_destroy(&smap);

    error = netdev_get_flags(iface->netdev, &flags);
    if (!error) {
        const char *state = flags & NETDEV_UP ? "up" : "down";

        ovsrec_interface_set_admin_state(iface->cfg, state);
    } else {
        ovsrec_interface_set_admin_state(iface->cfg, NULL);
    }

    link_state = netdev_get_carrier(iface->netdev) ? "up" : "down";
    ovsrec_interface_set_link_state(iface->cfg, link_state);

    link_resets = netdev_get_carrier_resets(iface->netdev);
    ovsrec_interface_set_link_resets(iface->cfg, &link_resets, 1);

    error = netdev_get_features(iface->netdev, &current, NULL, NULL, NULL);
    bps = !error ? netdev_features_to_bps(current, 0) : 0;
    if (bps) {
        ovsrec_interface_set_duplex(iface->cfg,
                                    netdev_features_is_full_duplex(current)
                                    ? "full" : "half");
        ovsrec_interface_set_link_speed(iface->cfg, &bps, 1);
    } else {
        ovsrec_interface_set_duplex(iface->cfg, NULL);
        ovsrec_interface_set_link_speed(iface->cfg, NULL, 0);
    }

    error = netdev_get_mtu(iface->netdev, &mtu);
    if (!error) {
        mtu_64 = mtu;
        ovsrec_interface_set_mtu(iface->cfg, &mtu_64, 1);
    } else {
        ovsrec_interface_set_mtu(iface->cfg, NULL, 0);
    }

    error = netdev_get_etheraddr(iface->netdev, &mac);
    if (!error) {
        char mac_string[32];

        sprintf(mac_string, ETH_ADDR_FMT, ETH_ADDR_ARGS(mac));
        ovsrec_interface_set_mac_in_use(iface->cfg, mac_string);
    } else {
        ovsrec_interface_set_mac_in_use(iface->cfg, NULL);
    }

#ifndef OPS_TEMP
    /* The netdev may return a negative number (such as -EOPNOTSUPP)
     * if there is no valid ifindex number. */
    ifindex64 = netdev_get_ifindex(iface->netdev);
    if (ifindex64 < 0) {
        ifindex64 = 0;
    }
    ovsrec_interface_set_ifindex(iface->cfg, &ifindex64, 1);
#endif
}

static void
iface_refresh_ofproto_status(struct iface *iface)
{
#ifndef OPS_TEMP
    int current;
#endif

    if (iface_is_synthetic(iface)) {
        return;
    }

#ifndef OPS_TEMP
    current = ofproto_port_is_lacp_current(iface->port->bridge->ofproto,
                                           iface->ofp_port);
    if (current >= 0) {
        bool bl = current;
        ovsrec_interface_set_lacp_current(iface->cfg, &bl, 1);
    } else {
        ovsrec_interface_set_lacp_current(iface->cfg, NULL, 0);
    }

    if (ofproto_port_cfm_status_changed(iface->port->bridge->ofproto,
                                        iface->ofp_port)
        || status_txn_try_again) {
        iface_refresh_cfm_stats(iface);
    }

    if (ofproto_port_bfd_status_changed(iface->port->bridge->ofproto,
                                        iface->ofp_port)
        || status_txn_try_again) {
        struct smap smap;

        smap_init(&smap);
        ofproto_port_get_bfd_status(iface->port->bridge->ofproto,
                                    iface->ofp_port, &smap);
        ovsrec_interface_set_bfd_status(iface->cfg, &smap);
        smap_destroy(&smap);
    }
#endif
}

#ifndef OPS_TEMP
/* Writes 'iface''s CFM statistics to the database. 'iface' must not be
 * synthetic. */
static void
iface_refresh_cfm_stats(struct iface *iface)
{
    const struct ovsrec_interface *cfg = iface->cfg;
    struct cfm_status status;
    int error;

    error = ofproto_port_get_cfm_status(iface->port->bridge->ofproto,
                                        iface->ofp_port, &status);
    if (error > 0) {
        ovsrec_interface_set_cfm_fault(cfg, NULL, 0);
        ovsrec_interface_set_cfm_fault_status(cfg, NULL, 0);
        ovsrec_interface_set_cfm_remote_opstate(cfg, NULL);
        ovsrec_interface_set_cfm_flap_count(cfg, NULL, 0);
        ovsrec_interface_set_cfm_health(cfg, NULL, 0);
        ovsrec_interface_set_cfm_remote_mpids(cfg, NULL, 0);
    } else {
        const char *reasons[CFM_FAULT_N_REASONS];
        int64_t cfm_health = status.health;
        int64_t cfm_flap_count = status.flap_count;
        bool faulted = status.faults != 0;
        size_t i, j;

        ovsrec_interface_set_cfm_fault(cfg, &faulted, 1);

        j = 0;
        for (i = 0; i < CFM_FAULT_N_REASONS; i++) {
            int reason = 1 << i;
            if (status.faults & reason) {
                reasons[j++] = cfm_fault_reason_to_str(reason);
            }
        }
        ovsrec_interface_set_cfm_fault_status(cfg, (char **) reasons, j);

        ovsrec_interface_set_cfm_flap_count(cfg, &cfm_flap_count, 1);

        if (status.remote_opstate >= 0) {
            const char *remote_opstate = status.remote_opstate ? "up" : "down";
            ovsrec_interface_set_cfm_remote_opstate(cfg, remote_opstate);
        } else {
            ovsrec_interface_set_cfm_remote_opstate(cfg, NULL);
        }

        ovsrec_interface_set_cfm_remote_mpids(cfg,
                                              (const int64_t *)status.rmps,
                                              status.n_rmps);
        if (cfm_health >= 0) {
            ovsrec_interface_set_cfm_health(cfg, &cfm_health, 1);
        } else {
            ovsrec_interface_set_cfm_health(cfg, NULL, 0);
        }

        free(status.rmps);
    }
}
#endif

static void
iface_refresh_stats(struct iface *iface)
{

#ifdef OPS
    /* Interface stats are updated from subsystem.c. */
    if (!iface->type || !strcmp(iface->type, "system")) {
            return;
    }
#endif

#define IFACE_STATS                             \
    IFACE_STAT(rx_packets,      "rx_packets")   \
    IFACE_STAT(tx_packets,      "tx_packets")   \
    IFACE_STAT(rx_bytes,        "rx_bytes")     \
    IFACE_STAT(tx_bytes,        "tx_bytes")     \
    IFACE_STAT(rx_dropped,      "rx_dropped")   \
    IFACE_STAT(tx_dropped,      "tx_dropped")   \
    IFACE_STAT(rx_errors,       "rx_errors")    \
    IFACE_STAT(tx_errors,       "tx_errors")    \
    IFACE_STAT(rx_frame_errors, "rx_frame_err") \
    IFACE_STAT(rx_over_errors,  "rx_over_err")  \
    IFACE_STAT(rx_crc_errors,   "rx_crc_err")   \
    IFACE_STAT(collisions,      "collisions")

#define IFACE_STAT(MEMBER, NAME) + 1
    enum { N_IFACE_STATS = IFACE_STATS };
#undef IFACE_STAT
    int64_t values[N_IFACE_STATS];
    char *keys[N_IFACE_STATS];
    int n;

    struct netdev_stats stats;

    if (iface_is_synthetic(iface)) {
        return;
    }

    /* Intentionally ignore return value, since errors will set 'stats' to
     * all-1s, and we will deal with that correctly below. */
    netdev_get_stats(iface->netdev, &stats);

    /* Copy statistics into keys[] and values[]. */
    n = 0;
#define IFACE_STAT(MEMBER, NAME)                \
    if (stats.MEMBER != UINT64_MAX) {           \
        keys[n] = NAME;                         \
        values[n] = stats.MEMBER;               \
        n++;                                    \
    }
    IFACE_STATS;
#undef IFACE_STAT
    ovs_assert(n <= N_IFACE_STATS);

    ovsrec_interface_set_statistics(iface->cfg, keys, values, n);
#undef IFACE_STATS
}

static void
br_refresh_datapath_info(struct bridge *br)
{
    const char *version;

    version = (br->ofproto && br->ofproto->ofproto_class->get_datapath_version
               ? br->ofproto->ofproto_class->get_datapath_version(br->ofproto)
               : NULL);

    ovsrec_bridge_set_datapath_version(br->cfg,
                                       version ? version : "<unknown>");
}

#ifndef OPS_TEMP
static void
br_refresh_stp_status(struct bridge *br)
{
    struct smap smap = SMAP_INITIALIZER(&smap);
    struct ofproto *ofproto = br->ofproto;
    struct ofproto_stp_status status;

    if (ofproto_get_stp_status(ofproto, &status)) {
        return;
    }

    if (!status.enabled) {
        ovsrec_bridge_set_status(br->cfg, NULL);
        return;
    }

    smap_add_format(&smap, "stp_bridge_id", STP_ID_FMT,
                    STP_ID_ARGS(status.bridge_id));
    smap_add_format(&smap, "stp_designated_root", STP_ID_FMT,
                    STP_ID_ARGS(status.designated_root));
    smap_add_format(&smap, "stp_root_path_cost", "%d", status.root_path_cost);

    ovsrec_bridge_set_status(br->cfg, &smap);
    smap_destroy(&smap);
}

static void
port_refresh_stp_status(struct port *port)
{
    struct ofproto *ofproto = port->bridge->ofproto;
    struct iface *iface;
    struct ofproto_port_stp_status status;
    struct smap smap;

    if (port_is_synthetic(port)) {
        return;
    }

    /* STP doesn't currently support bonds. */
    if (!list_is_singleton(&port->ifaces)) {
        ovsrec_port_set_status(port->cfg, NULL);
        return;
    }

    iface = CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);
    if (ofproto_port_get_stp_status(ofproto, iface->ofp_port, &status)) {
        return;
    }

    if (!status.enabled) {
        ovsrec_port_set_status(port->cfg, NULL);
        return;
    }

    /* Set Status column. */
    smap_init(&smap);
    smap_add_format(&smap, "stp_port_id", STP_PORT_ID_FMT, status.port_id);
    smap_add(&smap, "stp_state", stp_state_name(status.state));
    smap_add_format(&smap, "stp_sec_in_state", "%u", status.sec_in_state);
    smap_add(&smap, "stp_role", stp_role_name(status.role));
    ovsrec_port_set_status(port->cfg, &smap);
    smap_destroy(&smap);
}

static void
port_refresh_stp_stats(struct port *port)
{
    struct ofproto *ofproto = port->bridge->ofproto;
    struct iface *iface;
    struct ofproto_port_stp_stats stats;
    char *keys[3];
    int64_t int_values[3];

    if (port_is_synthetic(port)) {
        return;
    }

    /* STP doesn't currently support bonds. */
    if (!list_is_singleton(&port->ifaces)) {
        return;
    }

    iface = CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);
    if (ofproto_port_get_stp_stats(ofproto, iface->ofp_port, &stats)) {
        return;
    }

    if (!stats.enabled) {
        ovsrec_port_set_statistics(port->cfg, NULL, NULL, 0);
        return;
    }

    /* Set Statistics column. */
    keys[0] = "stp_tx_count";
    int_values[0] = stats.tx_count;
    keys[1] = "stp_rx_count";
    int_values[1] = stats.rx_count;
    keys[2] = "stp_error_count";
    int_values[2] = stats.error_count;

    ovsrec_port_set_statistics(port->cfg, keys, int_values,
                               ARRAY_SIZE(int_values));
}

static void
br_refresh_rstp_status(struct bridge *br)
{
    struct smap smap = SMAP_INITIALIZER(&smap);
    struct ofproto *ofproto = br->ofproto;
    struct ofproto_rstp_status status;

    if (ofproto_get_rstp_status(ofproto, &status)) {
        return;
    }
    if (!status.enabled) {
        ovsrec_bridge_set_rstp_status(br->cfg, NULL);
        return;
    }
    smap_add_format(&smap, "rstp_bridge_id", RSTP_ID_FMT,
                    RSTP_ID_ARGS(status.bridge_id));
    smap_add_format(&smap, "rstp_root_path_cost", "%"PRIu32,
                    status.root_path_cost);
    smap_add_format(&smap, "rstp_root_id", RSTP_ID_FMT,
                    RSTP_ID_ARGS(status.root_id));
    smap_add_format(&smap, "rstp_designated_id", RSTP_ID_FMT,
                    RSTP_ID_ARGS(status.designated_id));
    smap_add_format(&smap, "rstp_designated_port_id", RSTP_PORT_ID_FMT,
                    status.designated_port_id);
    smap_add_format(&smap, "rstp_bridge_port_id", RSTP_PORT_ID_FMT,
                    status.bridge_port_id);
    ovsrec_bridge_set_rstp_status(br->cfg, &smap);
    smap_destroy(&smap);
}

static void
port_refresh_rstp_status(struct port *port)
{
    struct ofproto *ofproto = port->bridge->ofproto;
    struct iface *iface;
    struct ofproto_port_rstp_status status;
    char *keys[3];
    int64_t int_values[3];
    struct smap smap;

    if (port_is_synthetic(port)) {
        return;
    }

    /* RSTP doesn't currently support bonds. */
    if (!list_is_singleton(&port->ifaces)) {
        ovsrec_port_set_rstp_status(port->cfg, NULL);
        return;
    }

    iface = CONTAINER_OF(list_front(&port->ifaces), struct iface, port_elem);
    if (ofproto_port_get_rstp_status(ofproto, iface->ofp_port, &status)) {
        return;
    }

    if (!status.enabled) {
        ovsrec_port_set_rstp_status(port->cfg, NULL);
        ovsrec_port_set_rstp_statistics(port->cfg, NULL, NULL, 0);
        return;
    }
    /* Set Status column. */
    smap_init(&smap);

    smap_add_format(&smap, "rstp_port_id", RSTP_PORT_ID_FMT,
                    status.port_id);
    smap_add_format(&smap, "rstp_port_role", "%s",
                    rstp_port_role_name(status.role));
    smap_add_format(&smap, "rstp_port_state", "%s",
                    rstp_state_name(status.state));
    smap_add_format(&smap, "rstp_designated_bridge_id", RSTP_ID_FMT,
                    RSTP_ID_ARGS(status.designated_bridge_id));
    smap_add_format(&smap, "rstp_designated_port_id", RSTP_PORT_ID_FMT,
                    status.designated_port_id);
    smap_add_format(&smap, "rstp_designated_path_cost", "%"PRIu32,
                    status.designated_path_cost);

    ovsrec_port_set_rstp_status(port->cfg, &smap);
    smap_destroy(&smap);

    /* Set Statistics column. */
    keys[0] = "rstp_tx_count";
    int_values[0] = status.tx_count;
    keys[1] = "rstp_rx_count";
    int_values[1] = status.rx_count;
    keys[2] = "rstp_uptime";
    int_values[2] = status.uptime;
    ovsrec_port_set_rstp_statistics(port->cfg, keys, int_values,
            ARRAY_SIZE(int_values));
}

static void
port_refresh_bond_status(struct port *port, bool force_update)
{
    struct eth_addr mac;

    /* Return if port is not a bond */
    if (list_is_singleton(&port->ifaces)) {
        return;
    }

    if (bond_get_changed_active_slave(port->name, &mac, force_update)) {
        struct ds mac_s;

        ds_init(&mac_s);
        ds_put_format(&mac_s, ETH_ADDR_FMT, ETH_ADDR_ARGS(mac));
        ovsrec_port_set_bond_active_slave(port->cfg, ds_cstr(&mac_s));
        ds_destroy(&mac_s);
    }
}
#endif

static bool
enable_system_stats(const struct ovsrec_open_vswitch *cfg)
{
    return smap_get_bool(&cfg->other_config, "enable-statistics", false);
}

static void
reconfigure_system_stats(const struct ovsrec_open_vswitch *cfg)
{
    bool enable = enable_system_stats(cfg);

    system_stats_enable(enable);
    if (!enable) {
        ovsrec_open_vswitch_set_statistics(cfg, NULL);
    }
}

static void
run_system_stats(void)
{
    const struct ovsrec_open_vswitch *cfg = ovsrec_open_vswitch_first(idl);
    struct smap *stats;

    stats = system_stats_run();
    if (stats && cfg) {
        struct ovsdb_idl_txn *txn;
        struct ovsdb_datum datum;

        txn = ovsdb_idl_txn_create(idl);
        ovsdb_datum_from_smap(&datum, stats);
        ovsdb_idl_txn_write(&cfg->header_, &ovsrec_open_vswitch_col_statistics,
                            &datum);
        ovsdb_idl_txn_commit(txn);
        ovsdb_idl_txn_destroy(txn);

        free(stats);
    }
}

#ifndef OPS_TEMP
static const char *
ofp12_controller_role_to_str(enum ofp12_controller_role role)
{
    switch (role) {
    case OFPCR12_ROLE_EQUAL:
        return "other";
    case OFPCR12_ROLE_MASTER:
        return "master";
    case OFPCR12_ROLE_SLAVE:
        return "slave";
    case OFPCR12_ROLE_NOCHANGE:
    default:
        return "*** INVALID ROLE ***";
    }
}

static void
refresh_controller_status(void)
{
    struct bridge *br;
    struct shash info;
    const struct ovsrec_controller *cfg;

    shash_init(&info);

    /* Accumulate status for controllers on all bridges. */
    HMAP_FOR_EACH (br, node, &all_bridges) {
        ofproto_get_ofproto_controller_info(br->ofproto, &info);
    }

    /* Update each controller in the database with current status. */
    OVSREC_CONTROLLER_FOR_EACH(cfg, idl) {
        struct ofproto_controller_info *cinfo =
            shash_find_data(&info, cfg->target);

        if (cinfo) {
            ovsrec_controller_set_is_connected(cfg, cinfo->is_connected);
            ovsrec_controller_set_role(cfg, ofp12_controller_role_to_str(
                                           cinfo->role));
            ovsrec_controller_set_status(cfg, &cinfo->pairs);
        } else {
            ovsrec_controller_set_is_connected(cfg, false);
            ovsrec_controller_set_role(cfg, NULL);
            ovsrec_controller_set_status(cfg, NULL);
        }
    }

    ofproto_free_ofproto_controller_info(&info);
}
#endif

/* Update interface and mirror statistics if necessary. */
static void
run_stats_update(void)
{
    static struct ovsdb_idl_txn *stats_txn;
    const struct ovsrec_open_vswitch *cfg = ovsrec_open_vswitch_first(idl);
    int stats_interval;

    if (!cfg) {
        return;
    }

    /* Statistics update interval should always be greater than or equal to
     * 5000 ms. */
    stats_interval = MAX(smap_get_int(&cfg->other_config,
                                      "stats-update-interval",
                                      5000), 5000);
    if (stats_timer_interval != stats_interval) {
        stats_timer_interval = stats_interval;
        stats_timer = LLONG_MIN;
    }

    if (time_msec() >= stats_timer) {
        enum ovsdb_idl_txn_status status;

        /* Rate limit the update.  Do not start a new update if the
         * previous one is not done. */
        if (!stats_txn) {
            struct bridge *br;

#ifdef OPS_TEMP
            struct vrf *vrf;
#endif
            stats_txn = ovsdb_idl_txn_create(idl);
            HMAP_FOR_EACH (br, node, &all_bridges) {
                struct port *port;
#ifndef OPS_TEMP
                struct mirror *m;
#endif
                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_stats(iface);
                    }
#ifndef OPS_TEMP
                    port_refresh_stp_stats(port);
#endif
                }
#ifndef OPS_TEMP
                HMAP_FOR_EACH (m, hmap_node, &br->mirrors) {
                    mirror_refresh_stats(m);
                }
#endif
            }

#ifdef OPS
            HMAP_FOR_EACH (vrf, node, &all_vrfs) {
                struct port *port;
                HMAP_FOR_EACH (port, hmap_node, &vrf->up->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_stats(iface);
                    }
                }
            }
#endif

#ifndef OPS_TEMP
            refresh_controller_status();
#endif
        }

        status = ovsdb_idl_txn_commit(stats_txn);
        if (status != TXN_INCOMPLETE) {
            stats_timer = time_msec() + stats_timer_interval;
            ovsdb_idl_txn_destroy(stats_txn);
            stats_txn = NULL;
        }
    }
}

/* Update bridge/port/interface status if necessary. */
static void
run_status_update(void)
{
    if (!status_txn) {
        uint64_t seq;

        /* Rate limit the update.  Do not start a new update if the
         * previous one is not done. */
        seq = seq_read(connectivity_seq_get());
        if (seq != connectivity_seqno || status_txn_try_again) {
            struct bridge *br;
#ifdef OPS
            struct vrf *vrf;
#endif
            connectivity_seqno = seq;
            status_txn = ovsdb_idl_txn_create(idl);
            HMAP_FOR_EACH (br, node, &all_bridges) {
                struct port *port;

#ifndef OPS_TEMP
                br_refresh_stp_status(br);
                br_refresh_rstp_status(br);
#endif
                br_refresh_datapath_info(br);
                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    struct iface *iface;

#ifndef OPS_TEMP
                    port_refresh_stp_status(port);
                    port_refresh_rstp_status(port);
                    port_refresh_bond_status(port, status_txn_try_again);
#endif
                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_netdev_status(iface);
                        iface_refresh_ofproto_status(iface);
                    }
                }
            }

#ifdef OPS
            HMAP_FOR_EACH (vrf, node, &all_vrfs) {
                struct port *port;

                HMAP_FOR_EACH (port, hmap_node, &vrf->up->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_netdev_status(iface);
                        iface_refresh_ofproto_status(iface);
                    }
                }
            }
 #endif
        }
    }

    /* Commit the transaction and get the status. If the transaction finishes,
     * then destroy the transaction. Otherwise, keep it so that we can check
     * progress the next time that this function is called. */
    if (status_txn) {
        enum ovsdb_idl_txn_status status;

        status = ovsdb_idl_txn_commit(status_txn);
        if (status != TXN_INCOMPLETE) {
            ovsdb_idl_txn_destroy(status_txn);
            status_txn = NULL;

            /* Sets the 'status_txn_try_again' if the transaction fails. */
            if (status == TXN_SUCCESS || status == TXN_UNCHANGED) {
                status_txn_try_again = false;
            } else {
                status_txn_try_again = true;
            }
        }
    }
}

static void
status_update_wait(void)
{
    /* This prevents the process from constantly waking up on
     * connectivity seq, when there is no connection to ovsdb. */
    if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    /* If the 'status_txn' is non-null (transaction incomplete), waits for the
     * transaction to complete.  If the status update to database needs to be
     * run again (transaction fails), registers a timeout in
     * 'STATUS_CHECK_AGAIN_MSEC'.  Otherwise, waits on the global connectivity
     * sequence number. */
    if (status_txn) {
        ovsdb_idl_txn_wait(status_txn);
    } else if (status_txn_try_again) {
        poll_timer_wait_until(time_msec() + STATUS_CHECK_AGAIN_MSEC);
    } else {
        seq_wait(connectivity_seq_get(), connectivity_seqno);
    }
}

static void
bridge_run__(void)
{
    struct bridge *br;
#ifdef OPS
    struct vrf *vrf;
#endif
    struct sset types;
    const char *type;

    /* Let each datapath type do the work that it needs to do. */
    sset_init(&types);
    ofproto_enumerate_types(&types);
    SSET_FOR_EACH (type, &types) {
        ofproto_type_run(type);
    }
    sset_destroy(&types);

    /* Let each bridge do the work that it needs to do. */
    HMAP_FOR_EACH (br, node, &all_bridges) {
        ofproto_run(br->ofproto);
    }

#ifdef OPS
    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        ofproto_run(vrf->up->ofproto);
    }
#endif
}

void
bridge_run(void)
{
    static struct ovsrec_open_vswitch null_cfg;
    const struct ovsrec_open_vswitch *cfg;

#ifndef OPS_TEMP
    bool vlan_splinters_changed;
#endif
    ovsrec_open_vswitch_init(&null_cfg);

    ovsdb_idl_run(idl);

    if (ovsdb_idl_is_lock_contended(idl)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);
        struct bridge *br, *next_br;
#ifdef OPS
        struct vrf *vrf, *next_vrf;
#endif
        VLOG_ERR_RL(&rl, "another ovs-vswitchd process is running, "
                    "disabling this process (pid %ld) until it goes away",
                    (long int) getpid());

        HMAP_FOR_EACH_SAFE (br, next_br, node, &all_bridges) {
            bridge_destroy(br);
        }

#ifdef OPS
        HMAP_FOR_EACH_SAFE (vrf, next_vrf, node, &all_vrfs) {
            vrf_destroy(vrf);
        }
#endif
        /* Since we will not be running system_stats_run() in this process
         * with the current situation of multiple ovs-vswitchd daemons,
         * disable system stats collection. */
        system_stats_enable(false);
        return;
    } else if (!ovsdb_idl_has_lock(idl)) {
        return;
    }
    cfg = ovsrec_open_vswitch_first(idl);

    /* Initialize the ofproto library.  This only needs to run once, but
     * it must be done after the configuration is set.  If the
     * initialization has already occurred, bridge_init_ofproto()
     * returns immediately. */
    bridge_init_ofproto(cfg);

    /* Once the value of flow-restore-wait is false, we no longer should
     * check its value from the database. */
    if (cfg && ofproto_get_flow_restore_wait()) {
        ofproto_set_flow_restore_wait(smap_get_bool(&cfg->other_config,
                                        "flow-restore-wait", false));
    }

    bridge_run__();

    /* Re-configure SSL.  We do this on every trip through the main loop,
     * instead of just when the database changes, because the contents of the
     * key and certificate files can change without the database changing.
     *
     * We do this before bridge_reconfigure() because that function might
     * initiate SSL connections and thus requires SSL to be configured. */
    if (cfg && cfg->ssl) {
        const struct ovsrec_ssl *ssl = cfg->ssl;

        stream_ssl_set_key_and_cert(ssl->private_key, ssl->certificate);
        stream_ssl_set_ca_cert_file(ssl->ca_cert, ssl->bootstrap_ca_cert);
    }

#ifndef OPS_TEMP
    /* If VLAN splinters are in use, then we need to reconfigure if VLAN
     * usage has changed. */
    vlan_splinters_changed = false;
    if (vlan_splinters_enabled_anywhere) {
        struct bridge *br;

        HMAP_FOR_EACH (br, node, &all_bridges) {
            if (ofproto_has_vlan_usage_changed(br->ofproto)) {
                vlan_splinters_changed = true;
                break;
            }
        }
    }
#endif
    if  (ovsdb_idl_get_seqno(idl) != idl_seqno
#ifndef OPS_TEMP
        || vlan_splinters_changed
#endif
        ) {
        struct ovsdb_idl_txn *txn;

#ifndef OPS
        idl_seqno = ovsdb_idl_get_seqno(idl);
#endif
        txn = ovsdb_idl_txn_create(idl);

        bridge_reconfigure(cfg ? cfg : &null_cfg);

#ifdef OPS
        /* Update seqno after bridge_reconfigure, to access earlier
         * seqno for comparision inside bridge_reconfigure */
        idl_seqno = ovsdb_idl_get_seqno(idl);
#endif

        if (cfg) {
            ovsrec_open_vswitch_set_cur_cfg(cfg, cfg->next_cfg);
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

            VLOG_INFO_ONCE("%s (Open vSwitch) %s", program_name, VERSION);
        }
    }

    run_stats_update();
    run_status_update();
    run_system_stats();
#ifdef OPS
    run_neighbor_update();
#endif
}

void
bridge_wait(void)
{
    struct sset types;
    const char *type;

    ovsdb_idl_wait(idl);
    if (daemonize_txn) {
        ovsdb_idl_txn_wait(daemonize_txn);
    }

    sset_init(&types);
    ofproto_enumerate_types(&types);
    SSET_FOR_EACH (type, &types) {
        ofproto_type_wait(type);
    }
    sset_destroy(&types);

    if (!hmap_is_empty(&all_bridges)) {
        struct bridge *br;

        HMAP_FOR_EACH (br, node, &all_bridges) {
            ofproto_wait(br->ofproto);
        }

        poll_timer_wait_until(stats_timer);
    }

    status_update_wait();
    system_stats_wait();
}

/* Adds some memory usage statistics for bridges into 'usage', for use with
 * memory_report(). */
void
bridge_get_memory_usage(struct simap *usage)
{
    struct bridge *br;
    struct sset types;
    const char *type;

    sset_init(&types);
    ofproto_enumerate_types(&types);
    SSET_FOR_EACH (type, &types) {
        ofproto_type_get_memory_usage(type, usage);
    }
    sset_destroy(&types);

    HMAP_FOR_EACH (br, node, &all_bridges) {
        ofproto_get_memory_usage(br->ofproto, usage);
    }
}
#ifndef OPS_TEMP

/* QoS unixctl user interface functions. */

struct qos_unixctl_show_cbdata {
    struct ds *ds;
    struct iface *iface;
};

static void
qos_unixctl_show_queue(unsigned int queue_id,
                       const struct smap *details,
                       struct iface *iface,
                       struct ds *ds)
{
    struct netdev_queue_stats stats;
    struct smap_node *node;
    int error;

    ds_put_cstr(ds, "\n");
    if (queue_id) {
        ds_put_format(ds, "Queue %u:\n", queue_id);
    } else {
        ds_put_cstr(ds, "Default:\n");
    }

    SMAP_FOR_EACH (node, details) {
        ds_put_format(ds, "\t%s: %s\n", node->key, node->value);
    }

    error = netdev_get_queue_stats(iface->netdev, queue_id, &stats);
    if (!error) {
        if (stats.tx_packets != UINT64_MAX) {
            ds_put_format(ds, "\ttx_packets: %"PRIu64"\n", stats.tx_packets);
        }

        if (stats.tx_bytes != UINT64_MAX) {
            ds_put_format(ds, "\ttx_bytes: %"PRIu64"\n", stats.tx_bytes);
        }

        if (stats.tx_errors != UINT64_MAX) {
            ds_put_format(ds, "\ttx_errors: %"PRIu64"\n", stats.tx_errors);
        }
    } else {
        ds_put_format(ds, "\tFailed to get statistics for queue %u: %s",
                      queue_id, ovs_strerror(error));
    }
}

static void
qos_unixctl_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                 const char *argv[], void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct smap smap = SMAP_INITIALIZER(&smap);
    struct iface *iface;
    const char *type;
    struct smap_node *node;

    iface = iface_find(argv[1]);
    if (!iface) {
        unixctl_command_reply_error(conn, "no such interface");
        return;
    }

    netdev_get_qos(iface->netdev, &type, &smap);

    if (*type != '\0') {
        struct netdev_queue_dump dump;
        struct smap details;
        unsigned int queue_id;

        ds_put_format(&ds, "QoS: %s %s\n", iface->name, type);

        SMAP_FOR_EACH (node, &smap) {
            ds_put_format(&ds, "%s: %s\n", node->key, node->value);
        }

        smap_init(&details);
        NETDEV_QUEUE_FOR_EACH (&queue_id, &details, &dump, iface->netdev) {
            qos_unixctl_show_queue(queue_id, &details, iface, &ds);
        }
        smap_destroy(&details);

        unixctl_command_reply(conn, ds_cstr(&ds));
    } else {
        ds_put_format(&ds, "QoS not configured on %s\n", iface->name);
        unixctl_command_reply_error(conn, ds_cstr(&ds));
    }

    smap_destroy(&smap);
    ds_destroy(&ds);
}
#endif

/* Bridge reconfiguration functions. */
static void
bridge_create(const struct ovsrec_bridge *br_cfg)
{
    struct bridge *br;
#ifdef OPS
    const struct ovsrec_open_vswitch* ovs = ovsrec_open_vswitch_first(idl);
#endif
    ovs_assert(!bridge_lookup(br_cfg->name));
    br = xzalloc(sizeof *br);

    br->name = xstrdup(br_cfg->name);
    br->type = xstrdup(ofproto_normalize_type(br_cfg->datapath_type));
    br->cfg = br_cfg;

#ifdef OPS
    /* Use system mac as default mac */
    memcpy(&br->default_ea, ether_aton(ovs->system_mac), ETH_ADDR_LEN);
#else
    /* Derive the default Ethernet address from the bridge's UUID.  This should
     * be unique and it will be stable between ovs-vswitchd runs.  */
    memcpy(&br->default_ea, &br_cfg->header_.uuid, ETH_ADDR_LEN);
    eth_addr_mark_random(br->default_ea);
#endif

    hmap_init(&br->ports);
    hmap_init(&br->ifaces);
    hmap_init(&br->iface_by_name);
#ifdef OPS
    hmap_init(&br->vlans);
#endif
#ifndef OPS_TEMP
    hmap_init(&br->mirrors);
#endif
    hmap_insert(&all_bridges, &br->node, hash_string(br->name, 0));
}

#ifdef OPS
static void
vrf_create(const struct ovsrec_vrf *vrf_cfg)
{
    struct vrf *vrf;
    const struct ovsrec_open_vswitch *ovs = ovsrec_open_vswitch_first(idl);

    ovs_assert(!vrf_lookup(vrf_cfg->name));
    vrf = xzalloc(sizeof *vrf);

    vrf->up = xzalloc(sizeof(*vrf->up));
    vrf->up->name = xstrdup(vrf_cfg->name);
    vrf->up->type = xstrdup("vrf");
    vrf->cfg = vrf_cfg;

    /* Use system mac as default mac */
    memcpy(&vrf->up->default_ea, ether_aton(ovs->system_mac), ETH_ADDR_LEN);

    hmap_init(&vrf->up->ports);
    hmap_init(&vrf->up->ifaces);
    hmap_init(&vrf->up->iface_by_name);
    hmap_init(&vrf->all_neighbors);
    hmap_init(&vrf->all_routes);
    hmap_init(&vrf->all_nexthops);
    hmap_insert(&all_vrfs, &vrf->node, hash_string(vrf->up->name, 0));
}
#endif


static void
bridge_destroy(struct bridge *br)
{
    if (br) {
#ifndef OPS_TEMP
        struct mirror *mirror, *next_mirror;
#endif
        struct port *port, *next_port;

        HMAP_FOR_EACH_SAFE (port, next_port, hmap_node, &br->ports) {
            port_destroy(port);
        }
#ifndef OPS_TEMP
        HMAP_FOR_EACH_SAFE (mirror, next_mirror, hmap_node, &br->mirrors) {
            mirror_destroy(mirror);
        }
#endif
        hmap_remove(&all_bridges, &br->node);
        ofproto_destroy(br->ofproto);
        hmap_destroy(&br->ifaces);
        hmap_destroy(&br->ports);
        hmap_destroy(&br->iface_by_name);
#ifdef OPS
        hmap_destroy(&br->vlans);
#endif
#ifndef OPS_TEMP
        hmap_destroy(&br->mirrors);
#endif
        free(br->name);
        free(br->type);
        free(br);
    }
}


#ifdef OPS
static void
vrf_destroy(struct vrf *vrf)
{
    if (vrf) {
        struct port *port, *next_port;

        /* Delete any neighbors, etc of this vrf */
        vrf_delete_all_neighbors(vrf);

        HMAP_FOR_EACH_SAFE (port, next_port, hmap_node, &vrf->up->ports) {
            port_destroy(port);
        }

        hmap_remove(&all_vrfs, &vrf->node);
        ofproto_destroy(vrf->up->ofproto);
        hmap_destroy(&vrf->up->ifaces);
        hmap_destroy(&vrf->up->ports);
        hmap_destroy(&vrf->up->iface_by_name);
        hmap_destroy(&vrf->all_neighbors);
        hmap_destroy(&vrf->all_routes);
        hmap_destroy(&vrf->all_nexthops);
        free(vrf->up->name);
        free(vrf->up);
        free(vrf);
    }
}

#endif

static struct bridge *
bridge_lookup(const char *name)
{
    struct bridge *br;

    HMAP_FOR_EACH_WITH_HASH (br, node, hash_string(name, 0), &all_bridges) {
        if (!strcmp(br->name, name)) {
            return br;
        }
    }
    return NULL;
}

#ifdef OPS
static struct vrf *
vrf_lookup(const char *name)
{
    struct vrf *vrf;

    HMAP_FOR_EACH_WITH_HASH (vrf, node, hash_string(name, 0), &all_vrfs) {
        if (!strcmp(vrf->up->name, name)) {
            return vrf;
        }
    }
    return NULL;
}
#endif

/* Handle requests for a listing of all flows known by the OpenFlow
 * stack, including those normally hidden. */
static void
bridge_unixctl_dump_flows(struct unixctl_conn *conn, int argc OVS_UNUSED,
                          const char *argv[], void *aux OVS_UNUSED)
{
    struct bridge *br;
    struct ds results;

    br = bridge_lookup(argv[1]);
    if (!br) {
        unixctl_command_reply_error(conn, "Unknown bridge");
        return;
    }

    ds_init(&results);
    ofproto_get_all_flows(br->ofproto, &results);

    unixctl_command_reply(conn, ds_cstr(&results));
    ds_destroy(&results);
}

/* "bridge/reconnect [BRIDGE]": makes BRIDGE drop all of its controller
 * connections and reconnect.  If BRIDGE is not specified, then all bridges
 * drop their controller connections and reconnect. */
static void
bridge_unixctl_reconnect(struct unixctl_conn *conn, int argc,
                         const char *argv[], void *aux OVS_UNUSED)
{
    struct bridge *br;
    if (argc > 1) {
        br = bridge_lookup(argv[1]);
        if (!br) {
            unixctl_command_reply_error(conn,  "Unknown bridge");
            return;
        }
#ifndef OPS_TEMP
        ofproto_reconnect_controllers(br->ofproto);
#endif
    }
#ifndef OPS_TEMP
    else {
        HMAP_FOR_EACH (br, node, &all_bridges) {
            ofproto_reconnect_controllers(br->ofproto);
        }
    }
#endif
    unixctl_command_reply(conn, NULL);
}

#ifndef OPS_TEMP
static size_t
bridge_get_controllers(const struct bridge *br,
                       struct ovsrec_controller ***controllersp)
{
    struct ovsrec_controller **controllers;
    size_t n_controllers;

    controllers = br->cfg->controller;
    n_controllers = br->cfg->n_controller;

    if (n_controllers == 1 && !strcmp(controllers[0]->target, "none")) {
        controllers = NULL;
        n_controllers = 0;
    }

    if (controllersp) {
        *controllersp = controllers;
    }
    return n_controllers;
}
#endif

static void
bridge_collect_wanted_ports(struct bridge *br,
#ifndef OPS_TEMP
                            const unsigned long int *splinter_vlans,
#else
                            const unsigned long int *splinter_vlans OVS_UNUSED,
#endif
                            struct shash *wanted_ports)
{
    size_t i;

    shash_init(wanted_ports);

    for (i = 0; i < br->cfg->n_ports; i++) {
        const char *name = br->cfg->ports[i]->name;
        if (!shash_add_once(wanted_ports, name, br->cfg->ports[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge port",
                      br->name, name);
        }
    }

#ifndef OPS_TEMP
    if (bridge_get_controllers(br, NULL)
        && !shash_find(wanted_ports, br->name)) {
        VLOG_WARN("bridge %s: no port named %s, synthesizing one",
                  br->name, br->name);

        ovsrec_interface_init(&br->synth_local_iface);
        ovsrec_port_init(&br->synth_local_port);

        br->synth_local_port.interfaces = &br->synth_local_ifacep;
        br->synth_local_port.n_interfaces = 1;
        br->synth_local_port.name = br->name;

        br->synth_local_iface.name = br->name;
        br->synth_local_iface.type = "internal";

        br->synth_local_ifacep = &br->synth_local_iface;

        shash_add(wanted_ports, br->name, &br->synth_local_port);
    }

    if (splinter_vlans) {
        add_vlan_splinter_ports(br, splinter_vlans, wanted_ports);
    }
#endif
}

#ifdef OPS
static void
vrf_collect_wanted_ports(struct vrf *vrf,
                         struct shash *wanted_ports)
{
    size_t i;

    shash_init(wanted_ports);

    for (i = 0; i < vrf->cfg->n_ports; i++) {
        const char *name = vrf->cfg->ports[i]->name;
        if (!shash_add_once(wanted_ports, name, vrf->cfg->ports[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge port",
                      vrf->up->name, name);
        }
    }
}
#endif

/* Deletes "struct port"s and "struct iface"s under 'br' which aren't
 * consistent with 'br->cfg'.  Updates 'br->if_cfg_queue' with interfaces which
 * 'br' needs to complete its configuration. */
static void
bridge_del_ports(struct bridge *br, const struct shash *wanted_ports)
{
    struct shash_node *port_node;
    struct port *port, *next;

    /* Get rid of deleted ports.
     * Get rid of deleted interfaces on ports that still exist. */
    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &br->ports) {
        port->cfg = shash_find_data(wanted_ports, port->name);
        if (!port->cfg) {
            port_destroy(port);
        } else {
            port_del_ifaces(port);
        }
    }

    /* Update iface->cfg and iface->type in interfaces that still exist. */
    SHASH_FOR_EACH (port_node, wanted_ports) {
        const struct ovsrec_port *port = port_node->data;
        size_t i;

        for (i = 0; i < port->n_interfaces; i++) {
            const struct ovsrec_interface *cfg = port->interfaces[i];
            struct iface *iface = iface_lookup(br, cfg->name);
            const char *type = iface_get_type(cfg, br->cfg);

            if (iface) {
                iface->cfg = cfg;
                iface->type = type;
            } else if (!strcmp(type, "null")) {
                VLOG_WARN_ONCE("%s: The null interface type is deprecated and"
                               " may be removed in February 2013. Please email"
                               " dev@openvswitch.org with concerns.",
                               cfg->name);
            } else {
                /* We will add new interfaces later. */
            }
        }
    }
}

#ifdef OPS
static void
vrf_del_ports(struct vrf *vrf, const struct shash *wanted_ports)
{
    struct shash_node *port_node;
    struct port *port, *next;

    /* Get rid of deleted ports.
     * Get rid of deleted interfaces on ports that still exist. */
    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &vrf->up->ports) {
        port->cfg = shash_find_data(wanted_ports, port->name);
        if (!port->cfg) {
            /* Delete the neighbors referring the deleted vrf ports */
            vrf_delete_port_neighbors(vrf, port);
            port_destroy(port);
        } else {
            port_del_ifaces(port);
        }
    }

    /* Update iface->cfg and iface->type in interfaces that still exist. */
    SHASH_FOR_EACH (port_node, wanted_ports) {
        const struct ovsrec_port *port = port_node->data;
        size_t i;

        for (i = 0; i < port->n_interfaces; i++) {
            const struct ovsrec_interface *cfg = port->interfaces[i];
            struct iface *iface = iface_lookup(vrf->up, cfg->name);
            const char *type = iface_get_type(cfg, NULL);

            if (iface) {
                iface->cfg = cfg;
                iface->type = type;
            } else if (!strcmp(type, "null")) {
                VLOG_WARN_ONCE("%s: The null interface type is deprecated and"
                               " may be removed in February 2013. Please email"
                               " dev@openvswitch.org with concerns.",
                               cfg->name);
            } else {
                /* We will add new interfaces later. */
            }
        }
    }
}
#endif

#ifndef OPS_TEMP
/* Initializes 'oc' appropriately as a management service controller for
 * 'br'.
 *
 * The caller must free oc->target when it is no longer needed. */
static void
bridge_ofproto_controller_for_mgmt(const struct bridge *br,
                                   struct ofproto_controller *oc)
{
    oc->target = xasprintf("punix:%s/%s.mgmt", ovs_rundir(), br->name);
    oc->max_backoff = 0;
    oc->probe_interval = 60;
    oc->band = OFPROTO_OUT_OF_BAND;
    oc->rate_limit = 0;
    oc->burst_limit = 0;
    oc->enable_async_msgs = true;
    oc->dscp = 0;
}

/* Converts ovsrec_controller 'c' into an ofproto_controller in 'oc'.  */
static void
bridge_ofproto_controller_from_ovsrec(const struct ovsrec_controller *c,
                                      struct ofproto_controller *oc)
{
    int dscp;

    oc->target = c->target;
    oc->max_backoff = c->max_backoff ? *c->max_backoff / 1000 : 8;
    oc->probe_interval = c->inactivity_probe ? *c->inactivity_probe / 1000 : 5;
    oc->band = (!c->connection_mode || !strcmp(c->connection_mode, "in-band")
                ? OFPROTO_IN_BAND : OFPROTO_OUT_OF_BAND);
    oc->rate_limit = c->controller_rate_limit ? *c->controller_rate_limit : 0;
    oc->burst_limit = (c->controller_burst_limit
                       ? *c->controller_burst_limit : 0);
    oc->enable_async_msgs = (!c->enable_async_messages
                             || *c->enable_async_messages);
    dscp = smap_get_int(&c->other_config, "dscp", DSCP_DEFAULT);
    if (dscp < 0 || dscp > 63) {
        dscp = DSCP_DEFAULT;
    }
    oc->dscp = dscp;
}

/* Configures the IP stack for 'br''s local interface properly according to the
 * configuration in 'c'.  */
static void
bridge_configure_local_iface_netdev(struct bridge *br, struct ovsrec_controller *c)
{
    struct netdev *netdev;
    struct in_addr mask, gateway;

    struct iface *local_iface;
    struct in_addr ip;

    /* If there's no local interface or no IP address, give up. */
    local_iface = iface_from_ofp_port(br, OFPP_LOCAL);
    if (!local_iface || !c->local_ip
        || !inet_pton(AF_INET, c->local_ip, &ip)) {
        return;
    }

    /* Bring up the local interface. */
    netdev = local_iface->netdev;
    netdev_turn_flags_on(netdev, NETDEV_UP, NULL);

    /* Configure the IP address and netmask. */
    if (!c->local_netmask
        || !inet_pton(AF_INET, c->local_netmask, &mask)
        || !mask.s_addr) {
        mask.s_addr = guess_netmask(ip.s_addr);
    }
    if (!netdev_set_in4(netdev, ip, mask)) {
        VLOG_INFO("bridge %s: configured IP address "IP_FMT", netmask "IP_FMT,
                  br->name, IP_ARGS(ip.s_addr), IP_ARGS(mask.s_addr));
    }

    /* Configure the default gateway. */
    if (c->local_gateway
        && inet_pton(AF_INET, c->local_gateway, &gateway)
        && gateway.s_addr) {
        if (!netdev_add_router(netdev, gateway)) {
            VLOG_INFO("bridge %s: configured gateway "IP_FMT,
                      br->name, IP_ARGS(gateway.s_addr));
        }
    }
}

/* Returns true if 'a' and 'b' are the same except that any number of slashes
 * in either string are treated as equal to any number of slashes in the other,
 * e.g. "x///y" is equal to "x/y".
 *
 * Also, if 'b_stoplen' bytes from 'b' are found to be equal to corresponding
 * bytes from 'a', the function considers this success.  Specify 'b_stoplen' as
 * SIZE_MAX to compare all of 'a' to all of 'b' rather than just a prefix of
 * 'b' against a prefix of 'a'.
 */
static bool
equal_pathnames(const char *a, const char *b, size_t b_stoplen)
{
    const char *b_start = b;
    for (;;) {
        if (b - b_start >= b_stoplen) {
            return true;
        } else if (*a != *b) {
            return false;
        } else if (*a == '/') {
            a += strspn(a, "/");
            b += strspn(b, "/");
        } else if (*a == '\0') {
            return true;
        } else {
            a++;
            b++;
        }
    }
}

static void
bridge_configure_remotes(struct bridge *br,
                         const struct sockaddr_in *managers, size_t n_managers)
{
    bool disable_in_band;

    struct ovsrec_controller **controllers;
    size_t n_controllers;

    enum ofproto_fail_mode fail_mode;

    struct ofproto_controller *ocs;
    size_t n_ocs;
    size_t i;

    /* Check if we should disable in-band control on this bridge. */
    disable_in_band = smap_get_bool(&br->cfg->other_config, "disable-in-band",
                                    false);

    /* Set OpenFlow queue ID for in-band control. */
    ofproto_set_in_band_queue(br->ofproto,
                              smap_get_int(&br->cfg->other_config,
                                           "in-band-queue", -1));

    if (disable_in_band) {
        ofproto_set_extra_in_band_remotes(br->ofproto, NULL, 0);
    } else {
        ofproto_set_extra_in_band_remotes(br->ofproto, managers, n_managers);
    }

    n_controllers = bridge_get_controllers(br, &controllers);

    ocs = xmalloc((n_controllers + 1) * sizeof *ocs);
    n_ocs = 0;

    bridge_ofproto_controller_for_mgmt(br, &ocs[n_ocs++]);
    for (i = 0; i < n_controllers; i++) {
        struct ovsrec_controller *c = controllers[i];

        if (!strncmp(c->target, "punix:", 6)
            || !strncmp(c->target, "unix:", 5)) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
            char *whitelist;

            if (!strncmp(c->target, "unix:", 5)) {
                /* Connect to a listening socket */
                whitelist = xasprintf("unix:%s/", ovs_rundir());
                if (strchr(c->target, '/') &&
                   !equal_pathnames(c->target, whitelist,
                     strlen(whitelist))) {
                    /* Absolute path specified, but not in ovs_rundir */
                    VLOG_ERR_RL(&rl, "bridge %s: Not connecting to socket "
                                  "controller \"%s\" due to possibility for "
                                  "remote exploit.  Instead, specify socket "
                                  "in whitelisted \"%s\" or connect to "
                                  "\"unix:%s/%s.mgmt\" (which is always "
                                  "available without special configuration).",
                                  br->name, c->target, whitelist,
                                  ovs_rundir(), br->name);
                    free(whitelist);
                    continue;
                }
            } else {
               whitelist = xasprintf("punix:%s/%s.controller",
                                     ovs_rundir(), br->name);
               if (!equal_pathnames(c->target, whitelist, SIZE_MAX)) {
                   /* Prevent remote ovsdb-server users from accessing
                    * arbitrary Unix domain sockets and overwriting arbitrary
                    * local files. */
                   VLOG_ERR_RL(&rl, "bridge %s: Not adding Unix domain socket "
                                  "controller \"%s\" due to possibility of "
                                  "overwriting local files. Instead, specify "
                                  "whitelisted \"%s\" or connect to "
                                  "\"unix:%s/%s.mgmt\" (which is always "
                                  "available without special configuration).",
                                  br->name, c->target, whitelist,
                                  ovs_rundir(), br->name);
                   free(whitelist);
                   continue;
               }
            }

            free(whitelist);
        }
        bridge_configure_local_iface_netdev(br, c);
        bridge_ofproto_controller_from_ovsrec(c, &ocs[n_ocs]);
        if (disable_in_band) {
            ocs[n_ocs].band = OFPROTO_OUT_OF_BAND;
        }
        n_ocs++;
    }

    ofproto_set_controllers(br->ofproto, ocs, n_ocs,
                            bridge_get_allowed_versions(br));
    free(ocs[0].target); /* From bridge_ofproto_controller_for_mgmt(). */
    free(ocs);

    /* Set the fail-mode. */
    fail_mode = !br->cfg->fail_mode
                || !strcmp(br->cfg->fail_mode, "standalone")
                    ? OFPROTO_FAIL_STANDALONE
                    : OFPROTO_FAIL_SECURE;
    ofproto_set_fail_mode(br->ofproto, fail_mode);

    /* Configure OpenFlow controller connection snooping. */
    if (!ofproto_has_snoops(br->ofproto)) {
        struct sset snoops;

        sset_init(&snoops);
        sset_add_and_free(&snoops, xasprintf("punix:%s/%s.snoop",
                                             ovs_rundir(), br->name));
        ofproto_set_snoops(br->ofproto, &snoops);
        sset_destroy(&snoops);
    }
}

static void
bridge_configure_tables(struct bridge *br)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
    int n_tables;
    int i, j, k;

    n_tables = ofproto_get_n_tables(br->ofproto);
    j = 0;
    for (i = 0; i < n_tables; i++) {
        struct ofproto_table_settings s;
        bool use_default_prefixes = true;

        s.name = NULL;
        s.max_flows = UINT_MAX;
        s.groups = NULL;
        s.n_groups = 0;
        s.n_prefix_fields = 0;
        memset(s.prefix_fields, ~0, sizeof(s.prefix_fields));

        if (j < br->cfg->n_flow_tables && i == br->cfg->key_flow_tables[j]) {
            struct ovsrec_flow_table *cfg = br->cfg->value_flow_tables[j++];

            s.name = cfg->name;
            if (cfg->n_flow_limit && *cfg->flow_limit < UINT_MAX) {
                s.max_flows = *cfg->flow_limit;
            }
            if (cfg->overflow_policy
                && !strcmp(cfg->overflow_policy, "evict")) {

                s.groups = xmalloc(cfg->n_groups * sizeof *s.groups);
                for (k = 0; k < cfg->n_groups; k++) {
                    const char *string = cfg->groups[k];
                    char *msg;

                    msg = mf_parse_subfield__(&s.groups[k], &string);
                    if (msg) {
                        VLOG_WARN_RL(&rl, "bridge %s table %d: error parsing "
                                     "'groups' (%s)", br->name, i, msg);
                        free(msg);
                    } else if (*string) {
                        VLOG_WARN_RL(&rl, "bridge %s table %d: 'groups' "
                                     "element '%s' contains trailing garbage",
                                     br->name, i, cfg->groups[k]);
                    } else {
                        s.n_groups++;
                    }
                }
            }
            /* Prefix lookup fields. */
            s.n_prefix_fields = 0;
            for (k = 0; k < cfg->n_prefixes; k++) {
                const char *name = cfg->prefixes[k];
                const struct mf_field *mf;

                if (strcmp(name, "none") == 0) {
                    use_default_prefixes = false;
                    s.n_prefix_fields = 0;
                    break;
                }
                mf = mf_from_name(name);
                if (!mf) {
                    VLOG_WARN("bridge %s: 'prefixes' with unknown field: %s",
                              br->name, name);
                    continue;
                }
                if (mf->flow_be32ofs < 0 || mf->n_bits % 32) {
                    VLOG_WARN("bridge %s: 'prefixes' with incompatible field: "
                              "%s", br->name, name);
                    continue;
                }
                if (s.n_prefix_fields >= ARRAY_SIZE(s.prefix_fields)) {
                    VLOG_WARN("bridge %s: 'prefixes' with too many fields, "
                              "field not used: %s", br->name, name);
                    continue;
                }
                use_default_prefixes = false;
                s.prefix_fields[s.n_prefix_fields++] = mf->id;
            }
        }
        if (use_default_prefixes) {
            /* Use default values. */
            s.n_prefix_fields = ARRAY_SIZE(default_prefix_fields);
            memcpy(s.prefix_fields, default_prefix_fields,
                   sizeof default_prefix_fields);
        } else {
            int k;
            struct ds ds = DS_EMPTY_INITIALIZER;
            for (k = 0; k < s.n_prefix_fields; k++) {
                if (k) {
                    ds_put_char(&ds, ',');
                }
                ds_put_cstr(&ds, mf_from_id(s.prefix_fields[k])->name);
            }
            if (s.n_prefix_fields == 0) {
                ds_put_cstr(&ds, "none");
            }
            VLOG_DBG("bridge %s table %d: Prefix lookup with: %s.",
                      br->name, i, ds_cstr(&ds));
            ds_destroy(&ds);
        }

        ofproto_configure_table(br->ofproto, i, &s);

        free(s.groups);
    }
    for (; j < br->cfg->n_flow_tables; j++) {
        VLOG_WARN_RL(&rl, "bridge %s: ignoring configuration for flow table "
                     "%"PRId64" not supported by this datapath", br->name,
                     br->cfg->key_flow_tables[j]);
    }
}
#endif
static void
bridge_configure_dp_desc(struct bridge *br)
{
    ofproto_set_dp_desc(br->ofproto,
                        smap_get(&br->cfg->other_config, "dp-desc"));
}

#ifdef OPS

/* VLAN functions. */
static struct vlan *
vlan_lookup_by_name(const struct bridge *br, const char *name)
{
    struct vlan *vlan;

    HMAP_FOR_EACH_WITH_HASH (vlan, hmap_node, hash_string(name, 0),
                             &br->vlans) {
        if (!strcmp(vlan->name, name)) {
            return vlan;
        }
    }
    return NULL;
}

static struct vlan *
vlan_lookup_by_vid(const struct bridge *br, int vid)
{
    struct vlan *vlan;

    HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
        if (vlan->vid == vid) {
            return vlan;
        }
    }
    return NULL;
}

static void
dump_vlan_data(struct ds *ds, struct vlan *vlan)
{
    ds_put_format(ds, "VLAN %d:\n", vlan->vid);
    ds_put_format(ds, "  name               :%s\n", vlan->name);
    ds_put_format(ds, "  cfg                :%p\n", vlan->cfg);
    ds_put_format(ds, "  hw_vlan_cfg:enable :%d\n", vlan->enable);
}

static void
vlan_unixctl_show(struct unixctl_conn *conn, int argc,
                  const char *argv[], void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct vlan *vlan = NULL;
    struct bridge *br;

    HMAP_FOR_EACH (br, node, &all_bridges) {
        ds_put_format(&ds, "========== Bridge %s ==========\n", br->name);

        /* Check for optional VID parameter.  We'll accept
         * either an integer VID or name of VLAN. */
        if (argc > 1) {
            int vid = strtol(argv[1], NULL, 10);
            if (vid > 0) {
                vlan = vlan_lookup_by_vid(br, vid);
            } else {
                vlan = vlan_lookup_by_name(br, argv[1]);
            }
            if (vlan == NULL) {
                ds_put_format(&ds, "VLAN %s is not in this bridge.\n",
                              argv[1]);
                continue;
            }
        }

        if (vlan != NULL) {
            dump_vlan_data(&ds, vlan);
        } else {
            HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
                dump_vlan_data(&ds, vlan);
            }
        }
    }

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
vlan_create(struct bridge *br, const struct ovsrec_vlan *vlan_cfg)
{
    struct vlan *new_vlan = NULL;
#ifndef OPS
    const char *hw_cfg_enable;
#endif

    /* Allocate structure to save state information for this VLAN. */
    new_vlan = xzalloc(sizeof(struct vlan));

    hmap_insert(&br->vlans, &new_vlan->hmap_node,
                hash_string(vlan_cfg->name, 0));

    new_vlan->bridge = br;
    new_vlan->cfg = vlan_cfg;
    new_vlan->vid = (int)vlan_cfg->id;
    new_vlan->name = xstrdup(vlan_cfg->name);

    /* Initialize state to disabled.  Will handle this later. */
    new_vlan->enable = false;
}

static void
vlan_destroy(struct vlan *vlan)
{
    if (vlan) {
        struct bridge *br = vlan->bridge;
        hmap_remove(&br->vlans, &vlan->hmap_node);
        free(vlan->name);
        free(vlan);
    }
}

static void
bridge_configure_vlans(struct bridge *br)
{
    size_t i;
    struct vlan *vlan, *next;
    struct shash sh_idl_vlans;
    struct shash_node *sh_node;

    /* Collect all the VLANs present in the DB. */
    shash_init(&sh_idl_vlans);
    for (i = 0; i < br->cfg->n_vlans; i++) {
        const char *name = br->cfg->vlans[i]->name;
        if (!shash_add_once(&sh_idl_vlans, name, br->cfg->vlans[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge VLAN",
                      br->name, name);
        }
    }

    /* Delete old VLANs. */
    HMAP_FOR_EACH_SAFE (vlan, next, hmap_node, &br->vlans) {
        const struct ovsrec_vlan *vlan_cfg;

        vlan_cfg = shash_find_data(&sh_idl_vlans, vlan->name);
        if (!vlan_cfg) {
            VLOG_DBG("Found a deleted VLAN %s", vlan->name);
            /* Need to update ofproto now since this VLAN
             * won't be around for the "check for changes"
             * loop below. */
            ofproto_set_vlan(br->ofproto, vlan->vid, 0);
            vlan_destroy(vlan);
        }
    }

    /* Add new VLANs. */
    SHASH_FOR_EACH (sh_node, &sh_idl_vlans) {
        vlan = vlan_lookup_by_name(br, sh_node->name);
        if (!vlan) {
            VLOG_DBG("Found an added VLAN %s", sh_node->name);
            vlan_create(br, sh_node->data);
        }
    }

    /* Check for changes in the VLAN row entries. */
    HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
        const struct ovsrec_vlan *row = vlan->cfg;

        /* Check for changes to row. */
        if (OVSREC_IDL_IS_ROW_INSERTED(row, idl_seqno) ||
            OVSREC_IDL_IS_ROW_MODIFIED(row, idl_seqno)) {
            bool new_enable = false;
            const char *hw_cfg_enable;

            // Check for hw_vlan_config:enable string changes.
            hw_cfg_enable = smap_get(&row->hw_vlan_config, VLAN_HW_CONFIG_MAP_ENABLE);
            if (hw_cfg_enable) {
                if (!strcmp(hw_cfg_enable, VLAN_HW_CONFIG_MAP_ENABLE_TRUE)) {
                    new_enable = true;
                }
            }

            if (new_enable != vlan->enable) {
                VLOG_DBG("  VLAN %d changed, enable=%d, new_enable=%d.  "
                         "idl_seq=%d, insert=%d, mod=%d",
                         vlan->vid, vlan->enable, new_enable, idl_seqno,
                         row->header_.insert_seqno,
                         row->header_.modify_seqno);

                vlan->enable = new_enable;
                ofproto_set_vlan(br->ofproto, vlan->vid, vlan->enable);
            }
        }
    }

    /* Destroy the shash of the IDL vlans */
    shash_destroy(&sh_idl_vlans);
}
#endif


/* Port functions. */

static struct port *
port_create(struct bridge *br, const struct ovsrec_port *cfg)
{
    struct port *port;

    port = xzalloc(sizeof *port);
    port->bridge = br;
    port->name = xstrdup(cfg->name);
    port->cfg = cfg;
#ifdef OPS
    port->bond_hw_handle = -1;
#endif
    list_init(&port->ifaces);

    hmap_insert(&br->ports, &port->hmap_node, hash_string(port->name, 0));
    return port;
}

/* Deletes interfaces from 'port' that are no longer configured for it. */
static void
port_del_ifaces(struct port *port)
{
    struct iface *iface, *next;
    struct sset new_ifaces;
    size_t i;

    /* Collect list of new interfaces. */
    sset_init(&new_ifaces);
    for (i = 0; i < port->cfg->n_interfaces; i++) {
        const char *name = port->cfg->interfaces[i]->name;
        const char *type = port->cfg->interfaces[i]->type;
        if (strcmp(type, "null")) {
            sset_add(&new_ifaces, name);
        }
    }

    /* Get rid of deleted interfaces. */
    LIST_FOR_EACH_SAFE (iface, next, port_elem, &port->ifaces) {
        if (!sset_contains(&new_ifaces, iface->name)) {
            iface_destroy(iface);
        }
    }

    sset_destroy(&new_ifaces);
}

static void
port_destroy(struct port *port)
{
    if (port) {
        struct bridge *br = port->bridge;
        struct iface *iface, *next;

        if (br->ofproto) {
            ofproto_bundle_unregister(br->ofproto, port);
        }

        LIST_FOR_EACH_SAFE (iface, next, port_elem, &port->ifaces) {
            iface_destroy__(iface);
        }

        hmap_remove(&br->ports, &port->hmap_node);
        free(port->name);
        free(port);
    }
}

static struct port *
port_lookup(const struct bridge *br, const char *name)
{
    struct port *port;

    HMAP_FOR_EACH_WITH_HASH (port, hmap_node, hash_string(name, 0),
                             &br->ports) {
        if (!strcmp(port->name, name)) {
            return port;
        }
    }
    return NULL;
}

static bool
enable_lacp(struct port *port, bool *activep)
{
    if (!port->cfg->lacp) {
        /* XXX when LACP implementation has been sufficiently tested, enable by
         * default and make active on bonded ports. */
        return false;
    } else if (!strcmp(port->cfg->lacp, "off")) {
        return false;
    } else if (!strcmp(port->cfg->lacp, "active")) {
        *activep = true;
        return true;
    } else if (!strcmp(port->cfg->lacp, "passive")) {
        *activep = false;
        return true;
    } else {
        VLOG_WARN("port %s: unknown LACP mode %s",
                  port->name, port->cfg->lacp);
        return false;
    }
}

#ifndef OPS
static struct lacp_settings *
port_configure_lacp(struct port *port, struct lacp_settings *s)
{
    const char *lacp_time, *system_id;
    int priority;

    if (!enable_lacp(port, &s->active)) {
        return NULL;
    }

    s->name = port->name;

    system_id = smap_get(&port->cfg->other_config, "lacp-system-id");
    if (system_id) {
        if (!ovs_scan(system_id, ETH_ADDR_SCAN_FMT,
                      ETH_ADDR_SCAN_ARGS(s->id))) {
            VLOG_WARN("port %s: LACP system ID (%s) must be an Ethernet"
                      " address.", port->name, system_id);
            return NULL;
        }
    } else {
        memcpy(s->id, port->bridge->ea, ETH_ADDR_LEN);
    }

    if (eth_addr_is_zero(s->id)) {
        VLOG_WARN("port %s: Invalid zero LACP system ID.", port->name);
        return NULL;
    }

    /* Prefer bondable links if unspecified. */
    priority = smap_get_int(&port->cfg->other_config, "lacp-system-priority",
                            0);
    s->priority = (priority > 0 && priority <= UINT16_MAX
                   ? priority
                   : UINT16_MAX - !list_is_short(&port->ifaces));

    lacp_time = smap_get(&port->cfg->other_config, "lacp-time");
    s->fast = lacp_time && !strcasecmp(lacp_time, "fast");

    s->fallback_ab_cfg = smap_get_bool(&port->cfg->other_config,
                                       "lacp-fallback-ab", false);

    return s;
}

static void
iface_configure_lacp(struct iface *iface, struct lacp_slave_settings *s)
{
    int priority, portid, key;

    portid = smap_get_int(&iface->cfg->other_config, "lacp-port-id", 0);
    priority = smap_get_int(&iface->cfg->other_config, "lacp-port-priority",
                            0);
    key = smap_get_int(&iface->cfg->other_config, "lacp-aggregation-key", 0);

    if (portid <= 0 || portid > UINT16_MAX) {
        portid = ofp_to_u16(iface->ofp_port);
    }

    if (priority <= 0 || priority > UINT16_MAX) {
        priority = UINT16_MAX;
    }

    if (key < 0 || key > UINT16_MAX) {
        key = 0;
    }

    s->name = iface->name;
    s->id = portid;
    s->priority = priority;
    s->key = key;
}
#endif

static void
port_configure_bond(struct port *port, struct bond_settings *s)
{
    const char *detect_s;
    struct iface *iface;
    const char *mac_s;
    int miimon_interval;

    s->name = port->name;
#ifdef OPS
    s->balance = BM_L3_SRC_DST_HASH;
#else
    s->balance = BM_AB;
#endif
    if (port->cfg->bond_mode) {
        if (!bond_mode_from_string(&s->balance, port->cfg->bond_mode)) {
            VLOG_WARN("port %s: unknown bond_mode %s, defaulting to %s",
                      port->name, port->cfg->bond_mode,
                      bond_mode_to_string(s->balance));
        }
    } else {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);

        /* XXX: Post version 1.5.*, the default bond_mode changed from SLB to
         * active-backup. At some point we should remove this warning. */
        VLOG_WARN_RL(&rl, "port %s: Using the default bond_mode %s. Note that"
                     " in previous versions, the default bond_mode was"
                     " balance-slb", port->name,
                     bond_mode_to_string(s->balance));
    }

#ifdef OPS
    VLOG_DBG("port %s: bond_mode is set to %s",
                      port->name, bond_mode_to_string(s->balance));
#endif

#ifndef OPS_TEMP
    if (s->balance == BM_SLB && port->bridge->cfg->n_flood_vlans) {
        VLOG_WARN("port %s: SLB bonds are incompatible with flood_vlans, "
                  "please use another bond type or disable flood_vlans",
                  port->name);
    }
#endif

    miimon_interval = smap_get_int(&port->cfg->other_config,
                                   "bond-miimon-interval", 0);
    if (miimon_interval <= 0) {
        miimon_interval = 200;
    }

    detect_s = smap_get(&port->cfg->other_config, "bond-detect-mode");
    if (!detect_s || !strcmp(detect_s, "carrier")) {
        miimon_interval = 0;
    } else if (strcmp(detect_s, "miimon")) {
        VLOG_WARN("port %s: unsupported bond-detect-mode %s, "
                  "defaulting to carrier", port->name, detect_s);
        miimon_interval = 0;
    }

#ifndef OPS_TEMP
    s->up_delay = MAX(0, port->cfg->bond_updelay);
    s->down_delay = MAX(0, port->cfg->bond_downdelay);
#endif
    s->basis = smap_get_int(&port->cfg->other_config, "bond-hash-basis", 0);
    s->rebalance_interval = smap_get_int(&port->cfg->other_config,
                                           "bond-rebalance-interval", 10000);
    if (s->rebalance_interval && s->rebalance_interval < 1000) {
        s->rebalance_interval = 1000;
    }

    s->lacp_fallback_ab_cfg = smap_get_bool(&port->cfg->other_config,
                                       "lacp-fallback-ab", false);

    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        netdev_set_miimon_interval(iface->netdev, miimon_interval);
    }

    mac_s = port->cfg->bond_active_slave;
    if (!mac_s || !ovs_scan(mac_s, ETH_ADDR_SCAN_FMT,
                            ETH_ADDR_SCAN_ARGS(s->active_slave_mac))) {
        /* OVSDB did not store the last active interface */
        s->active_slave_mac = eth_addr_zero;
    }
}

#ifndef OPS_TEMP
/* Returns true if 'port' is synthetic, that is, if we constructed it locally
 * instead of obtaining it from the database. */
static bool
port_is_synthetic(const struct port *port)
{
    return ovsdb_idl_row_is_synthetic(&port->cfg->header_);
}
#endif

/* Interface functions. */

static bool
iface_is_internal(const struct ovsrec_interface *iface,
                  const struct ovsrec_bridge *br)
{
    /* The local port and "internal" ports are always "internal". */
    return !strcmp(iface->type, "internal") ||
           (br && !strcmp(iface->name, br->name));
}

/* Returns the correct network device type for interface 'iface' in bridge
 * 'br'. */
static const char *
iface_get_type(const struct ovsrec_interface *iface,
               const struct ovsrec_bridge *br)
{
    const char *type;

    /* The local port always has type "internal".  Other ports take
     * their type from the database and default to "system" if none is
     * specified. */
    if (iface_is_internal(iface, br)) {
        type = "internal";
    } else {
        type = iface->type[0] ? iface->type : "system";
    }
    return ofproto_port_open_type(br ? br->datapath_type : "vrf", type);
}

static void
iface_destroy__(struct iface *iface)
{
    if (iface) {
        struct port *port = iface->port;
        struct bridge *br = port->bridge;

        if (br->ofproto && iface->ofp_port != OFPP_NONE) {
            ofproto_port_unregister(br->ofproto, iface->ofp_port);
        }

        if (iface->ofp_port != OFPP_NONE) {
            hmap_remove(&br->ifaces, &iface->ofp_port_node);
        }

        list_remove(&iface->port_elem);
        hmap_remove(&br->iface_by_name, &iface->name_node);

        /* The user is changing configuration here, so netdev_remove needs to be
         * used as opposed to netdev_close */
        netdev_remove(iface->netdev);

        free(iface->name);
        free(iface);
    }
}

static void
iface_destroy(struct iface *iface)
{
    if (iface) {
        struct port *port = iface->port;

        iface_destroy__(iface);
        if (list_is_empty(&port->ifaces)) {
            port_destroy(port);
        }
    }
}

static struct iface *
iface_lookup(const struct bridge *br, const char *name)
{
    struct iface *iface;

    HMAP_FOR_EACH_WITH_HASH (iface, name_node, hash_string(name, 0),
                             &br->iface_by_name) {
        if (!strcmp(iface->name, name)) {
            return iface;
        }
    }

    return NULL;
}

#ifndef OPS_TEMP
static struct iface *
iface_find(const char *name)
{
    const struct bridge *br;

    HMAP_FOR_EACH (br, node, &all_bridges) {
        struct iface *iface = iface_lookup(br, name);

        if (iface) {
            return iface;
        }
    }
    return NULL;
}
#endif

static struct iface *
iface_from_ofp_port(const struct bridge *br, ofp_port_t ofp_port)
{
    struct iface *iface;

    HMAP_FOR_EACH_IN_BUCKET (iface, ofp_port_node, hash_ofp_port(ofp_port),
                             &br->ifaces) {
        if (iface->ofp_port == ofp_port) {
            return iface;
        }
    }
    return NULL;
}

#ifndef OPS_TEMP
/* Set Ethernet address of 'iface', if one is specified in the configuration
 * file. */
static void
iface_set_mac(const struct bridge *br, const struct port *port, struct iface *iface)
{
    uint8_t ea[ETH_ADDR_LEN], *mac = NULL;
    struct iface *hw_addr_iface;

    if (strcmp(iface->type, "internal")) {
        return;
    }

    if (iface->cfg->mac && eth_addr_from_string(iface->cfg->mac, ea)) {
        mac = ea;
    } else if (port->cfg->fake_bridge) {
        /* Fake bridge and no MAC set in the configuration. Pick a local one. */
        find_local_hw_addr(br, ea, port, &hw_addr_iface);
        mac = ea;
    }

    if (mac) {
        if (iface->ofp_port == OFPP_LOCAL) {
            VLOG_ERR("interface %s: ignoring mac in Interface record "
                     "(use Bridge record to set local port's mac)",
                     iface->name);
        } else if (eth_addr_is_multicast(mac)) {
            VLOG_ERR("interface %s: cannot set MAC to multicast address",
                     iface->name);
        } else {
            int error = netdev_set_etheraddr(iface->netdev, mac);
            if (error) {
                VLOG_ERR("interface %s: setting MAC failed (%s)",
                         iface->name, ovs_strerror(error));
            }
        }
    }
}

/* Sets the ofport column of 'if_cfg' to 'ofport'. */
static void
iface_set_ofport(const struct ovsrec_interface *if_cfg, ofp_port_t ofport)
{
    if (if_cfg && !ovsdb_idl_row_is_synthetic(&if_cfg->header_)) {
        int64_t port = ofport == OFPP_NONE ? -1 : ofp_to_u16(ofport);
        ovsrec_interface_set_ofport(if_cfg, &port, 1);
    }
}
#endif
/* Clears all of the fields in 'if_cfg' that indicate interface status, and
 * sets the "ofport" field to -1.
 *
 * This is appropriate when 'if_cfg''s interface cannot be created or is
 * otherwise invalid. */
static void
#ifndef OPS_TEMP
iface_clear_db_record(const struct ovsrec_interface *if_cfg, char *errp)
#else
iface_clear_db_record(const struct ovsrec_interface *if_cfg, char *errp OVS_UNUSED)
#endif
{
    if (!ovsdb_idl_row_is_synthetic(&if_cfg->header_)) {
#ifndef OPS_TEMP
        iface_set_ofport(if_cfg, OFPP_NONE);
        ovsrec_interface_set_error(if_cfg, errp);
#endif
        ovsrec_interface_set_status(if_cfg, NULL);
        ovsrec_interface_set_admin_state(if_cfg, NULL);
        ovsrec_interface_set_duplex(if_cfg, NULL);
        ovsrec_interface_set_link_speed(if_cfg, NULL, 0);
        ovsrec_interface_set_link_state(if_cfg, NULL);
        ovsrec_interface_set_mac_in_use(if_cfg, NULL);
        ovsrec_interface_set_mtu(if_cfg, NULL, 0);
#ifndef OPS_TEMP
        ovsrec_interface_set_cfm_fault(if_cfg, NULL, 0);
        ovsrec_interface_set_cfm_fault_status(if_cfg, NULL, 0);
        ovsrec_interface_set_cfm_remote_mpids(if_cfg, NULL, 0);
        ovsrec_interface_set_lacp_current(if_cfg, NULL, 0);
#endif
        ovsrec_interface_set_statistics(if_cfg, NULL, NULL, 0);
#ifndef OPS_TEMP
        ovsrec_interface_set_ifindex(if_cfg, NULL, 0);
#endif
    }
}

#ifndef OPS_TEMP
static bool
queue_ids_include(const struct ovsdb_datum *queues, int64_t target)
{
    union ovsdb_atom atom;

    atom.integer = target;
    return ovsdb_datum_find_key(queues, &atom, OVSDB_TYPE_INTEGER) != UINT_MAX;
}

static void
iface_configure_qos(struct iface *iface, const struct ovsrec_qos *qos)
{
    struct ofpbuf queues_buf;

    ofpbuf_init(&queues_buf, 0);

    if (!qos || qos->type[0] == '\0' || qos->n_queues < 1) {
        netdev_set_qos(iface->netdev, NULL, NULL);
    } else {
        const struct ovsdb_datum *queues;
        struct netdev_queue_dump dump;
        unsigned int queue_id;
        struct smap details;
        bool queue_zero;
        size_t i;

        /* Configure top-level Qos for 'iface'. */
        netdev_set_qos(iface->netdev, qos->type, &qos->other_config);

        /* Deconfigure queues that were deleted. */
        queues = ovsrec_qos_get_queues(qos, OVSDB_TYPE_INTEGER,
                                       OVSDB_TYPE_UUID);
        smap_init(&details);
        NETDEV_QUEUE_FOR_EACH (&queue_id, &details, &dump, iface->netdev) {
            if (!queue_ids_include(queues, queue_id)) {
                netdev_delete_queue(iface->netdev, queue_id);
            }
        }
        smap_destroy(&details);

        /* Configure queues for 'iface'. */
        queue_zero = false;
        for (i = 0; i < qos->n_queues; i++) {
            const struct ovsrec_queue *queue = qos->value_queues[i];
            unsigned int queue_id = qos->key_queues[i];

            if (queue_id == 0) {
                queue_zero = true;
            }

            if (queue->n_dscp == 1) {
                struct ofproto_port_queue *port_queue;

                port_queue = ofpbuf_put_uninit(&queues_buf,
                                               sizeof *port_queue);
                port_queue->queue = queue_id;
                port_queue->dscp = queue->dscp[0];
            }

            netdev_set_queue(iface->netdev, queue_id, &queue->other_config);
        }
        if (!queue_zero) {
            struct smap details;

            smap_init(&details);
            netdev_set_queue(iface->netdev, 0, &details);
            smap_destroy(&details);
        }
    }

    if (iface->ofp_port != OFPP_NONE) {
        const struct ofproto_port_queue *port_queues = ofpbuf_data(&queues_buf);
        size_t n_queues = ofpbuf_size(&queues_buf) / sizeof *port_queues;

        ofproto_port_set_queues(iface->port->bridge->ofproto, iface->ofp_port,
                                port_queues, n_queues);
    }

    netdev_set_policing(iface->netdev,
                        iface->cfg->ingress_policing_rate,
                        iface->cfg->ingress_policing_burst);

    ofpbuf_uninit(&queues_buf);
}

static void
iface_configure_cfm(struct iface *iface)
{
    const struct ovsrec_interface *cfg = iface->cfg;
    const char *opstate_str;
    const char *cfm_ccm_vlan;
    struct cfm_settings s;
    struct smap netdev_args;

    if (!cfg->n_cfm_mpid) {
        ofproto_port_clear_cfm(iface->port->bridge->ofproto, iface->ofp_port);
        return;
    }

    s.check_tnl_key = false;
    smap_init(&netdev_args);
    if (!netdev_get_config(iface->netdev, &netdev_args)) {
        const char *key = smap_get(&netdev_args, "key");
        const char *in_key = smap_get(&netdev_args, "in_key");

        s.check_tnl_key = (key && !strcmp(key, "flow"))
                           || (in_key && !strcmp(in_key, "flow"));
    }
    smap_destroy(&netdev_args);

    s.mpid = *cfg->cfm_mpid;
    s.interval = smap_get_int(&iface->cfg->other_config, "cfm_interval", 0);
    cfm_ccm_vlan = smap_get(&iface->cfg->other_config, "cfm_ccm_vlan");
    s.ccm_pcp = smap_get_int(&iface->cfg->other_config, "cfm_ccm_pcp", 0);

    if (s.interval <= 0) {
        s.interval = 1000;
    }

    if (!cfm_ccm_vlan) {
        s.ccm_vlan = 0;
    } else if (!strcasecmp("random", cfm_ccm_vlan)) {
        s.ccm_vlan = CFM_RANDOM_VLAN;
    } else {
        s.ccm_vlan = atoi(cfm_ccm_vlan);
        if (s.ccm_vlan == CFM_RANDOM_VLAN) {
            s.ccm_vlan = 0;
        }
    }

    s.extended = smap_get_bool(&iface->cfg->other_config, "cfm_extended",
                               false);
    s.demand = smap_get_bool(&iface->cfg->other_config, "cfm_demand", false);

    opstate_str = smap_get(&iface->cfg->other_config, "cfm_opstate");
    s.opup = !opstate_str || !strcasecmp("up", opstate_str);

    ofproto_port_set_cfm(iface->port->bridge->ofproto, iface->ofp_port, &s);
}
#endif

/* Returns true if 'iface' is synthetic, that is, if we constructed it locally
 * instead of obtaining it from the database. */
static bool
iface_is_synthetic(const struct iface *iface)
{
    return ovsdb_idl_row_is_synthetic(&iface->cfg->header_);
}

static ofp_port_t
iface_validate_ofport__(size_t n, int64_t *ofport)
{
    return (n && *ofport >= 1 && *ofport < ofp_to_u16(OFPP_MAX)
            ? u16_to_ofp(*ofport)
            : OFPP_NONE);
}

#ifndef OPS_TEMP
static ofp_port_t
iface_get_requested_ofp_port(const struct ovsrec_interface *cfg)
{
    return iface_validate_ofport__(cfg->n_ofport_request, cfg->ofport_request);
}
#endif

static ofp_port_t
#ifndef OPS_TEMP
iface_pick_ofport(const struct ovsrec_interface *cfg)
#else
iface_pick_ofport(const struct ovsrec_interface *cfg OVS_UNUSED)
#endif
{
#ifndef OPS_TEMP
    ofp_port_t requested_ofport = iface_get_requested_ofp_port(cfg);
    return (requested_ofport != OFPP_NONE
            ? requested_ofport
            : iface_validate_ofport__(cfg->n_ofport, cfg->ofport));
#else
    return iface_validate_ofport__(0, NULL);
#endif
}
#ifndef OPS_TEMP

/* Port mirroring. */

static struct mirror *
mirror_find_by_uuid(struct bridge *br, const struct uuid *uuid)
{
    struct mirror *m;

    HMAP_FOR_EACH_IN_BUCKET (m, hmap_node, uuid_hash(uuid), &br->mirrors) {
        if (uuid_equals(uuid, &m->uuid)) {
            return m;
        }
    }
    return NULL;
}

static void
bridge_configure_mirrors(struct bridge *br)
{
    const struct ovsdb_datum *mc;
#ifndef OPS_TEMP
    unsigned long *flood_vlans;
#endif
    struct mirror *m, *next;
    size_t i;

    /* Get rid of deleted mirrors. */
    mc = ovsrec_bridge_get_mirrors(br->cfg, OVSDB_TYPE_UUID);
    HMAP_FOR_EACH_SAFE (m, next, hmap_node, &br->mirrors) {
        union ovsdb_atom atom;

        atom.uuid = m->uuid;
        if (ovsdb_datum_find_key(mc, &atom, OVSDB_TYPE_UUID) == UINT_MAX) {
            mirror_destroy(m);
        }
    }

    /* Add new mirrors and reconfigure existing ones. */
    for (i = 0; i < br->cfg->n_mirrors; i++) {
        const struct ovsrec_mirror *cfg = br->cfg->mirrors[i];
        struct mirror *m = mirror_find_by_uuid(br, &cfg->header_.uuid);
        if (!m) {
            m = mirror_create(br, cfg);
        }
        m->cfg = cfg;
        if (!mirror_configure(m)) {
            mirror_destroy(m);
        }
    }

#ifndef OPS_TEMP
    /* Update flooded vlans (for RSPAN). */
    flood_vlans = vlan_bitmap_from_array(br->cfg->flood_vlans,
                                         br->cfg->n_flood_vlans);
    ofproto_set_flood_vlans(br->ofproto, flood_vlans);
    bitmap_free(flood_vlans);
#endif
}

static struct mirror *
mirror_create(struct bridge *br, const struct ovsrec_mirror *cfg)
{
    struct mirror *m;

    m = xzalloc(sizeof *m);
    m->uuid = cfg->header_.uuid;
    hmap_insert(&br->mirrors, &m->hmap_node, uuid_hash(&m->uuid));
    m->bridge = br;
    m->name = xstrdup(cfg->name);

    return m;
}

static void
mirror_destroy(struct mirror *m)
{
    if (m) {
        struct bridge *br = m->bridge;

        if (br->ofproto) {
            ofproto_mirror_unregister(br->ofproto, m);
        }

        hmap_remove(&br->mirrors, &m->hmap_node);
        free(m->name);
        free(m);
    }
}

static void
mirror_collect_ports(struct mirror *m,
                     struct ovsrec_port **in_ports, int n_in_ports,
                     void ***out_portsp, size_t *n_out_portsp)
{
    void **out_ports = xmalloc(n_in_ports * sizeof *out_ports);
    size_t n_out_ports = 0;
    size_t i;

    for (i = 0; i < n_in_ports; i++) {
        const char *name = in_ports[i]->name;
        struct port *port = port_lookup(m->bridge, name);
        if (port) {
            out_ports[n_out_ports++] = port;
        } else {
            VLOG_WARN("bridge %s: mirror %s cannot match on nonexistent "
                      "port %s", m->bridge->name, m->name, name);
        }
    }
    *out_portsp = out_ports;
    *n_out_portsp = n_out_ports;
}

static bool
mirror_configure(struct mirror *m)
{
    const struct ovsrec_mirror *cfg = m->cfg;
    struct ofproto_mirror_settings s;

    /* Set name. */
    if (strcmp(cfg->name, m->name)) {
        free(m->name);
        m->name = xstrdup(cfg->name);
    }
    s.name = m->name;

    /* Get output port or VLAN. */
    if (cfg->output_port) {
        s.out_bundle = port_lookup(m->bridge, cfg->output_port->name);
        if (!s.out_bundle) {
            VLOG_ERR("bridge %s: mirror %s outputs to port not on bridge",
                     m->bridge->name, m->name);
            return false;
        }
        s.out_vlan = UINT16_MAX;

        if (cfg->output_vlan) {
            VLOG_ERR("bridge %s: mirror %s specifies both output port and "
                     "output vlan; ignoring output vlan",
                     m->bridge->name, m->name);
        }
    } else if (cfg->output_vlan) {
        /* The database should prevent invalid VLAN values. */
        s.out_bundle = NULL;
        s.out_vlan = *cfg->output_vlan;
    } else {
        VLOG_ERR("bridge %s: mirror %s does not specify output; ignoring",
                 m->bridge->name, m->name);
        return false;
    }

    /* Get port selection. */
    if (cfg->select_all) {
        size_t n_ports = hmap_count(&m->bridge->ports);
        void **ports = xmalloc(n_ports * sizeof *ports);
        struct port *port;
        size_t i;

        i = 0;
        HMAP_FOR_EACH (port, hmap_node, &m->bridge->ports) {
            ports[i++] = port;
        }

        s.srcs = ports;
        s.n_srcs = n_ports;

        s.dsts = ports;
        s.n_dsts = n_ports;
    } else {
        /* Get ports, dropping ports that don't exist.
         * The IDL ensures that there are no duplicates. */
        mirror_collect_ports(m, cfg->select_src_port, cfg->n_select_src_port,
                             &s.srcs, &s.n_srcs);
        mirror_collect_ports(m, cfg->select_dst_port, cfg->n_select_dst_port,
                             &s.dsts, &s.n_dsts);
    }

    /* Get VLAN selection. */
    s.src_vlans = vlan_bitmap_from_array(cfg->select_vlan, cfg->n_select_vlan);

    /* Configure. */
    ofproto_mirror_register(m->bridge->ofproto, m, &s);

    /* Clean up. */
    if (s.srcs != s.dsts) {
        free(s.dsts);
    }
    free(s.srcs);
    free(s.src_vlans);

    return true;
}

/* Linux VLAN device support (e.g. "eth0.10" for VLAN 10.)
 *
 * This is deprecated.  It is only for compatibility with broken device drivers
 * in old versions of Linux that do not properly support VLANs when VLAN
 * devices are not used.  When broken device drivers are no longer in
 * widespread use, we will delete these interfaces. */

static struct ovsrec_port **recs;
static size_t n_recs, allocated_recs;

/* Adds 'rec' to a list of recs that have to be destroyed when the VLAN
 * splinters are reconfigured. */
static void
register_rec(struct ovsrec_port *rec)
{
    if (n_recs >= allocated_recs) {
        recs = x2nrealloc(recs, &allocated_recs, sizeof *recs);
    }
    recs[n_recs++] = rec;
}

/* Frees all of the ports registered with register_reg(). */
static void
free_registered_recs(void)
{
    size_t i;

    for (i = 0; i < n_recs; i++) {
        struct ovsrec_port *port = recs[i];
        size_t j;

        for (j = 0; j < port->n_interfaces; j++) {
            struct ovsrec_interface *iface = port->interfaces[j];
            free(iface->name);
            free(iface);
        }

        smap_destroy(&port->other_config);
        free(port->interfaces);
        free(port->name);
        free(port->tag);
        free(port);
    }
    n_recs = 0;
}

/* Returns true if VLAN splinters are enabled on 'iface_cfg', false
 * otherwise. */
static bool
vlan_splinters_is_enabled(const struct ovsrec_interface *iface_cfg)
{
    return smap_get_bool(&iface_cfg->other_config, "enable-vlan-splinters",
                         false);
}

/* Figures out the set of VLANs that are in use for the purpose of VLAN
 * splinters.
 *
 * If VLAN splinters are enabled on at least one interface and any VLANs are in
 * use, returns a 4096-bit bitmap with a 1-bit for each in-use VLAN (bits 0 and
 * 4095 will not be set).  The caller is responsible for freeing the bitmap,
 * with free().
 *
 * If VLANs splinters are not enabled on any interface or if no VLANs are in
 * use, returns NULL.
 *
 * Updates 'vlan_splinters_enabled_anywhere'. */
static unsigned long int *
collect_splinter_vlans(const struct ovsrec_open_vswitch *ovs_cfg)
{
    unsigned long int *splinter_vlans;
    struct sset splinter_ifaces;
    const char *real_dev_name;
    struct shash *real_devs;
    struct shash_node *node;
    struct bridge *br;
    size_t i;

    /* Free space allocated for synthesized ports and interfaces, since we're
     * in the process of reconstructing all of them. */
    free_registered_recs();

    splinter_vlans = bitmap_allocate(4096);
    sset_init(&splinter_ifaces);
    vlan_splinters_enabled_anywhere = false;
    for (i = 0; i < ovs_cfg->n_bridges; i++) {
        struct ovsrec_bridge *br_cfg = ovs_cfg->bridges[i];
        size_t j;

        for (j = 0; j < br_cfg->n_ports; j++) {
            struct ovsrec_port *port_cfg = br_cfg->ports[j];
            int k;

            for (k = 0; k < port_cfg->n_interfaces; k++) {
                struct ovsrec_interface *iface_cfg = port_cfg->interfaces[k];

                if (vlan_splinters_is_enabled(iface_cfg)) {
                    vlan_splinters_enabled_anywhere = true;
                    sset_add(&splinter_ifaces, iface_cfg->name);
                    vlan_bitmap_from_array__(port_cfg->trunks,
                                             port_cfg->n_trunks,
                                             splinter_vlans);
                }
            }

            if (port_cfg->tag && *port_cfg->tag > 0 && *port_cfg->tag < 4095) {
                bitmap_set1(splinter_vlans, *port_cfg->tag);
            }
        }
    }

    if (!vlan_splinters_enabled_anywhere) {
        free(splinter_vlans);
        sset_destroy(&splinter_ifaces);
        return NULL;
    }

    HMAP_FOR_EACH (br, node, &all_bridges) {
        if (br->ofproto) {
            ofproto_get_vlan_usage(br->ofproto, splinter_vlans);
        }
    }

    /* Don't allow VLANs 0 or 4095 to be splintered.  VLAN 0 should appear on
     * the real device.  VLAN 4095 is reserved and Linux doesn't allow a VLAN
     * device to be created for it. */
    bitmap_set0(splinter_vlans, 0);
    bitmap_set0(splinter_vlans, 4095);

    /* Delete all VLAN devices that we don't need. */
    vlandev_refresh();
    real_devs = vlandev_get_real_devs();
    SHASH_FOR_EACH (node, real_devs) {
        const struct vlan_real_dev *real_dev = node->data;
        const struct vlan_dev *vlan_dev;
        bool real_dev_has_splinters;

        real_dev_has_splinters = sset_contains(&splinter_ifaces,
                                               real_dev->name);
        HMAP_FOR_EACH (vlan_dev, hmap_node, &real_dev->vlan_devs) {
            if (!real_dev_has_splinters
                || !bitmap_is_set(splinter_vlans, vlan_dev->vid)) {
                struct netdev *netdev;

                if (!netdev_open(vlan_dev->name, "system", &netdev)) {
                    if (!netdev_get_in4(netdev, NULL, NULL) ||
                        !netdev_get_in6(netdev, NULL)) {
                        /* It has an IP address configured, so we don't own
                         * it.  Don't delete it. */
                    } else {
                        vlandev_del(vlan_dev->name);
                    }
                    netdev_close(netdev);
                }
            }

        }
    }

    /* Add all VLAN devices that we need. */
    SSET_FOR_EACH (real_dev_name, &splinter_ifaces) {
        int vid;

        BITMAP_FOR_EACH_1 (vid, 4096, splinter_vlans) {
            if (!vlandev_get_name(real_dev_name, vid)) {
                vlandev_add(real_dev_name, vid);
            }
        }
    }

    vlandev_refresh();

    sset_destroy(&splinter_ifaces);

    if (bitmap_scan(splinter_vlans, 1, 0, 4096) >= 4096) {
        free(splinter_vlans);
        return NULL;
    }
    return splinter_vlans;
}

/* Pushes the configure of VLAN splinter port 'port' (e.g. eth0.9) down to
 * ofproto.  */
static void
configure_splinter_port(struct port *port)
{
    struct ofproto *ofproto = port->bridge->ofproto;
    ofp_port_t realdev_ofp_port;
    const char *realdev_name;
    struct iface *vlandev, *realdev;

    ofproto_bundle_unregister(port->bridge->ofproto, port);

    vlandev = CONTAINER_OF(list_front(&port->ifaces), struct iface,
                           port_elem);

    realdev_name = smap_get(&port->cfg->other_config, "realdev");
    realdev = iface_lookup(port->bridge, realdev_name);
    realdev_ofp_port = realdev ? realdev->ofp_port : 0;

    ofproto_port_set_realdev(ofproto, vlandev->ofp_port, realdev_ofp_port,
                             *port->cfg->tag);
}

static struct ovsrec_port *
synthesize_splinter_port(const char *real_dev_name,
                         const char *vlan_dev_name, int vid)
{
    struct ovsrec_interface *iface;
    struct ovsrec_port *port;

    iface = xmalloc(sizeof *iface);
    ovsrec_interface_init(iface);
    iface->name = xstrdup(vlan_dev_name);
    iface->type = "system";

    port = xmalloc(sizeof *port);
    ovsrec_port_init(port);
    port->interfaces = xmemdup(&iface, sizeof iface);
    port->n_interfaces = 1;
    port->name = xstrdup(vlan_dev_name);
    port->vlan_mode = "splinter";
    port->tag = xmalloc(sizeof *port->tag);
    *port->tag = vid;

    smap_add(&port->other_config, "realdev", real_dev_name);

    register_rec(port);
    return port;
}

/* For each interface with 'br' that has VLAN splinters enabled, adds a
 * corresponding ovsrec_port to 'ports' for each splinter VLAN marked with a
 * 1-bit in the 'splinter_vlans' bitmap. */
static void
add_vlan_splinter_ports(struct bridge *br,
                        const unsigned long int *splinter_vlans,
                        struct shash *ports)
{
    size_t i;

    /* We iterate through 'br->cfg->ports' instead of 'ports' here because
     * we're modifying 'ports'. */
    for (i = 0; i < br->cfg->n_ports; i++) {
        const char *name = br->cfg->ports[i]->name;
        struct ovsrec_port *port_cfg = shash_find_data(ports, name);
        size_t j;

        for (j = 0; j < port_cfg->n_interfaces; j++) {
            struct ovsrec_interface *iface_cfg = port_cfg->interfaces[j];

            if (vlan_splinters_is_enabled(iface_cfg)) {
                const char *real_dev_name;
                uint16_t vid;

                real_dev_name = iface_cfg->name;
                BITMAP_FOR_EACH_1 (vid, 4096, splinter_vlans) {
                    const char *vlan_dev_name;

                    vlan_dev_name = vlandev_get_name(real_dev_name, vid);
                    if (vlan_dev_name
                        && !shash_find(ports, vlan_dev_name)) {
                        shash_add(ports, vlan_dev_name,
                                  synthesize_splinter_port(
                                      real_dev_name, vlan_dev_name, vid));
                    }
                }
            }
        }
    }
}

static void
mirror_refresh_stats(struct mirror *m)
{
    struct ofproto *ofproto = m->bridge->ofproto;
    uint64_t tx_packets, tx_bytes;
    char *keys[2];
    int64_t values[2];
    size_t stat_cnt = 0;

    if (ofproto_mirror_get_stats(ofproto, m, &tx_packets, &tx_bytes)) {
        ovsrec_mirror_set_statistics(m->cfg, NULL, NULL, 0);
        return;
    }

    if (tx_packets != UINT64_MAX) {
        keys[stat_cnt] = "tx_packets";
        values[stat_cnt] = tx_packets;
        stat_cnt++;
    }
    if (tx_bytes != UINT64_MAX) {
        keys[stat_cnt] = "tx_bytes";
        values[stat_cnt] = tx_bytes;
        stat_cnt++;
    }

    ovsrec_mirror_set_statistics(m->cfg, keys, values, stat_cnt);
}
#endif

#ifdef OPS
/* Neighbor Functions */
/* Function to cleanup neighbor from hash, in case of any failures */
static void
neighbor_hash_delete(struct vrf *vrf, struct neighbor *neighbor)
{
    VLOG_DBG("In neighbor_hash_delete for neighbor %s", neighbor->ip_address);
    if (neighbor) {
        hmap_remove(&vrf->all_neighbors, &neighbor->node);
        free(neighbor->ip_address);
        free(neighbor->port_name);
        free(neighbor->mac);
        free(neighbor);
    }
}

/* Add neighbor host entry into ofprotoc/asic */
static int
neighbor_set_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor)
{
    const struct ovsrec_neighbor *idl_neighbor = neighbor->cfg;
    struct port *port;

    VLOG_DBG("neighbor_set_l3_host_entry called for ip %s and mac %s",
              idl_neighbor->ip_address, idl_neighbor->mac);

    /* Get port info */
    port = port_lookup(vrf->up, neighbor->port_name);
    if (port == NULL) {
        VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
        return 1;
    }

    /* Call Provider */
    if (!ofproto_add_l3_host_entry(vrf->up->ofproto, port,
                                   neighbor->is_ipv6_addr,
                                   idl_neighbor->ip_address,
                                   idl_neighbor->mac,
                                   &neighbor->l3_egress_id)) {
        VLOG_DBG("VRF %s: Added host entry for %s",
                  vrf->up->name, neighbor->ip_address);

        return 0;
    }
    else {
        VLOG_ERR("ofproto_add_l3_host_entry failed");

        /* if l3_intf not configured yet or any failure,
        ** delete from hash */
        neighbor_hash_delete(vrf, neighbor);

        return 1;
    }
} /* neighbor_set_l3_host_entry */

/* Delete port ipv4/ipv6 host entry */
static int
neighbor_delete_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor)
{
    struct port *port;

    VLOG_DBG("neighbor_delete_l3_host_entry called for ip %s",
              neighbor->ip_address);

    /* Get port info */
    port = port_lookup(vrf->up, neighbor->port_name);
    if (port == NULL) {
        VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
        return 1;
    }

    /* Call Provider */
    /* Note: Cannot access idl neighbor_cfg as it is already deleted */
    if (!ofproto_delete_l3_host_entry(vrf->up->ofproto, port,
                                      neighbor->is_ipv6_addr,
                                      neighbor->ip_address,
                                      &neighbor->l3_egress_id)) {
        VLOG_DBG("VRF %s: Deleted host entry for ip %s",
                  vrf->up->name, neighbor->ip_address);

        return 0;
    }
    else {
        VLOG_ERR("ofproto_delete_l3_host_entry failed");
        return 1;
    }
} /* neighbor_delete_l3_host_entry */

/* Function to find neighbor in vrf local hash */
struct neighbor*
neighbor_hash_lookup(const struct vrf *vrf, const char *ip_address)
{
    struct neighbor *neighbor;

    HMAP_FOR_EACH_WITH_HASH (neighbor, node, hash_string(ip_address, 0),
                             &vrf->all_neighbors) {
        if (!strcmp(neighbor->ip_address, ip_address)) {
            return neighbor;
        }
    }
    return NULL;
}

/* Function to create new neighbor hash entry and configure asic */
static void
neighbor_create(struct vrf *vrf,
                const struct ovsrec_neighbor *idl_neighbor)
{
    struct neighbor *neighbor;

    VLOG_DBG("In neighbor_create for neighbor %s",
              idl_neighbor->ip_address);
    ovs_assert(!neighbor_hash_lookup(vrf, idl_neighbor->ip_address));

    neighbor = xzalloc(sizeof *neighbor);
    neighbor->ip_address = xstrdup(idl_neighbor->ip_address);
    neighbor->mac = xstrdup(idl_neighbor->mac);

    if (strcmp(idl_neighbor->address_family,
                             OVSREC_NEIGHBOR_ADDRESS_FAMILY_IPV6) == 0) {
        neighbor->is_ipv6_addr = true;
    }
    neighbor->port_name = xstrdup(idl_neighbor->port->name);
    neighbor->cfg = idl_neighbor;
    neighbor->vrf = vrf;
    neighbor->l3_egress_id = -1;

    hmap_insert(&vrf->all_neighbors, &neighbor->node,
                hash_string(neighbor->ip_address, 0));

    /* Add ofproto/asic neighbors */
    neighbor_set_l3_host_entry(vrf, neighbor);
    vrf_ofproto_update_route_with_neighbor(vrf, neighbor, true);
}

/* Function to delete neighbor in hash and also from ofproto/asic */
static void
neighbor_delete(struct vrf *vrf, struct neighbor *neighbor)
{
    VLOG_DBG("In neighbor_delete for neighbor %s", neighbor->ip_address);
    if (neighbor) {

        /* Update routes before deleting the l3 host entry */
        vrf_ofproto_update_route_with_neighbor(vrf, neighbor, false);
        /* Delete from ofproto/asic */
        neighbor_delete_l3_host_entry(vrf, neighbor);

        /* Delete from hash */
        neighbor_hash_delete(vrf, neighbor);
    }
}

/* Function to handle modifications to neighbor entry and configure asic */
static void
neighbor_modify(struct neighbor *neighbor,
                const struct ovsrec_neighbor *idl_neighbor)
{
    VLOG_DBG("In neighbor_modify for neighbor %s",
              idl_neighbor->ip_address);

    /* TODO: Get status, if failed or incomplete delete the entry */
    /* OPENSWITCH_TODO : instead of delete/add, reprogram the entry in ofproto */

    if ( (strcmp(neighbor->port_name, idl_neighbor->port->name) != 0) ||
        (strcmp(neighbor->mac, idl_neighbor->mac) != 0 ) ) {
        struct ether_addr *ether_mac = NULL;

        /* Delete earlier egress/host entry */
        neighbor_delete_l3_host_entry(neighbor->vrf, neighbor);

        /* Update and add new one */
        free(neighbor->port_name);
        free(neighbor->mac);
        neighbor->mac = xstrdup(idl_neighbor->mac);
        neighbor->port_name = xstrdup(idl_neighbor->port->name);

        /* Configure provider/asic only if valid mac */
        ether_mac = ether_aton(idl_neighbor->mac);
        if (ether_mac != NULL) {
            neighbor_set_l3_host_entry(neighbor->vrf, neighbor);
        }
        /* entry stays in hash, and on modification add to asic */
    }
} /* neighbor_modify */

/* Function to delete all neighbors of an vrf, when that vrf is deleted */
static void
vrf_delete_all_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor, *next;

    /* Delete all neighbors of this vrf */
    HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
        if (neighbor) {
            neighbor_delete(vrf, neighbor);
        }
    }

} /* vrf_delete_all_neighbors */

/* Function to to delete the neighbors which are referencing the deleted vrf port */
static void
vrf_delete_port_neighbors(struct vrf *vrf, struct port *port)
{
    struct neighbor *neighbor, *next;

    /* Delete the neighbors which are referencing the deleted vrf port */
    HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
        if ( (neighbor) &&
             (strcmp(neighbor->port_name, port->name) == 0) ) {
            neighbor_delete(vrf, neighbor);
        }
    }

} /* vrf_delete_port_neighbors */

/*
** Function to add neighbors of given vrf and program in ofproto/asic
*/
static void
vrf_add_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor;
    const struct ovsrec_neighbor *idl_neighbor;

    idl_neighbor = ovsrec_neighbor_first(idl);
    if (idl_neighbor == NULL)
    {
        VLOG_DBG("No rows in Neighbor table");
        return;
    }

    /* Add neighbors of this vrf */
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
       if (strcmp(vrf->cfg->name, idl_neighbor->vrf->name) == 0 ) {
           neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
           if (!neighbor && idl_neighbor->port) {
               neighbor_create(vrf, idl_neighbor);
           }
       }
    }

} /* vrf_add_neighbors */

/*
** Function to handle independent addition/deletion/modifications to
** neighbor table.  */
static void
vrf_reconfigure_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor, *next;
    struct shash current_idl_neigbors;
    const struct ovsrec_neighbor *idl_neighbor;

    idl_neighbor = ovsrec_neighbor_first(idl);
    if (idl_neighbor == NULL)
    {
        VLOG_DBG("No rows in Neighbor table, delete if any in our hash");

        /* May be all neighbors got delete, cleanup if any in this vrf hash */
        HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
            if (neighbor) {
                neighbor_delete(vrf, neighbor);
            }
        }

        return;
    }

    if ( (!OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(idl_neighbor, idl_seqno)) &&
       (!OVSREC_IDL_ANY_TABLE_ROWS_DELETED(idl_neighbor, idl_seqno))  &&
       (!OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(idl_neighbor, idl_seqno)) )
    {
        VLOG_DBG("No modification in Neighbor table");
        return;
    }

    /* Collect all neighbors of this vrf */
    shash_init(&current_idl_neigbors);
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

        /* add only neighbors of this vrf */
        if (strcmp(vrf->cfg->name, idl_neighbor->vrf->name) == 0 ) {
            if (!shash_add_once(&current_idl_neigbors, idl_neighbor->ip_address,
                                idl_neighbor)) {
                VLOG_DBG("neighbor %s specified twice",
                          idl_neighbor->ip_address);
                VLOG_WARN_RL(&rl, "neighbor %s specified twice",
                             idl_neighbor->ip_address);
            }
        }
    }

    /* Delete the neighbors' that are deleted from the db */
    VLOG_DBG("Deleting which are no more in idl");
    HMAP_FOR_EACH_SAFE(neighbor, next, node, &vrf->all_neighbors) {
        neighbor->cfg = shash_find_data(&current_idl_neigbors,
                                        neighbor->ip_address);
        if (!neighbor->cfg) {
            neighbor_delete(vrf, neighbor);
        }
    }

    /* Add new neighbors. */
    VLOG_DBG("Adding newly added idl neighbors");
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
        neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
        if (!neighbor && idl_neighbor->port) {
            neighbor_create(vrf, idl_neighbor);
        }
    }

    /* Look for any modification of mac/port of this vrf neighbors */
    VLOG_DBG("Looking for any modified neighbors, mac, etc");
    idl_neighbor = ovsrec_neighbor_first(idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(idl_neighbor, idl_seqno)) {
        OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
           if ( (OVSREC_IDL_IS_ROW_MODIFIED(idl_neighbor, idl_seqno)) &&
               !(OVSREC_IDL_IS_ROW_INSERTED(idl_neighbor, idl_seqno)) ) {

                VLOG_DBG("Some modifications in Neigbor %s",
                                      idl_neighbor->ip_address);

                neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
                if (neighbor) {
                    if(idl_neighbor->port)
                        neighbor_modify(neighbor, idl_neighbor);
                    else
                        neighbor_delete(vrf, neighbor);
                }
            }
        }
    }

    shash_destroy(&current_idl_neigbors);

} /* add_reconfigure_neighbors */

/* Read/Reset neighbors data-path hit-bit and update into db */
static void
run_neighbor_update(void)
{
    const struct ovsrec_neighbor *idl_neighbor =
                                  ovsrec_neighbor_first(idl);
    struct neighbor *neighbor;
    int neighbor_interval;
    const struct vrf *vrf;
    struct port *port;
    struct ovsdb_idl_txn *txn;

    /* Skip if nothing to update */
    if (idl_neighbor ==  NULL) {
        return;
    }

    /* TODO: Add the timer-internval in some table/column */
    /* And decide on the interval */
    /* const struct ovsrec_open_vswitch *idl_ovs =
    **                            ovsrec_open_vswitch_first(idl);
    ** neighbor_interval = MAX(smap_get_int(&idl_ovs->other_config,
                                         "neighbor-update-interval",
                                         NEIGHBOR_HIT_BIT_UPDATE_INTERVAL),
                                         NEIGHBOR_HIT_BIT_UPDATE_INTERVAL); */
    neighbor_interval = NEIGHBOR_HIT_BIT_UPDATE_INTERVAL;
    if (neighbor_timer_interval != neighbor_interval) {
        neighbor_timer_interval = neighbor_interval;
        neighbor_timer = LLONG_MIN;
    }

    if (time_msec() >= neighbor_timer) {
        //enum ovsdb_idl_txn_status status;

        txn = ovsdb_idl_txn_create(idl);

        /* Rate limit the update.  Do not start a new update if the
        ** previous one is not done. */
        OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
            VLOG_DBG(" Checking hit-bit for %s", idl_neighbor->ip_address);

            vrf = vrf_lookup(idl_neighbor->vrf->name);
            neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
            if (neighbor == NULL) {
                VLOG_ERR("Neighbor not found in local hash");
                continue;
            }

            /* Get port/ofproto info */
            port = port_lookup(neighbor->vrf->up, neighbor->port_name);
            if (port == NULL) {
                VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
                continue;
            }

            /* Call Provider */
            if (!ofproto_get_l3_host_hit(neighbor->vrf->up->ofproto, port,
                                        neighbor->is_ipv6_addr,
                                        idl_neighbor->ip_address,
                                        &neighbor->hit_bit)) {
                VLOG_DBG("Got host %s hit bit=0x%x",
                          idl_neighbor->ip_address, neighbor->hit_bit);

                struct smap smap;

                /* Write the hit bit status to status column */
                smap_clone(&smap, &idl_neighbor->status);
                if (neighbor->hit_bit) {
                    smap_replace(&smap, OVSDB_NEIGHBOR_STATUS_DP_HIT, "true");
                } else {
                    smap_replace(&smap, OVSDB_NEIGHBOR_STATUS_DP_HIT, "false");
                }
                ovsrec_neighbor_set_status(idl_neighbor, &smap);
                smap_destroy(&smap);
            }
            else {
                VLOG_ERR("!ofproto_get_l3_host_hit failed");
                continue;
            }
        } /* For each */

        /* No need to retry since we will update with latest state every 10sec */
        ovsdb_idl_txn_commit(txn);
        ovsdb_idl_txn_destroy(txn);

        neighbor_timer = time_msec() + neighbor_timer_interval;
    }
} /* run_neighbor_update */

/* OPENSWITCH_TODO - remove after integration ... */
int
vrf_l3_route_action(struct vrf *vrf, enum ofproto_route_action action,
                    struct ofproto_route *route)
{
    return ofproto_l3_route_action(vrf->up->ofproto, action, route);
}

bool
vrf_has_l3_route_action(struct vrf *vrf)
{
    return vrf->up->ofproto->ofproto_class->l3_route_action ? true : false;
}

int
vrf_l3_ecmp_set(struct vrf *vrf, bool enable)
{
    return ofproto_l3_ecmp_set(vrf->up->ofproto, enable);
}

int
vrf_l3_ecmp_hash_set(struct vrf *vrf, unsigned int hash, bool enable)
{
    return ofproto_l3_ecmp_hash_set(vrf->up->ofproto, hash, enable);
}
#endif
