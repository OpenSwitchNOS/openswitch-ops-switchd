/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015 Nicira, Inc.
 * Copyright (c) 2015-2016 Hewlett Packard Enterprise Development LP
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
#include "subsystem.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include "async-append.h"
#include "coverage.h"
#include "dirs.h"
#include "dynamic-string.h"
#include "hash.h"
#include "hmap.h"
#include "hmapx.h"
#include "list.h"
#include "netdev.h"
#include "poll-loop.h"
#include "seq.h"
#include "shash.h"
#include "smap.h"
#include "sset.h"
#include "stats-blocks.h"
#include "timeval.h"
#include "util.h"
#include "vswitch-idl.h"
#include "openvswitch/vlog.h"

#include "openswitch-idl.h"
#include "openswitch-dflt.h"

VLOG_DEFINE_THIS_MODULE(subsystem);

COVERAGE_DEFINE(subsystem_reconfigure);

/* Each time this timer expires, the interface statistics
 * are pushed to the database. */
static int stats_timer_interval;
static long long int stats_timer = LLONG_MIN;

struct iface {
    /* These members are always valid.
     * They are immutable: they never change between iface_create() and
     * iface_destroy(). */
    struct hmap_node name_node;  /* In struct subsystem's "iface_by_name" hmap. */
    struct subsystem *subsystem; /* Containing subsystem. */
    char *name;                  /* Host network device name. */
    struct netdev *netdev;       /* Network device. */
    uint64_t change_seq;

    const struct ovsrec_interface *cfg;
};

struct subsystem {
    struct hmap_node node;      /* In 'all_subsystems'. */
    char *name;                 /* User-specified arbitrary name. */
    const struct ovsrec_subsystem *cfg;

    /* Subsystem ports. */
    struct hmap iface_by_name;  /* "struct iface"s indexed by name. */

    /* Used during reconfiguration. */
    struct shash wanted_ifaces;
};

/* All subsystems, indexed by name. */
static struct hmap all_subsystems = HMAP_INITIALIZER(&all_subsystems);

/* OVSDB IDL used to obtain configuration. */
extern struct ovsdb_idl *idl;

/* Most recently processed IDL sequence number. */
static unsigned int idl_seqno;

static void add_del_subsystems(const struct ovsrec_open_vswitch *);
static void subsystem_create(const struct ovsrec_subsystem *);
static void subsystem_destroy(struct subsystem *);
static struct subsystem *subsystem_lookup(const char *name);

static void subsystem_del_ifaces(struct subsystem *,
                             const struct shash *wanted_ifaces);
static void subsystem_reconfigure_ifaces(struct subsystem *,
                             const struct shash *wanted_ifaces);
static void subsystem_add_ifaces(struct subsystem *,
                             const struct shash *wanted_ifaces);
static void subsystem_collect_wanted_ifaces(struct subsystem *,
                            struct shash *wanted_ifaces);

static bool iface_create(struct subsystem *, const struct ovsrec_interface *);
static void iface_destroy(struct iface *);
static struct iface *iface_lookup(const struct subsystem *, const char *name);

static int iface_set_netdev_hw_intf_config(const struct ovsrec_interface *,
                                           struct netdev *);
static void iface_refresh_netdev_status(struct iface *iface);
static void iface_refresh_stats(struct iface *iface);

static void
run_status_update(void)
{
    struct subsystem *ss;
    struct iface *iface;

    HMAP_FOR_EACH (ss, node, &all_subsystems) {
        HMAP_FOR_EACH (iface, name_node, &ss->iface_by_name) {
            iface_refresh_netdev_status(iface);
        }
    }
}

