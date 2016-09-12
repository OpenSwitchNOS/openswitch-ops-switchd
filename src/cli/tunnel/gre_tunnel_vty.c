/* GRE tunnel CLI commands
 *
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
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
#include "gre_tunnel_vty.h"
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
#include "vtysh/utils/tunnel_vtysh_utils.h"

VLOG_DEFINE_THIS_MODULE(vtysh_gre_tunnel_interface_cli);
extern struct ovsdb_idl *idl;

DEFUN (cli_gre_tunnel_ip_addr,
       cli_gre_tunnel_ip_addr_cmd,
       "ip address A.B.C.D",
       IP_STR
       "Set IP address\n"
       "Interface IP address\n")
{
    return set_intf_tunnel_ip_addr_by_type(vty,
                                           (char*)vty->index,
                                           INTERFACE_TYPE_GRE_IPV4,
                                           (char*)argv[0]);
}

DEFUN (cli_no_gre_tunnel_ip_addr,
       cli_no_gre_tunnel_ip_addr_cmd,
       "no ip address",
       NO_STR
       IP_STR)
{
    return set_intf_tunnel_ip_addr_by_type(vty,
                                           (char*)vty->index,
                                           INTERFACE_TYPE_GRE_IPV4,
                                           NULL);
}

ALIAS (cli_no_gre_tunnel_ip_addr,
       cli_no_gre_tunnel_ip_addr_val_cmd,
       "no ip address A.B.C.D",
       NO_STR
       IP_STR
       "Set IP address\n"
       IPV4_STR)
{
    return CMD_SUCCESS;
}

DEFUN (cli_gre_tunnel_src_ip,
       cli_gre_tunnel_src_ip_cmd,
       "source ip (A.B.C.D|X:X::X:X)",
       "Set the tunnel source ip\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_src_ip(vty, if_row, argv[0]);
}

DEFUN (cli_no_gre_tunnel_src_ip,
       cli_no_gre_tunnel_src_ip_cmd,
       "no source ip",
       "Remove the tunnel source ip\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_intf_src_ip(if_row);
}

ALIAS (cli_no_gre_tunnel_src_ip,
       cli_no_gre_tunnel_src_ip_val_cmd,
       "no source ip (A.B.C.D|X:X::X:X)",
       "Remove the tunnel source ip\n")

DEFUN (cli_gre_tunnel_src_intf,
       cli_gre_tunnel_src_intf_cmd,
       "source interface IFNUMBER",
       "Set the tunnel source ip from the interface\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_src_intf(vty, if_row, argv[0]);
}

DEFUN (cli_no_gre_tunnel_src_intf,
       cli_no_gre_tunnel_src_intf_cmd,
       "no source interface",
       "Remove the source interface\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_src_intf(if_row);
}

ALIAS (cli_no_gre_tunnel_src_intf,
       cli_no_gre_tunnel_src_intf_val_cmd,
       "no source interface IFNUMBER",
       "Remove the source interface\n")

DEFUN (cli_gre_tunnel_dest_ip,
       cli_gre_tunnel_dest_ip_cmd,
       "destination (A.B.C.D|X:X::X:X)",
       "Set the destination ip\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_dest_ip(if_row, argv[0]);
}

DEFUN (cli_no_gre_tunnel_dest_ip,
       cli_no_gre_tunnel_dest_ip_cmd,
       "no destination",
       TUNNEL_STR
       "Remove the destination ip\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_intf_by_name_and_type((char*)vty->index,
                                       INTERFACE_TYPE_GRE_IPV4);
    if (!if_row)
    {
        vty_out(vty, "Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_intf_dest_ip(if_row);
}

ALIAS (cli_no_gre_tunnel_dest_ip,
       cli_no_gre_tunnel_dest_ip_val_cmd,
       "no destination (A.B.C.D|X:X::X:X)",
       TUNNEL_STR
       "Remove the destination ip\n")

void
gre_tunnel_add_clis(void)
{
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_gre_tunnel_ip_addr_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_no_gre_tunnel_ip_addr_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_no_gre_tunnel_ip_addr_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_gre_tunnel_src_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_gre_tunnel_src_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_gre_tunnel_src_ip_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_gre_tunnel_dest_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_no_gre_tunnel_dest_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_no_gre_tunnel_dest_ip_val_cmd);
    // install_element(CONFIG_NODE, &cli_show_gre_intf_cmd);
}
