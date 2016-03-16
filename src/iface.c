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

#include <errno.h>
#include "openvswitch/types.h"
#include "openvswitch/vlog.h"
#include "netdev-provider.h"
#include "vswitch-idl.h"
#include "bridge.h"
#include "iface.h"
#include "port.h"
#include "timeval.h"
#include "hmap.h"
#include "vrf.h"
#include "poll-loop.h"
#include "switchd.h"

VLOG_DEFINE_THIS_MODULE(iface);

/* Each time this timer expires, the bridge fetches interface and mirror
 * statistics and pushes them into the database. */
long long int stats_timer = LLONG_MIN;
static int stats_timer_interval;

static int
iface_set_netdev_config(const struct ovsrec_interface *,
                        struct netdev *, char **errp);
static void iface_clear_db_record(const struct ovsrec_interface *,
                                  char *errp OVS_UNUSED);
static bool iface_is_internal(const struct ovsrec_interface *iface,
                              const struct ovsrec_bridge *br);
static void iface_clear_db_record(const struct ovsrec_interface *if_cfg, char *errp);
static bool iface_is_synthetic(const struct iface *);

/* Opens a network device for 'if_cfg' and configures it.  Adds the network
 * device to br->ofproto and stores the OpenFlow port number in '*ofp_portp'.
 *
 * If successful, returns 0 and stores the network device in '*netdevp'.  On
 * failure, returns a positive errno value and stores NULL in '*netdevp'. */
static int
iface_do_create(const struct bridge *br,
                const struct ovsrec_interface *iface_cfg,
                ofp_port_t *ofp_portp,
                struct netdev **netdevp,
                char **errp)
{
    struct netdev *netdev = NULL;
    struct smap sub_intf_info;
    int ret = 0;
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
    *ofp_portp = iface_pick_ofport(iface_cfg);
    error = ofproto_port_add(br->ofproto, netdev, ofp_portp);
    if (error) {
        goto error;
    }

    VLOG_DBG("bridge %s: added interface %s on port %d",
              br->name, iface_cfg->name, *ofp_portp);

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
bool
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
    error = iface_do_create(br, iface_cfg, &ofp_port, &netdev, &errp);
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
    iface_refresh_netdev_status(iface, false);

    return true;
}

/* Returns the correct network device type for interface 'iface' in bridge
 * 'br'. */
const char *
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

void
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

/* Configures 'netdev' based on the "options" column in 'iface_cfg'.
 * Returns 0 if successful, otherwise a positive errno value. */
static int
iface_set_netdev_config(const struct ovsrec_interface *iface_cfg,
                        struct netdev *netdev, char **errp)
{
    return netdev_set_config(netdev, &iface_cfg->options, errp);
}

static bool
iface_is_internal(const struct ovsrec_interface *iface,
                  const struct ovsrec_bridge *br)
{
    /* The local port and "internal" ports are always "internal". */
    return !strcmp(iface->type, "internal") ||
           (br && !strcmp(iface->name, br->name));
}

void
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

void
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

struct iface *
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

struct iface *
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

/* Clears all of the fields in 'if_cfg' that indicate interface status, and
 * sets the "ofport" field to -1.
 *
 * This is appropriate when 'if_cfg''s interface cannot be created or is
 * otherwise invalid. */
static void
iface_clear_db_record(const struct ovsrec_interface *if_cfg, char *errp OVS_UNUSED)
{
    if (!ovsdb_idl_row_is_synthetic(&if_cfg->header_)) {
        ovsrec_interface_set_status(if_cfg, NULL);
        ovsrec_interface_set_admin_state(if_cfg, NULL);
        ovsrec_interface_set_duplex(if_cfg, NULL);
        ovsrec_interface_set_link_speed(if_cfg, NULL, 0);
        ovsrec_interface_set_link_state(if_cfg, NULL);
        ovsrec_interface_set_mac_in_use(if_cfg, NULL);
        ovsrec_interface_set_mtu(if_cfg, NULL, 0);
        ovsrec_interface_set_statistics(if_cfg, NULL, NULL, 0);
    }
}


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


ofp_port_t
iface_pick_ofport(const struct ovsrec_interface *cfg OVS_UNUSED)
{
    return iface_validate_ofport__(0, NULL);
}

void
iface_refresh_stats(struct iface *iface)
{

    /* Interface stats are updated from subsystem.c. */
    if (!iface->type || !strcmp(iface->type, "system")) {
            return;
    }

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

void
iface_refresh_netdev_status(struct iface *iface,
                            bool status_txn_try_again)
{
    struct smap smap;

    enum netdev_features current;
    enum netdev_flags flags;
    const char *link_state;
    struct eth_addr mac;
    int64_t bps, mtu_64,
    link_resets;
    int mtu, error;
    if (iface_is_synthetic(iface)) {
        return;
    }

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

}

void
iface_refresh_ofproto_status(struct iface *iface OVS_UNUSED)
{
    return;
}

/* Update interface and mirror statistics if necessary. */
void
iface_stats_run()
{
    static struct ovsdb_idl_txn *stats_txn;
    const struct ovsrec_system *cfg = ovsrec_system_first(idl);
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

            struct vrf *vrf;
            stats_txn = ovsdb_idl_txn_create(idl);
            HMAP_FOR_EACH (br, node, &all_bridges) {
                struct port *port;
                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_stats(iface);
                    }
                }
            }

            HMAP_FOR_EACH (vrf, node, &all_vrfs) {
                struct port *port;
                HMAP_FOR_EACH (port, hmap_node, &vrf->up->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_stats(iface);
                    }
                }
            }
        }

        status = ovsdb_idl_txn_commit(stats_txn);
        if (status != TXN_INCOMPLETE) {
            stats_timer = time_msec() + stats_timer_interval;
            ovsdb_idl_txn_destroy(stats_txn);
            stats_txn = NULL;
        }
    }
}

void
iface_stats_wait()
{
    poll_timer_wait_until(stats_timer);
}
