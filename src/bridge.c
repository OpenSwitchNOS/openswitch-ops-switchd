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

#include <string.h>
#include <netinet/ether.h>
#include "coverage.h"
#include "hash.h"
#include "openvswitch/vlog.h"
#include "vswitch-idl.h"
#include "ovsdb-idl.h"
#include "unixctl.h"
#include "iface.h"
#include "switchd.h"
#include "bridge.h"
#include "reconfigure-blocks.h"
#include "vrf.h"
#include "seq.h"
#include "system-stats.h"
#include "vlan.h"
#include "netdev.h"
#include "mac-learning.h"
#include "connectivity.h"
#include "switchd-ofproto.h"

VLOG_DEFINE_THIS_MODULE(bridge);

COVERAGE_DEFINE(bridge_reconfigure);

/* All bridges, indexed by name. */
struct hmap all_bridges = HMAP_INITIALIZER(&all_bridges);

static void add_del_bridges(const struct ovsrec_system *);
static void bridge_create(const struct ovsrec_bridge *);
static void bridge_destroy(struct bridge *);
static struct bridge *bridge_lookup(const char *name);
static unixctl_cb_func bridge_unixctl_dump_flows;
static unixctl_cb_func bridge_unixctl_reconnect;
static void bridge_collect_wanted_ports(struct bridge *,
                                        const unsigned long *splinter_vlans,
                                        struct shash *wanted_ports);
static void bridge_delete_ofprotos(void);
static void bridge_del_ports(struct bridge *,
                             const struct shash *wanted_ports);
static void bridge_add_ports(struct bridge *,
                             const struct shash *wanted_ports);
static void bridge_configure_datapath_id(struct bridge *);
static void bridge_configure_mac_table(struct bridge *);
static void bridge_configure_dp_desc(struct bridge *);
static void bridge_pick_local_hw_addr(struct bridge *,
                                      struct eth_addr *ea,
                                      struct iface **hw_addr_iface);
static uint64_t bridge_pick_datapath_id(struct bridge *,
                                        const struct eth_addr bridge_ea,
                                        struct iface *hw_addr_iface);


/* Public functions. */

/* Initializes the bridge module, configuring it to obtain its configuration
 * from an OVSDB server accessed over 'remote', which should be a string in a
 * form acceptable to ovsdb_idl_create(). */
void
bridge_init()
{
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_datapath_id);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_datapath_version);
    ovsdb_idl_omit_alert(idl, &ovsrec_bridge_col_status);
    ovsdb_idl_omit(idl, &ovsrec_bridge_col_external_ids);

    /*port_init();*/

    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_admin_state);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_duplex);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_speed);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_state);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_pause);
    ovsdb_idl_omit_alert(idl, &ovsrec_neighbor_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_link_resets);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_mac_in_use);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_mtu);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_statistics);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_lacp_current);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_error);
    ovsdb_idl_omit(idl, &ovsrec_interface_col_external_ids);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_hw_intf_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_pm_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_user_config);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_external_ids);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_inactivity_probe);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_is_connected);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_max_backoff);
    ovsdb_idl_omit(idl, &ovsrec_manager_col_status);

    ovsdb_idl_omit(idl, &ovsrec_ssl_col_external_ids);

    /* Nexthop table */
    ovsdb_idl_omit_alert(idl, &ovsrec_nexthop_col_status);

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

    /* Register unixctl commands. */
    unixctl_command_register("bridge/dump-flows", "bridge", 1, 1,
                             bridge_unixctl_dump_flows, NULL);
    unixctl_command_register("bridge/reconnect", "[bridge]", 0, 1,
                             bridge_unixctl_reconnect, NULL);
}

void
bridge_exit(void)
{
    struct bridge *br, *next_br;

    HMAP_FOR_EACH_SAFE (br, next_br, node, &all_bridges) {
        bridge_destroy(br);
    }
}

