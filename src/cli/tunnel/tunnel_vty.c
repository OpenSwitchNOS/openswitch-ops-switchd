/* Tunnel CLI commands
 *
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * File: tunnel_vty.c
 *
 * Purpose:  To add tunnel CLI configuration and display commands.
 */

#include <inttypes.h>
#include <netdb.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <lib/version.h>
#include "command.h"
#include "tunnel_vty.h"
#include "memory.h"
#include "prefix.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"
#include "ovsdb-idl.h"
#include "smap.h"
#include "vswitch-idl.h"
#include "vtysh/vtysh.h"
#include "vtysh/vtysh_user.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/intf_vtysh_utils.h"

VLOG_DEFINE_THIS_MODULE(vtysh_tunnel_cli);
extern struct ovsdb_idl *idl;


DEFUN (cli_create_tunnel,
        cli_create_tunnel_cmd,
        "interface TUNNEL_INTF TUNNEL_INTF_NUMBER | {mode TUNNEL_TYPE}",
        TUNNEL_STR
        "Create a tunnel interface\n")
{
    return CMD_SUCCESS;
}

DEFUN (cli_delete_tunnel,
        cli_delete_tunnel_cmd,
        "no interface TUNNEL_INTF TUNNEL_INTF_NUMBER",
        TUNNEL_STR
        "Delate a tunnel interface\n")
{
    return CMD_SUCCESS;
}

DEFUN (cli_set_source_intf_ip,
        cli_set_source_intf_ip_cmd,
        "source-interface loopback LOOPBACK_INTF_NUMBER",
        TUNNEL_STR
        "Set the source interface ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_source_intf_ip,
        cli_no_set_source_intf_ip_cmd,
        "no source-interface loopback LOOPBACK_INTF_NUMBER",
        TUNNEL_STR
        "Remove the source interface ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_dest_ip,
        cli_set_dest_ip_cmd,
        "destination (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set the destination ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_dest_ip,
        cli_no_set_dest_ip_cmd,
        "no destination (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the destination ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_vxlan_tunnel_key,
        cli_set_vxlan_tunnel_key_cmd,
        "vni TUNNEL_KEY",
        TUNNEL_STR
        "Set the tunnel key\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vxlan_tunnel_key,
        cli_no_set_vxlan_tunnel_key_cmd,
        "no vni TUNNEL_KEY",
        TUNNEL_STR
        "Remove the vxlan tunnel key\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_vxlan_tunnel_name,
        cli_set_vxlan_tunnel_name_cmd,
        "name TUNNEL_NAME",
        TUNNEL_STR
        "Set the vxlan tunnel name\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_tunnel_description,
        cli_set_tunnel_description_cmd,
        "description TUNNEL_DESCRIPTION",
        TUNNEL_STR
        "Set the vxlan tunnel description\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_multicast_group_ip,
        cli_set_multicast_group_ip_cmd,
        "mcast-group (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set multicast group ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_multicast_group_ip,
        cli_no_set_multicast_group_ip_cmd,
        "no mcast-group (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the multicast group ip\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_replication_group_ips,
        cli_set_replication_group_ips_cmd,
        "replication-group (A.B.C.D|X:X::X:X)...(A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set replication group ips\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_replication_group_ips,
        cli_no_set_replication_group_ips_cmd,
        "no replication-group (A.B.C.D|X:X::X:X)...(A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the given ip from replication group\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_vlan_to_vni_mapping,
        cli_set_vlan_to_vni_mapping_cmd,
        "vlan VLAN_NUMBER vni TUNNEL_KEY",
        TUNNEL_STR
        "Set vlan to vni mapping\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vlan_to_vni_mapping,
        cli_no_set_vlan_to_vni_mapping_cmd,
        "no vlan VLAN_NUMBER vni TUNNEL_KEY",
        TUNNEL_STR
        "Remove vlan to vni mapping\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_vxlan_udp_port,
        cli_set_vxlan_udp_port_cmd,
        "vxlan udp-port UDP_PORT",
        TUNNEL_STR
        "Set the vxlan port\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vxlan_udp_port,
        cli_no_set_vxlan_udp_port_cmd,
        "no vxlan udp-port UDP_PORT",
        TUNNEL_STR
        "Set the vxlan port to default (4789)\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_show_vxlan_intf,
        cli_show_vxlan_intf_cmd,
        "show interface vxlan {TUNNEL_INTF TUNNEL_INTF_NUMBER | VTEP}",
        TUNNEL_STR
        "Show tunnel interface info\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_show_vxlan_vni,
        cli_show_vxlan_vni_cmd,
        "show vni {TUNNEL_KEY}",
        TUNNEL_STR
        "Show vxlan tunnel info\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_show_vxlan_mac_table,
        cli_show_vxlan_mac_table_cmd,
        "show vxlan mac-table {FROM | MAC_ADDR | VLANS | REMOTE_VTEP}",
        TUNNEL_STR
        "Show vxlan tunnel info\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_show_vxlan_statistics,
        cli_show_vxlan_statistics_cmd,
        "show vxlan statistics",
        TUNNEL_STR
        "Show vxlan tunnel statistics info\n")
{

    return CMD_SUCCESS;
}


/*================================================================================================*/

static void
tunnel_ovsdb_init()
{
    ovsdb_idl_add_table(idl, &ovsrec_table_port);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_interfaces);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_ip4_address);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_ip4_address_secondary);

    ovsdb_idl_add_table(idl, &ovsrec_table_logical_switch);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_tunnel_key);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_mcast_group_ip);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_replication_group_ips);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_description);

    ovsdb_idl_add_table(idl, &ovsrec_table_interface);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_type);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_options);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_statistics);
}

/* Initialize ops-switchd cli node. */

void
cli_pre_init(void)
{
    /* ops-switchd doesn't have any context level cli commands.
     * To load ops-switchd cli shared libraries at runtime, this function is required.
     */
    /* Tunnel tables. */
    tunnel_ovsdb_init();
}

/* Install Tunnel related vty commands. */

void
cli_post_init(void)
{
    install_element(CONFIG_NODE, &cli_create_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_delete_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_intf_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_vni_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_mac_table_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_statistics_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_source_intf_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_intf_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vxlan_tunnel_key_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vxlan_tunnel_key_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_tunnel_description_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vxlan_udp_port_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vxlan_udp_port_cmd);
    install_element(VNI_NODE, &cli_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_no_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_set_replication_group_ips_cmd);
    install_element(VNI_NODE, &cli_no_set_replication_group_ips_cmd);
}