static void
run_stats_update(void)
{
    int stats_interval;
    struct subsystem *ss;
    struct iface *iface;
    const struct ovsrec_open_vswitch *cfg = ovsrec_open_vswitch_first(idl);
    struct stats_blk_params sblk = {0};

    /* Statistics update interval should always be greater than or equal to
     * 5000 ms. */
    stats_interval = MAX(smap_get_int(&cfg->other_config,
                                      "stats-update-interval",
                                      DFLT_SYSTEM_OTHER_CONFIG_STATS_UPDATE_INTERVAL),
                                      DFLT_SYSTEM_OTHER_CONFIG_STATS_UPDATE_INTERVAL);
    if (stats_timer_interval != stats_interval) {
        stats_timer_interval = stats_interval;
        stats_timer = LLONG_MIN;
    }

    if (time_msec() >= stats_timer) {

        sblk.idl = idl;
        sblk.idl_seqno = idl_seqno;
        execute_stats_block(&sblk, STATS_SUBSYSTEM_BEGIN);
        HMAP_FOR_EACH (ss, node, &all_subsystems) {
            execute_stats_block(&sblk, STATS_PER_SUBSYSTEM);
            HMAP_FOR_EACH (iface, name_node, &ss->iface_by_name) {
                iface_refresh_stats(iface);

                /* Statistics-callback for system interfaces.
                   Note: non-system interfaces are handled in bridge.c. */
                if (iface->netdev != NULL) {
                    sblk.netdev = iface->netdev;
                    sblk.cfg = iface->cfg;
                    execute_stats_block(&sblk, STATS_PER_SUBSYSTEM_NETDEV);
                }
            }
            sblk.netdev = NULL;
        }

        execute_stats_block(&sblk, STATS_SUBSYSTEM_END);
        stats_timer = time_msec() + stats_timer_interval;
        poll_timer_wait_until(stats_timer);
    }
}

/* Public functions. */
void
subsystem_init(void)
{
    idl_seqno = ovsdb_idl_get_seqno(idl);
}

void
subsystem_exit(void)
{
    struct subsystem *ss, *next_ss;

    HMAP_FOR_EACH_SAFE (ss, next_ss, node, &all_subsystems) {
        subsystem_destroy(ss);
    }
}

static void
subsystem_reconfigure(const struct ovsrec_open_vswitch *ovs_cfg)
{
    struct subsystem *ss;

    COVERAGE_INC(subsystem_reconfigure);

    /* Destroy "struct subsystem"s and "struct iface"s according
     * to 'ovs_cfg', with only very minimal configuration otherwise.
     */
    add_del_subsystems(ovs_cfg);
    HMAP_FOR_EACH (ss, node, &all_subsystems) {
        subsystem_collect_wanted_ifaces(ss, &ss->wanted_ifaces);
        subsystem_del_ifaces(ss, &ss->wanted_ifaces);
    }

    HMAP_FOR_EACH (ss, node, &all_subsystems) {
        subsystem_reconfigure_ifaces(ss, &ss->wanted_ifaces);
    }

    HMAP_FOR_EACH (ss, node, &all_subsystems) {
        subsystem_add_ifaces(ss, &ss->wanted_ifaces);
        shash_destroy(&ss->wanted_ifaces);
    }
}

static void
subsystem_reconfigure_ifaces(struct subsystem *ss, const struct shash *wanted_ifaces)
{
    struct shash_node *iface_node;

    SHASH_FOR_EACH (iface_node, wanted_ifaces) {
        const struct ovsrec_interface *iface_cfg = iface_node->data;
        struct iface *iface = iface_lookup(ss, iface_cfg->name);

        if (iface && OVSREC_IDL_IS_ROW_MODIFIED(iface_cfg, idl_seqno)) {

            iface_set_netdev_hw_intf_config(iface_cfg, iface->netdev);
        }
    }
}

static void
subsystem_add_ifaces(struct subsystem *ss, const struct shash *wanted_ifaces)
{
    struct shash_node *iface_node;

    /* Split children interfaces expect their parent interface
     * to be created ahead of them. So create all the split parent
     * interfaces first.
     */
    SHASH_FOR_EACH (iface_node, wanted_ifaces) {
        const struct ovsrec_interface *iface_cfg = iface_node->data;

        if (iface_cfg->n_split_children != 0) {

            struct iface *iface = iface_lookup(ss, iface_cfg->name);
            if (!iface) {
                VLOG_DBG("Adding splittable interface. Name=%s", iface_cfg->name);
                iface_create(ss, iface_cfg);
            }
        }
    }

    SHASH_FOR_EACH (iface_node, wanted_ifaces) {
        const struct ovsrec_interface *iface_cfg = iface_node->data;

        if (iface_cfg->n_split_children == 0) {

            struct iface *iface = iface_lookup(ss, iface_cfg->name);
            if (!iface) {
                VLOG_DBG("Adding non-splittable interface. Name=%s", iface_cfg->name);
                iface_create(ss, iface_cfg);
            }
        }
    }
}