void
bridge_reconfigure(const struct ovsrec_system *ovs_cfg)
{
    struct bridge *br, *next;

    struct vrf *vrf, *vrf_next;
    struct blk_params bridge_blk_params;

    COVERAGE_INC(bridge_reconfigure);

    bridge_blk_params.idl = idl;
    bridge_blk_params.ofproto = NULL;

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

    add_del_vrfs(ovs_cfg);

    /* Execute the reconfigure for block BLK_INIT_RECONFIGURE */
    execute_reconfigure_block(&bridge_blk_params, BLK_INIT_RECONFIGURE);

    HMAP_FOR_EACH (br, node, &all_bridges) {
        bridge_collect_wanted_ports(br, NULL, &br->wanted_ports);

        /* Execute the reconfigure for block BLK_BR_DELETE_PORTS */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_DELETE_PORTS);
        bridge_del_ports(br, &br->wanted_ports);
    }

    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        vrf_collect_wanted_ports(vrf, &vrf->up->wanted_ports);

        /* Execute the reconfigure for block BLK_VRF_DELETE_PORTS */
        bridge_blk_params.ofproto = vrf->up->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_VRF_DELETE_PORTS);

        /* Inside vrf_del_ports, delete neighbors refering the
        ** deleted ports */

        vrf_del_ports(vrf, &vrf->up->wanted_ports);
    }
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

            /* Execute the reconfigure for block BLK_BR_RECONFIGURE_PORTS */
            bridge_blk_params.ofproto = br->ofproto;
            execute_reconfigure_block(&bridge_blk_params, BLK_BR_RECONFIGURE_PORTS);
        }
    }

    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        if (vrf->up->ofproto) {

            /* Note: Already deleted the neighbors in vrf_del_ports */
            vrf_delete_or_reconfigure_ports(vrf);

            /* Execute the reconfigure for block BLK_VRF_RECONFIGURE_PORTS */
            bridge_blk_params.ofproto = vrf->up->ofproto;
            execute_reconfigure_block(&bridge_blk_params, BLK_VRF_RECONFIGURE_PORTS);
        }
    }

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
    HMAP_FOR_EACH (br, node, &all_bridges) {
        bridge_add_ports(br, &br->wanted_ports);
        /* Execute the reconfigure for block BLK_BR_ADD_PORTS */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_ADD_PORTS);
        shash_destroy(&br->wanted_ports);
    }

    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        bridge_add_ports(vrf->up, &vrf->up->wanted_ports);

        /* Execute the reconfigure for block BLK_VRF_ADD_PORTS */
        bridge_blk_params.ofproto = vrf->up->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_VRF_ADD_PORTS);

        shash_destroy(&vrf->up->wanted_ports);
    }

    reconfigure_system_stats(ovs_cfg);

    /* Complete the configuration. */
    HMAP_FOR_EACH (br, node, &all_bridges) {
        struct port *port;

        VLOG_DBG("config bridge - %s", br->name);
        /* We need the datapath ID early to allow LACP ports to use it as the
         * default system ID. */
        bridge_configure_datapath_id(br);

        HMAP_FOR_EACH (port, hmap_node, &br->ports) {
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

            }
        }
        bridge_configure_vlans(br, idl_seqno);
        bridge_configure_mac_table(br);
        bridge_configure_dp_desc(br);

        /* Execute the reconfigure for block BLK_BR_FEATURE_RECONFIG */
        bridge_blk_params.ofproto = br->ofproto;
        execute_reconfigure_block(&bridge_blk_params, BLK_BR_FEATURE_RECONFIG);

    }

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


    /* The ofproto-dpif provider does some final reconfiguration in its
     * ->type_run() function.  We have to call it before notifying the database
     * client that reconfiguration is complete, otherwise there is a very
     * narrow race window in which e.g. ofproto/trace will not recognize the
     * new configuration (sometimes this causes unit test failures). */
    switchd_ofproto_run();
}

/* Delete ofprotos which aren't configured or have the wrong type.  Create
 * ofprotos which don't exist but need to. */
