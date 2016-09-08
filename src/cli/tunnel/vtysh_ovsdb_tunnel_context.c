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

extern struct ovsdb_idl *idl;

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
      vtysh_ovsdb_cli_print(p_msg, "%s %ld", "vni", logical_switch->tunnel_key);

      if (logical_switch->name)
          vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "name", logical_switch->name);

      if (logical_switch->description)
          vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "description", logical_switch->description);

      if (logical_switch->mcast_group_ip)
          vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "mcast-group-ip", logical_switch->mcast_group_ip);
    }
    vtysh_ovsdb_cli_print(p_msg,"!");

    return e_vtysh_ok;
}

/*-----------------------------------------------------------------------------
| Function : vtysh_tunnel_intf_context_clientcallback
| Responsibility : VNI commands
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/

int
get_id_from_name(const char *name, const char *static_str)
{
    int i = 0, id_number = 0;
    char id_str[5] = {0};
    int ss_len = strlen(static_str);

    for(i=0; i < (strlen(name) - ss_len); i++)
        id_str[i] = name[i+ss_len];
    id_str[i] = '\0';
    id_number = atoi(id_str);

    return id_number;
}



vtysh_ret_val
vtysh_tunnel_intf_context_clientcallback(void *p_private)
{
    const struct ovsrec_interface *ifrow = NULL;
    const struct ovsrec_port *port_row = NULL;
    const char *dest_ip = NULL;
    const char *src_ip = NULL;
    const char *src_intf_ip = NULL;
    const char *vni_list = NULL;
    const char *udp_port = NULL;
    int tunnel_number = 0;
    int loopback_id = 0;
    bool port_found = false;
    vtysh_ovsdb_cbmsg_ptr p_msg = (vtysh_ovsdb_cbmsg *)p_private;

    vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_DBG,
                              "vtysh_tunnel_intf_context_clientcallback entered");

    OVSREC_INTERFACE_FOR_EACH(ifrow, p_msg->idl)
    {
        if (strncmp(ifrow->type, "vxlan", strlen("vxlan")) == 0)
        {
            tunnel_number = get_id_from_name((const char*)ifrow->name, "tunnel");
            vtysh_ovsdb_cli_print(p_msg, "%s %s %d %s %s", "interface",
                                  "tunnel", tunnel_number, "mode",
                                  ifrow->type);

            dest_ip = smap_get(&ifrow->options,
                               OVSREC_INTERFACE_OPTIONS_REMOTE_IP);
            if (dest_ip != NULL)
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "destination",
                                      dest_ip);

            src_ip = smap_get(&ifrow->options,
                              OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP);
            if (src_ip != NULL)
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "source",
                                      src_ip);

            src_intf_ip = smap_get(&ifrow->options,
                              OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);
            if (src_intf_ip != NULL)
            {
                loopback_id = get_id_from_name(src_intf_ip, "loopback");
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s %d", "",
                                      "source-interface", "loopback",
                                      loopback_id);
            }

            vni_list = smap_get(&ifrow->options,
                                OVSREC_INTERFACE_OPTIONS_VNI_LIST);
            if (vni_list != NULL)
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "vni",
                                      vni_list);

            OVSREC_PORT_FOR_EACH(port_row, idl)
            {
                if (strcmp(port_row->name, ifrow->name) == 0)
                {
                    port_found = true;
                    break;
                }
            }

            if (port_found)
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "ip address",
                                      port_row->ip4_address);

            udp_port = smap_get(&ifrow->options,
                                OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT);
            if (udp_port != NULL)
                vtysh_ovsdb_cli_print(p_msg, "%4s %s %s", "", "vxlan",
                                      udp_port);
        }
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

    vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_DBG,
                              "vtysh_global_vlan_vni_mapping_context_clientcallback entered");

    OVSREC_VLAN_FOR_EACH(vlan_row, p_msg->idl)
    {
      if (vlan_row->tunnel_key->tunnel_key)
          vtysh_ovsdb_cli_print(p_msg, "%s %s %ld %s %ld", "vxlan", "vlan", vlan_row->id, "vni", vlan_row->tunnel_key->tunnel_key);
    }
    vtysh_ovsdb_cli_print(p_msg,"!");

    return e_vtysh_ok;
}