static void
add_del_subsystems(const struct ovsrec_open_vswitch *cfg)
{
    struct subsystem *ss, *next;
    struct shash new_ss;
    size_t i;

    /* Collect new subsystems' names and types. */
    shash_init(&new_ss);
    for (i = 0; i < cfg->n_subsystems; i++) {
        const struct ovsrec_subsystem *ss_cfg = cfg->subsystems[i];

        if (!shash_add_once(&new_ss, ss_cfg->name, ss_cfg)) {
            VLOG_WARN("subsystem %s specified twice", ss_cfg->name);
        }
    }

    /* Get rid of deleted subsystems. */
    HMAP_FOR_EACH_SAFE (ss, next, node, &all_subsystems) {
        ss->cfg = shash_find_data(&new_ss, ss->name);
        if (!ss->cfg) {
            subsystem_destroy(ss);
        }
    }

    /* Add new subsystems. */
    for (i = 0; i < cfg->n_subsystems; i++) {
        const struct ovsrec_subsystem *ss_cfg = cfg->subsystems[i];
        struct subsystem *ss = subsystem_lookup(ss_cfg->name);
        if (!ss) {
            subsystem_create(ss_cfg);
        }
    }

    shash_destroy(&new_ss);
}

void
subsystem_run(void)
{
    static struct ovsrec_open_vswitch null_cfg;
    const struct ovsrec_open_vswitch *cfg;
    struct ovsdb_idl_txn *txn;

    if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    cfg = ovsrec_open_vswitch_first(idl);

    txn = ovsdb_idl_txn_create(idl);

    if (ovsdb_idl_get_seqno(idl) != idl_seqno) {
        subsystem_reconfigure(cfg ? cfg : &null_cfg);
        idl_seqno = ovsdb_idl_get_seqno(idl);
    }

    run_status_update();
    run_stats_update();

    ovsdb_idl_txn_commit(txn);
    ovsdb_idl_txn_destroy(txn);
}

void
subsystem_wait(void)
{
}

/* Subsystem reconfiguration functions. */
static void
subsystem_create(const struct ovsrec_subsystem *ss_cfg)
{
    struct subsystem *ss;

    ovs_assert(!subsystem_lookup(ss_cfg->name));
    ss = xzalloc(sizeof *ss);

    ss->name = xstrdup(ss_cfg->name);
    ovs_assert(ss->name);
    ss->cfg = ss_cfg;

    hmap_init(&ss->iface_by_name);

    hmap_insert(&all_subsystems, &ss->node, hash_string(ss->name, 0));
}

static void
subsystem_destroy(struct subsystem *ss)
{
    if (ss) {
        struct iface *iface, *next;

        HMAP_FOR_EACH_SAFE (iface, next, name_node, &ss->iface_by_name) {
            iface_destroy(iface);
        }

        hmap_remove(&all_subsystems, &ss->node);
        hmap_destroy(&ss->iface_by_name);
        free(ss->name);
        free(ss);
    }
}

static struct subsystem *
subsystem_lookup(const char *name)
{
    struct subsystem *ss;

    HMAP_FOR_EACH_WITH_HASH (ss, node, hash_string(name, 0), &all_subsystems) {
        if (!strcmp(ss->name, name)) {
            return ss;
        }
    }
    return NULL;
}

static void
subsystem_collect_wanted_ifaces(struct subsystem *ss, struct shash *wanted_ifaces)
{
    size_t i;

    shash_init(wanted_ifaces);

    for (i = 0; i < ss->cfg->n_interfaces; i++) {
        const char *name = ss->cfg->interfaces[i]->name;
        if (!shash_add_once(wanted_ifaces, name, ss->cfg->interfaces[i])) {
            VLOG_WARN("subsystem %s: %s specified twice as subsystem interfaces",
                      ss->name, name);
        }
    }
}

