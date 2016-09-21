/* Tunnel client callback registration source files.
 *
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * File: vtysh_ovsdb_tunnel_context.c
 *
 * Purpose: Source for registering sub-context callback with
 *          global config context.
 */

#include "vtysh/vty.h"
#include "vtysh/vector.h"
#include "vswitch-idl.h"
#include "ovsdb-idl.h"
#include "openswitch-idl.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/system_vtysh_utils.h"
#include "vtysh_ovsdb_tunnel_context.h"
#include "vtysh/utils/tunnel_vtysh_utils.h"

/*-----------------------------------------------------------------------------
| Function : vtysh_tunnel_context_clientcallback
| Responsibility : VNI commands
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/

vtysh_ret_val
vtysh_tunnel_context_clientcallback(void *p_private)
{
    const struct ovsrec_logical_switch *logical_switch = NULL;
    vtysh_ovsdb_cbmsg_ptr p_msg = (vtysh_ovsdb_cbmsg *)p_private;

    vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_DBG,
                              "vtysh_tunnel_context_clientcallback entered");

    OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch, p_msg->idl)
    {
        vtysh_ovsdb_cli_print(p_msg, "%s %ld", "vni",
                              logical_switch->tunnel_key);

        if (logical_switch->name)
        {
            vtysh_ovsdb_cli_print(p_msg, "%4s%s %s", "", "name",
                                  logical_switch->name);
        }

        if (logical_switch->description)
        {
            vtysh_ovsdb_cli_print(p_msg, "%4s%s %s", "", "description",
                                  logical_switch->description);
        }

        if (logical_switch->mcast_group_ip)
        {
            vtysh_ovsdb_cli_print(p_msg, "%4s%s %s", "", "mcast-group-ip",
                                  logical_switch->mcast_group_ip);
        }
    }
    vtysh_ovsdb_cli_print(p_msg,"!");

    return e_vtysh_ok;
}

void
print_common_tunnel_running_config(vtysh_ovsdb_cbmsg_ptr p_msg,
                                   const struct ovsrec_interface *if_row,
                                   struct vty *vty,
                                   struct ovsdb_idl *idl)
{
    const char *src_ip;
    const char *dest_ip;
    const struct ovsrec_port *port_row;

    // Source IP
    src_ip = smap_get(&if_row->options,
                    OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP);
    if (src_ip)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s %s", "", "source", "ip", src_ip);
    }

    // Destination IP
    dest_ip = smap_get(&if_row->options, OVSREC_INTERFACE_OPTIONS_REMOTE_IP);
    if (dest_ip)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s %s", "", "destination", "ip", dest_ip);
    }

    // IP address
    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, if_row->name) == 0)
        {
            if (port_row->ip4_address)
            {
                VTY_PRINT(p_msg, vty, "%4s%s %s", "", "ip address",
                          port_row->ip4_address);
            }
            else if (port_row->ip6_address)
            {
                VTY_PRINT(p_msg, vty, "%4s%s %s", "", "ip address",
                          port_row->ip6_address);
            }
        }
    }
}

void
print_vxlan_tunnel_running_config(vtysh_ovsdb_cbmsg_ptr p_msg,
                                  const struct ovsrec_interface *if_row,
                                  struct vty *vty,
                                  struct ovsdb_idl *idl)
{
    const char *src_intf;
    const char *vni_list;
    const char *udp_port;
    int tunnel_id;

    if (!if_row)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                  "Invalid interface row");
        return;
    }

    // Tunnel mode
    tunnel_id = get_id_from_name((const char*)if_row->name);
    if (tunnel_id == -1)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                  "Invalid interface ID");
        return;
    }

    VTY_PRINT(p_msg, vty, "%s %s %d %s %s", "interface", "tunnel", tunnel_id,
              "mode", "vxlan");

    // Common configurations
    print_common_tunnel_running_config(p_msg, if_row, vty, idl);

    // source-interface loopback
    src_intf = smap_get(&if_row->options,
                        OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);
    if (src_intf)
    {
        int loopback_id = get_id_from_name(src_intf);
        if (loopback_id == -1)
        {
            vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                      "Invalid interface ID");
            return;
        }

        VTY_PRINT(p_msg, vty, "%4s%s %s %d", "", "source-interface", "loopback",
                  loopback_id);
    }

    // VNI list
    vni_list = smap_get(&if_row->options,
                        OVSREC_INTERFACE_OPTIONS_VNI_LIST);
    if (vni_list)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s", "", "vni", vni_list);
    }

    // VxLAN UDP port
    udp_port = smap_get(&if_row->options,
                        OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT);
    if (udp_port)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s", "", "vxlan", udp_port);
    }
}

void
print_gre_tunnel_running_config(vtysh_ovsdb_cbmsg_ptr p_msg,
                                const struct ovsrec_interface *if_row,
                                struct vty *vty,
                                struct ovsdb_idl *idl)
{
    int tunnel_id;
    const char *src_intf;
    const char *ttl;
    const char *mtu;

    if (!if_row)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                  "Invalid interface row");
        return;
    }

    // Tunnel mode
    tunnel_id = get_id_from_name((const char*)if_row->name);
    if (tunnel_id == -1)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                  "Invalid interface ID");
        return;
    }

    VTY_PRINT(p_msg, vty, "%s %s %d %s %s %s", "interface",
              "tunnel", tunnel_id, "mode", "gre", "ipv4");

    // Common configurations
    print_common_tunnel_running_config(p_msg, if_row, vty, idl);

    // source interface
    src_intf = smap_get(&if_row->options,
                        OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);
    if (src_intf)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s %s", "", "source", "interface",
                  src_intf);
    }

    // Time to live
    ttl = smap_get(&if_row->options, OVSREC_INTERFACE_OPTIONS_TTL);
    if (ttl)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s", "", "ttl", ttl);
    }

    // Maximum transmission unit
    mtu = smap_get(&if_row->user_config, INTERFACE_USER_CONFIG_MAP_MTU);
    if (mtu)
    {
        VTY_PRINT(p_msg, vty, "%4s%s %s", "", "mtu", mtu);
    }
}

void
print_tunnel_intf_run_cfg(const struct ovsrec_interface *if_row,
                          struct ovsdb_idl *idl,
                          vtysh_ovsdb_cbmsg_ptr p_msg,
                          struct vty *vty)
{
    if (!if_row)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                                  "Invalid interface row");
        return;
    }

    if (strncmp(if_row->type, "vxlan", strlen("vxlan")) == 0)
    {
        print_vxlan_tunnel_running_config(p_msg, if_row, vty, idl);
    }
    else if (strncmp(if_row->type, "gre_ipv4", strlen("gre_ipv4")) == 0)
    {
        print_gre_tunnel_running_config(p_msg, if_row, vty, idl);
    }
}

/*-----------------------------------------------------------------------------
| Function : vtysh_tunnel_intf_context_clientcallback
| Responsibility : Print tunnel interface configurations
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/
vtysh_ret_val
vtysh_tunnel_intf_context_clientcallback(void *p_private)
{
    const struct ovsrec_interface *ifrow = NULL;
    vtysh_ovsdb_cbmsg_ptr p_msg = (vtysh_ovsdb_cbmsg *)p_private;

    vtysh_ovsdb_config_logmsg(
        VTYSH_OVSDB_CONFIG_DBG,
        "vtysh_tunnel_intf_context_clientcallback entered");

    OVSREC_INTERFACE_FOR_EACH(ifrow, p_msg->idl)
    {
        print_tunnel_intf_run_cfg(ifrow, p_msg->idl, p_msg, NULL /* vty */);
    }

    vtysh_ovsdb_cli_print(p_msg,"!");
    return e_vtysh_ok;
}

/*-----------------------------------------------------------------------------
| Function : vtysh_global_vlan_vni_mapping_context_clientcallback
| Responsibility : VNI commands
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/

vtysh_ret_val
vtysh_global_vlan_vni_mapping_context_clientcallback(void *p_private)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    vtysh_ovsdb_cbmsg_ptr p_msg = (vtysh_ovsdb_cbmsg *)p_private;

    vtysh_ovsdb_config_logmsg(
        VTYSH_OVSDB_CONFIG_DBG,
        "vtysh_global_vlan_vni_mapping_context_clientcallback entered");

    OVSREC_VLAN_FOR_EACH(vlan_row, p_msg->idl)
    {
        if (vlan_row->tunnel_key && vlan_row->tunnel_key->tunnel_key)
        {
            vtysh_ovsdb_cli_print(p_msg, "%s %s %ld %s %ld", "vxlan", "vlan",
                                  vlan_row->id, "vni",
                                  vlan_row->tunnel_key->tunnel_key);
        }
    }
    vtysh_ovsdb_cli_print(p_msg,"!");

    return e_vtysh_ok;
}
