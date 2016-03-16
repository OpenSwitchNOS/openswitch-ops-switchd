/* Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
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

#include "openvswitch/types.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"
#include "openswitch-dflt.h"
#include "ofproto/ofproto.h"
#include "ofproto/bond.h"
#include "vlan-bitmap.h"
#include "util.h"
#include "port.h"
#include "switchd.h"
#include "vrf.h"

VLOG_DEFINE_THIS_MODULE(port);

static void vrf_port_reconfig_ipaddr(struct port *,
                                     struct ofproto_bundle_settings *);
static ofp_port_t *add_ofp_port(ofp_port_t port, ofp_port_t *ports,
                                size_t *n, size_t *allocated);

void
port_init()
{
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_statistics);
    ovsdb_idl_omit_alert(idl, &ovsrec_port_col_bond_active_slave);
    ovsdb_idl_omit(idl, &ovsrec_port_col_external_ids);
}

struct port *
port_create(struct bridge *br, const struct ovsrec_port *cfg)
{
    struct port *port;

    port = xzalloc(sizeof *port);
    port->bridge = br;
    port->name = xstrdup(cfg->name);
    port->cfg = cfg;
    port->bond_hw_handle = -1;
    list_init(&port->ifaces);

    hmap_insert(&br->ports, &port->hmap_node, hash_string(port->name, 0));
    return port;
}

/* Deletes interfaces from 'port' that are no longer configured for it. */
void
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

void
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

struct port *
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

bool
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