/* Deletes "struct iface"s under 'ss' which aren't
 * consistent with 'ss->cfg'. */
static void
subsystem_del_ifaces(struct subsystem *ss, const struct shash *wanted_ifaces)
{
    struct iface *iface, *next;

    /* Get rid of deleted interfaces */
    HMAP_FOR_EACH_SAFE (iface, next, name_node, &ss->iface_by_name) {
        iface->cfg = shash_find_data(wanted_ifaces, iface->name);
        if (!iface->cfg) {
            iface_destroy(iface);
        }
    }
}

/* Opens a network device for 'if_cfg' and configures it. */
static int
iface_do_create(const struct subsystem *ss,
                const struct ovsrec_interface *iface_cfg,
                struct netdev **netdevp)
{
    struct smap hw_intf_info;
    struct netdev *netdev = NULL;
    int error;

    if (netdev_is_reserved_name(iface_cfg->name)) {
        VLOG_WARN("could not create interface %s, name is reserved",
                  iface_cfg->name);
        error = EINVAL;
        goto error;
    }

    error = netdev_open(iface_cfg->name, "system", &netdev);
    if (error) {
        VLOG_WARN("could not open network device %s (%s)",
                  iface_cfg->name, ovs_strerror(error));
        goto error;
    }

    VLOG_DBG("subsystem %s: added interface %s", ss->name, iface_cfg->name);

    /* Copy the iface->hw_intf_info to a local smap. */
    smap_clone(&hw_intf_info, &(iface_cfg->hw_intf_info));

    /* Check if the interface is a split child of another port. */
    if (iface_cfg->split_parent != NULL) {
        smap_add(&hw_intf_info,
                 INTERFACE_HW_INTF_INFO_SPLIT_PARENT,
                 iface_cfg->split_parent->name);
    }

    error = netdev_set_hw_intf_info(netdev, &hw_intf_info);
    if (error) {
        smap_destroy(&hw_intf_info);
        goto error;
    }

    smap_destroy(&hw_intf_info);

    error = iface_set_netdev_hw_intf_config(iface_cfg, netdev);
    if (error) {
        goto error;
    }

    *netdevp = netdev;
    return 0;

error:
    *netdevp = NULL;
    netdev_close(netdev);
    return error;
}

/* Creates a new iface on 'ss' based on 'iface_cfg'.
 * Return true if an iface is successfully created, false otherwise. */
static bool
iface_create(struct subsystem *ss, const struct ovsrec_interface *iface_cfg)
{
    struct netdev *netdev;
    struct iface *iface;
    int error;
    struct stats_blk_params sblk = {0};

    /* Do the bits that can fail up front. */
    ovs_assert(!iface_lookup(ss, iface_cfg->name));
    error = iface_do_create(ss, iface_cfg, &netdev);
    if (error) {
        return false;
    }

    /* Create the iface structure. */
    iface = xzalloc(sizeof *iface);
    hmap_insert(&ss->iface_by_name, &iface->name_node,
                hash_string(iface_cfg->name, 0));
    iface->name = xstrdup(iface_cfg->name);
    iface->netdev = netdev;
    iface->cfg = iface_cfg;

    iface_refresh_netdev_status(iface);
    iface_refresh_stats(iface);

    if (iface->netdev != NULL) {
        sblk.netdev = iface->netdev;
        sblk.cfg = iface_cfg;
        execute_stats_block(&sblk, STATS_SUBSYSTEM_CREATE_NETDEV);
    }

    return true;
}

static void
iface_destroy(struct iface *iface)
{
    if (iface) {
        struct subsystem *ss = iface->subsystem;

        hmap_remove(&ss->iface_by_name, &iface->name_node);

        /* The user is changing configuration here, so netdev_remove needs to be
         * used as opposed to netdev_close */
        netdev_remove(iface->netdev);

        free(iface->name);
        free(iface);
    }
}

/* Configures 'netdev' based on the "hw_intf_config"
 * columns in 'iface_cfg'.
 * Returns 0 if successful, otherwise a positive errno value. */
static int
iface_set_netdev_hw_intf_config(const struct ovsrec_interface *iface_cfg, struct netdev *netdev)
{
    return netdev_set_hw_intf_config(netdev, &(iface_cfg->hw_intf_config));
}