static void
bridge_delete_ofprotos(void)
{
    struct bridge *br;
    struct vrf *vrf;
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
            vrf = vrf_lookup(name);
            if ((!br || strcmp(type, br->type)) &&
                (!vrf || strcmp(type, "vrf"))) {
                ofproto_delete(name, type);
            }
        }
    }
    sset_destroy(&names);
    sset_destroy(&types);
}

static void
bridge_add_ports__(struct bridge *br, const struct shash *wanted_ports)
{
    struct shash_node *port_node;

    SHASH_FOR_EACH (port_node, wanted_ports) {
        const struct ovsrec_port *port_cfg = port_node->data;
        size_t i;

        VLOG_DBG("bridge_add_ports__ adding port %s", port_node->name);
        for (i = 0; i < port_cfg->n_interfaces; i++) {
            const struct ovsrec_interface *iface_cfg = port_cfg->interfaces[i];
                struct iface *iface = iface_lookup(br, iface_cfg->name);

                if (!iface) {
                    iface_create(br, iface_cfg, port_cfg);
                }
        }
    }
}

static void
bridge_add_ports(struct bridge *br, const struct shash *wanted_ports)
{
    /* Then add interfaces that want automatic port number assignment.
     * We add these afterward to avoid accidentally taking a specifically
     * requested port number. */
    bridge_add_ports__(br, wanted_ports);
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

static void
add_del_bridges(const struct ovsrec_system *cfg)
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

static void
find_local_hw_addr(const struct bridge *br, struct eth_addr *ea,
                   const struct port *fake_br, struct iface **hw_addr_iface)
{
    struct port *port;
    bool found_addr = false;
    int error;
    /* Otherwise choose the minimum non-local MAC address among all of the
     * interfaces. */
    HMAP_FOR_EACH (port, hmap_node, &br->ports) {
        struct eth_addr iface_ea;
        struct iface *candidate;
        struct iface *iface;

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

    return eth_addr_to_uint64(bridge_ea);
}

void
bridge_wait(void)
{
    struct sset types;
    const char *type;

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
    }
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

/* Bridge reconfiguration functions. */
static void
bridge_create(const struct ovsrec_bridge *br_cfg)
{
    struct bridge *br;
    const struct ovsrec_system* ovs = ovsrec_system_first(idl);
    ovs_assert(!bridge_lookup(br_cfg->name));
    br = xzalloc(sizeof *br);

    br->name = xstrdup(br_cfg->name);
    br->type = xstrdup(ofproto_normalize_type(br_cfg->datapath_type));
    br->cfg = br_cfg;

    /* Use system mac as default mac */
    memcpy(&br->default_ea, ether_aton(ovs->system_mac), ETH_ADDR_LEN);

    hmap_init(&br->ports);
    hmap_init(&br->ifaces);
    hmap_init(&br->iface_by_name);
    hmap_init(&br->vlans);
    hmap_insert(&all_bridges, &br->node, hash_string(br->name, 0));
}

static void
bridge_destroy(struct bridge *br)
{
    if (br) {
        struct port *port, *next_port;

        HMAP_FOR_EACH_SAFE (port, next_port, hmap_node, &br->ports) {
            port_destroy(port);
        }
        hmap_remove(&all_bridges, &br->node);
        ofproto_destroy(br->ofproto);
        hmap_destroy(&br->ifaces);
        hmap_destroy(&br->ports);
        hmap_destroy(&br->iface_by_name);
        hmap_destroy(&br->vlans);
        free(br->name);
        free(br->type);
        free(br);
    }
}

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
    }
    unixctl_command_reply(conn, NULL);
}


static void
bridge_collect_wanted_ports(struct bridge *br,
                            const unsigned long int *splinter_vlans OVS_UNUSED,
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
}

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
                VLOG_WARN_ONCE("%s: The null interface type is deprecated",
                               cfg->name);
            } else {
                /* We will add new interfaces later. */
            }
        }
    }
}

static void
bridge_configure_dp_desc(struct bridge *br)
{
    ofproto_set_dp_desc(br->ofproto,
                        smap_get(&br->cfg->other_config, "dp-desc"));
}