void
port_configure_bond(struct port *port, struct bond_settings *s)
{
    const char *detect_s;
    struct iface *iface;
    const char *mac_s;
    int miimon_interval;

    s->name = port->name;
    s->balance = BM_L3_SRC_DST_HASH;
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

    VLOG_DBG("port %s: bond_mode is set to %s",
                      port->name, bond_mode_to_string(s->balance));


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

void
port_configure(struct port *port)
{
    const struct ovsrec_port *cfg = port->cfg;
    struct bond_settings bond_settings;
    struct ofproto_bundle_settings s;
    struct iface *iface;
    int prev_bond_handle = port->bond_hw_handle;
    int cfg_slave_count;
    bool lacp_enabled = false;
    bool lacp_active = false;   /* Not used. */
    /* Get name. */
    s.name = port->name;

    /* Get slaves. */
    s.n_slaves = 0;
    s.slaves = xmalloc(list_size(&port->ifaces) * sizeof *s.slaves);
    cfg_slave_count = list_size(&port->ifaces);
    s.slaves_entered = cfg_slave_count;
    s.n_slaves_tx_enable = 0;
    s.slaves_tx_enable = xmalloc(cfg_slave_count * sizeof *s.slaves);

    s.enable = smap_get_bool(&cfg->hw_config,
            PORT_HW_CONFIG_MAP_ENABLE,
            PORT_HW_CONFIG_MAP_ENABLE_DEFAULT);

    /* Determine if bond mode is dynamic (LACP). */
    lacp_enabled  = enable_lacp(port, &lacp_active);
    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
        /* This should be moved outside the for statement as the evaluated variables
           dont depend on the for. */
        if ((strncmp(port->name, "lag", 3) == 0) || (cfg_slave_count > 1) || lacp_enabled) {
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
    }
    VLOG_DBG("port %s has %d configured interfaces, %d eligible "
             "interfaces, lacp_enabled=%d",
             s.name, cfg_slave_count, (int)s.n_slaves, lacp_enabled);
    s.bond_handle_alloc_only = false;
    if (s.hw_bond_should_exist && (s.n_slaves < 1)) {
        if (port->bond_hw_handle == -1) {
            s.bond_handle_alloc_only = true;
        }
    }
    /* Get VLAN tag. */
    s.vlan = -1;
    if (cfg->tag && *cfg->tag >= 1 && *cfg->tag <= 4094) {
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
    /* If port is in TRUNK mode, VLAN tag needs to be ignored. */
    if (s.vlan_mode == PORT_VLAN_TRUNK) {
        s.vlan = -1;
    }
    s.use_priority_tags = smap_get_bool(&cfg->other_config, "priority-tags",
                                        false);

/* For OPS, LACP support is handled by lacpd. */

    /* Get bond settings. */
    if (s.hw_bond_should_exist) {
        s.bond = &bond_settings;
        port_configure_bond(port, &bond_settings);
    } else {
        s.bond = NULL;
        LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
            netdev_set_miimon_interval(iface->netdev, 0);
        }
    }

    /* Setup port configuration option array and save
       its address in bundle setting */
    s.port_options[PORT_OPT_VLAN] = &cfg->vlan_options;
    s.port_options[PORT_OPT_BOND] = &cfg->bond_options;
    s.port_options[PORT_HW_CONFIG] = &cfg->hw_config;

    /* Check for port L3 ip changes */
    vrf_port_reconfig_ipaddr(port, &s);

    /* Register. */
    ofproto_bundle_register(port->bridge->ofproto, port, &s);
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
    /* Clean up. */
    free(s.slaves);
    free(s.slaves_tx_enable);
    free(s.trunks);
}

void
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
        struct iface *iface;

        sset_add(&ofproto_ports, ofproto_port.name);

        iface = iface_lookup(br, ofproto_port.name);
        if (!iface) {
            /* No such iface is configured, so we should delete this
             * ofproto_port.
             *
             * As a corner case exception, keep the port if it's a bond fake
             * interface. */
            goto delete;
        }

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

void
vrf_delete_or_reconfigure_ports(struct vrf *vrf)
{
    struct ofproto_port ofproto_port;
    struct ofproto_port_dump dump;

    struct sset ofproto_ports;
    struct port *port, *port_next;
    struct iface *iface;
    struct smap sub_intf_info;
    int ret = 0;

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
        sset_add(&ofproto_ports, ofproto_port.name);

        iface = iface_lookup(vrf->up, ofproto_port.name);
        if (!iface) {
            /* No such iface is configured, so we should delete this
             * ofproto_port. */
            goto delete;
        }
        if (!strcmp(iface->cfg->type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
           smap_init(&sub_intf_info);
           vrf_delete_or_reconfigure_subintf(&sub_intf_info, iface->cfg);
           ret = netdev_set_config(iface->netdev, &sub_intf_info, NULL);
           smap_destroy(&sub_intf_info);
           if (ret)
              goto delete;
           continue;
        }

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

static ofp_port_t *
add_ofp_port(ofp_port_t port, ofp_port_t *ports, size_t *n, size_t *allocated)
{
    if (*n >= *allocated) {
        ports = x2nrealloc(ports, allocated, sizeof *ports);
    }
    ports[(*n)++] = port;
    return ports;
}

/*
** Function to handle add/delete/modify of port ipv4/v6 address.
*/
static void
vrf_port_reconfig_ipaddr(struct port *port,
                         struct ofproto_bundle_settings *bundle_setting)
{
    const struct ovsrec_port *idl_port = port->cfg;

    /* If primary ipv4 got changed */
    bundle_setting->ip_change = 0;
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip4_address,
                                      idl_seqno) ) {
        VLOG_DBG("ip4_address modified");
        bundle_setting->ip_change |= PORT_PRIMARY_IPv4_CHANGED;
        bundle_setting->ip4_address = idl_port->ip4_address;
    }

    /* If primary ipv6 got changed */
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip6_address,
                                      idl_seqno) ) {
        VLOG_DBG("ip6_address modified");
        bundle_setting->ip_change |= PORT_PRIMARY_IPv6_CHANGED;
        bundle_setting->ip6_address = idl_port->ip6_address;
    }
    /*
     * Configure secondary network addresses
     */
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip4_address_secondary,
                                      idl_seqno) ) {
        VLOG_DBG("ip4_address_secondary modified");
        bundle_setting->ip_change |= PORT_SECONDARY_IPv4_CHANGED;
        bundle_setting->n_ip4_address_secondary =
                                      idl_port->n_ip4_address_secondary;
        bundle_setting->ip4_address_secondary =
                                      idl_port->ip4_address_secondary;
    }

    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip6_address_secondary,
                                      idl_seqno) ) {
        VLOG_DBG("ip6_address_secondary modified");
        bundle_setting->ip_change |= PORT_SECONDARY_IPv6_CHANGED;
        bundle_setting->n_ip6_address_secondary =
                                      idl_port->n_ip6_address_secondary;
        bundle_setting->ip6_address_secondary =
                                      idl_port->ip6_address_secondary;
    }
}