static struct iface *
iface_lookup(const struct subsystem *ss, const char *name)
{
    struct iface *iface;

    HMAP_FOR_EACH_WITH_HASH (iface, name_node, hash_string(name, 0),
                             &ss->iface_by_name) {
        if (!strcmp(iface->name, name)) {
            return iface;
        }
    }

    return NULL;
}

static void
iface_refresh_netdev_status(struct iface *iface)
{
    struct smap smap;

    enum netdev_features current;
    enum netdev_features pause_staus;
    enum netdev_flags flags;
    const char *link_state;
    struct eth_addr mac;
    int64_t bps = 0, mtu_64, link_resets = 0;
    int mtu, error;

    if (iface->change_seq == netdev_get_change_seq(iface->netdev)) {
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

    /* admin_state */
    error = netdev_get_flags(iface->netdev, &flags);
    if (!error) {
        const char *state = (flags & NETDEV_UP) ?
                                OVSREC_INTERFACE_ADMIN_STATE_UP :
                                OVSREC_INTERFACE_ADMIN_STATE_DOWN;

        ovsrec_interface_set_admin_state(iface->cfg, state);
    } else {
        ovsrec_interface_set_admin_state(iface->cfg, NULL);
    }

    /* link_state */
    link_state = netdev_get_carrier(iface->netdev) ?
                    OVSREC_INTERFACE_LINK_STATE_UP :
                    OVSREC_INTERFACE_LINK_STATE_DOWN;
    ovsrec_interface_set_link_state(iface->cfg, link_state);

    link_resets = netdev_get_carrier_resets(iface->netdev);
    ovsrec_interface_set_link_resets(iface->cfg, &link_resets, 1);

    /* duplex, speed, pause */
    error = netdev_get_features(iface->netdev, &current, NULL, NULL, NULL);
    if (!error) {

        pause_staus = (current & (NETDEV_F_PAUSE | NETDEV_F_PAUSE_ASYM));
        if (!pause_staus) {
            ovsrec_interface_set_pause(iface->cfg, OVSREC_INTERFACE_PAUSE_NONE);
        } else if (pause_staus == NETDEV_F_PAUSE) {
            ovsrec_interface_set_pause(iface->cfg, OVSREC_INTERFACE_PAUSE_RXTX);
        } else if (pause_staus == NETDEV_F_PAUSE_ASYM) {
            ovsrec_interface_set_pause(iface->cfg, OVSREC_INTERFACE_PAUSE_TX);
        } else {
            ovsrec_interface_set_pause(iface->cfg, OVSREC_INTERFACE_PAUSE_RX);
        }

        bps = netdev_features_to_bps(current, 0);
        const char *duplex = netdev_features_is_full_duplex(current) ?
                                OVSREC_INTERFACE_DUPLEX_FULL :
                                OVSREC_INTERFACE_DUPLEX_HALF;
        ovsrec_interface_set_duplex(iface->cfg, duplex);
        ovsrec_interface_set_link_speed(iface->cfg, &bps, 1);

    } else {
            ovsrec_interface_set_duplex(iface->cfg, NULL);
            ovsrec_interface_set_link_speed(iface->cfg, &bps, 1);
            ovsrec_interface_set_pause(iface->cfg, NULL);
    }

    /* mtu */
    error = netdev_get_mtu(iface->netdev, &mtu);
    if (!error) {
        mtu_64 = mtu;
        ovsrec_interface_set_mtu(iface->cfg, &mtu_64, 1);
    } else {
        ovsrec_interface_set_mtu(iface->cfg, NULL, 0);
    }

    /* MAC addr in use */
    error = netdev_get_etheraddr(iface->netdev, &mac);
    if (!error) {
        char mac_string[32];

        sprintf(mac_string, ETH_ADDR_FMT, ETH_ADDR_ARGS(mac));
        ovsrec_interface_set_mac_in_use(iface->cfg, mac_string);
    } else {
        ovsrec_interface_set_mac_in_use(iface->cfg, NULL);
    }
}

static void
iface_refresh_stats(struct iface *iface)
{
    /* This function is copied from bridge.c */
#define IFACE_STATS                             \
    IFACE_STAT(rx_packets,      "rx_packets")   \
    IFACE_STAT(tx_packets,      "tx_packets")   \
    IFACE_STAT(rx_bytes,        "rx_bytes")     \
    IFACE_STAT(tx_bytes,        "tx_bytes")     \
    IFACE_STAT(rx_dropped,      "rx_dropped")   \
    IFACE_STAT(tx_dropped,      "tx_dropped")   \
    IFACE_STAT(rx_errors,       "rx_errors")    \
    IFACE_STAT(tx_errors,       "tx_errors")    \
    IFACE_STAT(rx_crc_errors,   "rx_crc_err")   \
    IFACE_STAT(collisions,      "collisions")   \
    IFACE_STAT(ipv4_uc_tx_packets,  "ipv4_uc_tx_packets")  \
    IFACE_STAT(ipv4_uc_rx_packets,  "ipv4_uc_rx_packets")  \
    IFACE_STAT(ipv4_uc_tx_bytes,    "ipv4_uc_tx_bytes")    \
    IFACE_STAT(ipv4_uc_rx_bytes,    "ipv4_uc_rx_bytes")    \
    IFACE_STAT(ipv4_mc_tx_packets,  "ipv4_mc_tx_packets")  \
    IFACE_STAT(ipv4_mc_rx_packets,  "ipv4_mc_rx_packets")  \
    IFACE_STAT(ipv4_mc_tx_bytes,    "ipv4_mc_tx_bytes")    \
    IFACE_STAT(ipv4_mc_rx_bytes,    "ipv4_mc_rx_bytes")    \
    IFACE_STAT(ipv6_uc_tx_packets,  "ipv6_uc_tx_packets")  \
    IFACE_STAT(ipv6_uc_rx_packets,  "ipv6_uc_rx_packets")  \
    IFACE_STAT(ipv6_uc_tx_bytes,    "ipv6_uc_tx_bytes")    \
    IFACE_STAT(ipv6_uc_rx_bytes,    "ipv6_uc_rx_bytes")    \
    IFACE_STAT(ipv6_mc_tx_packets,  "ipv6_mc_tx_packets")  \
    IFACE_STAT(ipv6_mc_rx_packets,  "ipv6_mc_rx_packets")  \
    IFACE_STAT(ipv6_mc_tx_bytes,    "ipv6_mc_tx_bytes")    \
    IFACE_STAT(ipv6_mc_rx_bytes,    "ipv6_mc_rx_bytes")    \
    IFACE_STAT(l3_uc_rx_packets,    "l3_uc_rx_packets")    \
    IFACE_STAT(l3_uc_rx_bytes,      "l3_uc_rx_bytes")      \
    IFACE_STAT(l3_uc_tx_packets,    "l3_uc_tx_packets")    \
    IFACE_STAT(l3_uc_tx_bytes,      "l3_uc_tx_bytes")      \
    IFACE_STAT(l3_mc_rx_packets,    "l3_mc_rx_packets")    \
    IFACE_STAT(l3_mc_rx_bytes,      "l3_mc_rx_bytes")      \
    IFACE_STAT(l3_mc_tx_packets,    "l3_mc_tx_packets")    \
    IFACE_STAT(l3_mc_tx_bytes,      "l3_mc_tx_bytes")      \
    IFACE_STAT(sflow_ingress_packets,   "sflow_ingress_packets") \
    IFACE_STAT(sflow_ingress_bytes,     "sflow_ingress_bytes")   \
    IFACE_STAT(sflow_egress_packets,    "sflow_egress_packets")  \
    IFACE_STAT(sflow_egress_bytes,      "sflow_egress_bytes")

#define IFACE_STAT(MEMBER, NAME) + 1
    enum { N_IFACE_STATS = IFACE_STATS };
#undef IFACE_STAT
    int64_t values[N_IFACE_STATS];
    char *keys[N_IFACE_STATS];
    int n;

    struct netdev_stats stats;

    /* Intentionally ignore return value, since errors will set 'stats' to
     * all-1s, and we will deal with that correctly below. */
    memset(&stats, 0, sizeof(struct netdev_stats));
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
