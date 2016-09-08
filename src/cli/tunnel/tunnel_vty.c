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
#include "vtysh_ovsdb_tunnel_context.h"

VLOG_DEFINE_THIS_MODULE(vtysh_tunnel_cli);
extern struct ovsdb_idl *idl;

/* Helper functions */
/*
ovsrec_logical_switch *get_matching_logical_switch(int64_t tunnel_key)
{
    const struct ovsrec_logical_switch *ls_row = NULL;
    OVSREC_LOGICAL_SWITCH_FOR_EACH(ls_row, idl)
    {
        if (ls_row->tunnel_key == tunnel_key)
            break;
    }
    return ls_row;
}

ovsrec_interface *get_matching_interface(char *tunnel_name)
{
    const struct ovsrec_interface *intf_row = NULL;
    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
            break;
    }
    return intf_row;
}

ovsrec_port *get_matching_port(char *tunnel_name)
{
    const struct ovsrec_port *port_row = NULL;
    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
            break;
    }
    return port_row;
}

ovsrec_vlan *get_matching_vlan(char *tunnel_name)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (strcmp(vlan_row->name, tunnel_name) == 0)
            break;
    }
    return vlan_row;
}
*/


const struct ovsrec_logical_switch *
check_vni_id(const struct ovsrec_logical_switch *logical_switch_row, int64_t vni_id)
{
    OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch_row, idl)
    {
        if (logical_switch_row->tunnel_key == (int64_t)vni_id)
            return logical_switch_row;
    }

    return NULL;
}

bool
transaction_status(struct ovsdb_idl_txn *status_txn)
{
    enum ovsdb_idl_txn_status txn_status;
    txn_status = cli_do_config_finish(status_txn);

    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED) {
        return true;
    }
    else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return false;
    }
}

DEFUN (cli_create_tunnel,
        cli_create_tunnel_cmd,
        "interface tunnel <1-99> {mode (vxlan|gre_ipv4)}",
        TUNNEL_STR
        "Create a tunnel interface\n")
{
    const struct ovsrec_interface *intf_row = NULL;
    const struct ovsrec_port *port_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    const struct ovsrec_bridge *default_bridge_row = NULL;
    const struct ovsrec_bridge *bridge_row = NULL;
    int i=0;
    struct ovsrec_interface **interfaces = NULL;
    struct ovsrec_port **ports = NULL;
    char *tunnel_mode = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    vty_out(vty, "argc %d %s", argc, VTY_NEWLINE);
    for (i=0 ; i < argc; i++)
        vty_out(vty, "argv[%d] %s %s", i, argv[i], VTY_NEWLINE);

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);
    tunnel_name[MAX_TUNNEL_LENGTH] = '\0';

    vty_out(vty, "tunnel_name %s %s", tunnel_name, VTY_NEWLINE);

    tunnel_mode = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_mode, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    if (argc == 2)
        snprintf(tunnel_mode, MAX_TUNNEL_LENGTH, "%s", argv[1]);
    vty_out(vty, "tunnel_mode %s %s", tunnel_mode, VTY_NEWLINE);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            vty_out(vty, "Cannot create TUNNEL interface."
                    "Specified interface already exists.%s", VTY_NEWLINE);
            break;
        }
    }

    if(!intf_found)
    {
        if(tunnel_mode != NULL)
        {
            tunnel_txn = cli_do_config_start();
            if (tunnel_txn == NULL)
            {
                VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                         " cli_do_config_start()", __func__, __LINE__);
                cli_do_config_abort(tunnel_txn);
                return CMD_OVSDB_FAILURE;
            }

            /* Create an entry in the Port table */
            port_row = ovsrec_port_insert(tunnel_txn);
            ovsrec_port_set_name(port_row, tunnel_name);

            vty_out(vty, "PORT set_name done %s", VTY_NEWLINE);
            OVSREC_BRIDGE_FOR_EACH (bridge_row, idl)
            {
                if (strcmp(bridge_row->name, DEFAULT_BRIDGE_NAME) == 0) {
                    default_bridge_row = bridge_row;
                    break;
                }
            }

            if(default_bridge_row == NULL)
            {
                assert(0);
                VLOG_DBG("Couldn't fetch default Bridge row. %s:%d",
                        __func__, __LINE__);
                cli_do_config_abort(tunnel_txn);
                return CMD_OVSDB_FAILURE;
            }

            ports = xmalloc(sizeof *default_bridge_row->ports *
                           (default_bridge_row->n_ports + 1));
            for (i = 0; i < default_bridge_row->n_ports; i++)
                ports[i] = default_bridge_row->ports[i];

            ports[default_bridge_row->n_ports] = CONST_CAST(struct ovsrec_port*, port_row);
            ovsrec_bridge_set_ports(default_bridge_row, ports,
                             default_bridge_row->n_ports + 1);
            free(ports);

            /* Create an entry in the Interface table */
            intf_row = ovsrec_interface_insert(tunnel_txn);
            ovsrec_interface_set_name(intf_row, tunnel_name);
            ovsrec_interface_set_type(intf_row, tunnel_mode);

            vty_out(vty, "Interface set_name done %s", VTY_NEWLINE);

            interfaces = xmalloc(sizeof *port_row->interfaces *
                           (port_row->n_interfaces + 1));
            for (i = 0; i < port_row->n_interfaces; i++)
                interfaces[i] = port_row->interfaces[i];

            interfaces[port_row->n_interfaces] =
                            CONST_CAST(struct ovsrec_interface*, intf_row);
            ovsrec_port_set_interfaces(port_row, interfaces,
                            port_row->n_interfaces + 1);
            free(interfaces);

            status_txn = cli_do_config_finish(tunnel_txn);
            if(strcmp(tunnel_mode, "vxlan") == 0)
            {
                if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
                {
                    vty_out(vty, "TXN committed %s", VTY_NEWLINE);
                    vty->node = VXLAN_TUNNEL_INTERFACE_NODE;
                    vty->index = (void *)tunnel_name;
                    vty_out(vty, "tunnel_name %s vty->index %s %s", tunnel_name, (char *)vty->index, VTY_NEWLINE);
                    return CMD_SUCCESS;
                }
                else
                {
                    VLOG_ERR("Transaction commit failed in function=%s, line=%d",__func__,__LINE__);
                    return CMD_OVSDB_FAILURE;
                }
            }
            else
            {
                // Add GRE code here
                vty_out(vty, "Invalid mode %s", VTY_NEWLINE);
                return CMD_WARNING;
            }
        }
        else
        {
            vty_out(vty, "Please provide tunnel mode in order to create the"
                         "tunnel %s", VTY_NEWLINE);
            return CMD_ERR_INCOMPLETE;
        }
    }
    else
    {
        if (argc > 1)
        {
            vty_out(vty, "Tunnel %s already exists... Please don't provide"
                          "tunnel mode %s", tunnel_name, VTY_NEWLINE);

            if (strcmp(tunnel_mode, "vxlan") == 0)
            {
                vty_out(vty, "Interface exists!! %s", VTY_NEWLINE);
                vty->node = VXLAN_TUNNEL_INTERFACE_NODE;
                vty->index = (void *)tunnel_name;
                vty_out(vty, "tunnel_name %s vty->index %s %s", tunnel_name, (char *)vty->index, VTY_NEWLINE);
            }
        }
/*
        else
        {
            // GRE TODO
        }
*/
    }

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

DEFUN (cli_set_tunnel_ip,
        cli_set_tunnel_ip_cmd,
        "ip address (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set the tunnel ip\n")
{
    const struct ovsrec_port *port_row = NULL;
    bool port_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));


    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
        {
            vty_out(vty, "Port found! %s %s", port_row->name, VTY_NEWLINE);
            port_found = true;
            break;
        }
    }

    if(port_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        ovsrec_port_set_ip4_address(port_row, argv[0]);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_tunnel_ip,
        cli_no_set_tunnel_ip_cmd,
        "no ip address (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the tunnel ip\n")
{
    const struct ovsrec_port *port_row = NULL;
    bool port_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
        {
            port_found = true;
            break;
        }
    }

    if(port_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        ovsrec_port_set_ip4_address(port_row, NULL);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot delete the tunnel ip"
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_set_source_intf_ip,
        cli_set_source_intf_ip_cmd,
        "source-interface loopback <1-2147483647>",
        TUNNEL_STR
        "Set the source interface ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    char *src_intf_name = NULL;
    const char *src_ip = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    src_intf_name = xmalloc(MAX_INTF_LENGTH * sizeof(char));
    memset(src_intf_name, 0, MAX_INTF_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);
    snprintf(src_intf_name, MAX_INTF_LENGTH, "%s%s","loopback", argv[0]);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    src_ip = smap_get(&intf_row->options,
                      OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP);

    vty_out(vty, "Source IP %s %s", src_ip, VTY_NEWLINE);
    if(src_ip != NULL)
    {
        vty_out(vty, "Source IP %s is already set for given tunnel!! %s",
                src_ip, VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF,
                     src_intf_name);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel source interface."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_source_intf_ip,
        cli_no_set_source_intf_ip_cmd,
        "no source-interface loopback <1-2147483647>",
        TUNNEL_STR
        "Remove the source interface ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_remove(&options, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot delete tunnel source interface."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_set_source_ip,
        cli_set_source_ip_cmd,
        "source ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set the tunnel source ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    const char *src_intf = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    vty_out(vty, "Set source ip %s %s %s",(char *)vty->index, argv[0], VTY_NEWLINE);
    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    src_intf = smap_get(&intf_row->options, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);

    if(src_intf != NULL)
    {
        vty_out(vty, "Source Interface IP %s is already set for given tunnel!! %s",
                src_intf, VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP,
                     argv[0]);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);
        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel source ip."
                "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_source_ip,
        cli_no_set_source_ip_cmd,
        "no source ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the source ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_remove(&options, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel source interface."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_set_dest_ip,
        cli_set_dest_ip_cmd,
        "destination (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set the destination ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_REMOTE_IP, argv[0]);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_dest_ip,
        cli_no_set_dest_ip_cmd,
        "no destination (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the destination ip\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_remove(&options, OVSREC_INTERFACE_OPTIONS_REMOTE_IP);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot delete tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

static int
set_vxlan_tunnel_key(int64_t vni_id)
{
    const struct ovsrec_system *system_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_bridge *bridge_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    bridge_row = ovsrec_bridge_first(idl);

    if (!(check_vni_id(logical_switch_row, vni_id)))
    {
        logical_switch_row = ovsrec_logical_switch_insert(status_txn);

        ovsrec_logical_switch_set_tunnel_key(logical_switch_row, vni_id);
        ovsrec_logical_switch_set_bridge(logical_switch_row, bridge_row);
        ovsrec_logical_switch_set_from(logical_switch_row, "hw-vtep");
    }

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        vty->node = VNI_NODE;
        vty->index = (void *)vni_id;
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;

}

DEFUN (cli_set_vxlan_tunnel_key,
        cli_set_vxlan_tunnel_key_cmd,
        "vni <1-16777216>",
        TUNNEL_STR
        "Set the tunnel key\n")
{
    int64_t vni_id = (int64_t)(atoi(argv[0]));
    return set_vxlan_tunnel_key(vni_id);
}

static int
no_set_vxlan_tunnel_key(int64_t vni_id)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
        if(logical_switch_row->tunnel_key == vni_id)
        {
            ovsrec_logical_switch_delete(logical_switch_row);
        }
        else
            vty_out(vty,"Tunnel with %ld tunnel_key not found %s",
                    vni_id, VTY_NEWLINE);
    else
       vty_out(vty,"No tunnel with vni %ld found %s", vni_id, VTY_NEWLINE);

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        vty->node = CONFIG_NODE;
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;

}

DEFUN (cli_no_set_vxlan_tunnel_key,
        cli_no_set_vxlan_tunnel_key_cmd,
        "no vni <1-16777216>",
        TUNNEL_STR
        "Remove the vxlan tunnel key\n")
{
    int64_t vni_id = (int64_t)(atoi(argv[0]));
    return no_set_vxlan_tunnel_key(vni_id);
}


static int
set_vxlan_tunnel_name(const char *name)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
    {
        ovsrec_logical_switch_set_name(logical_switch_row, name);
    }
    else
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        vty->node = VNI_NODE;
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_set_vxlan_tunnel_name,
        cli_set_vxlan_tunnel_name_cmd,
        "name TUNNEL_NAME",
        TUNNEL_STR
        "Set the vxlan tunnel name\n")
{
    return set_vxlan_tunnel_name(argv[0]);
}


static int
unset_vxlan_tunnel_name(const char *name)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    int64_t vni_id = (int64_t)vty->index;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->name)
            if (strncmp(logical_switch_row->name, name, strlen(logical_switch_row->name)) == 0)
                ovsrec_logical_switch_set_name(logical_switch_row, NULL);
            else
                vty_out(vty,"Name %s not found in current tunnel config%s", name, VTY_NEWLINE);
        else
            vty_out(vty,"Name not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
    {
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);
    }

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_no_set_vxlan_tunnel_name,
        cli_no_set_vxlan_tunnel_name_cmd,
        "no name TUNNEL_NAME",
        TUNNEL_STR
        "Remove the vxlan tunnel name\n")
{
    return unset_vxlan_tunnel_name(argv[0]);
}

static int
set_vxlan_tunnel_description(const char *desc)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, (int64_t)vty->index);
    if (logical_switch_row)
        ovsrec_logical_switch_set_description(logical_switch_row, desc);
    else
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        vty->node = VNI_NODE;
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_set_tunnel_description,
        cli_set_tunnel_description_cmd,
        "description TUNNEL_DESCRIPTION",
        TUNNEL_STR
        "Set the vxlan tunnel description\n")
{
    return set_vxlan_tunnel_description(argv[0]);
}


static int
unset_vxlan_tunnel_description(const char *description)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->description)
            if (strncmp(logical_switch_row->description, description, strlen(logical_switch_row->description)) == 0)
                ovsrec_logical_switch_set_description(logical_switch_row, NULL);
            else
                vty_out(vty,"Description %s not found in current tunnel config%s", description, VTY_NEWLINE);
        else
            vty_out(vty,"Description not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                (int64_t)vty->index, VTY_NEWLINE);

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_no_set_tunnel_description,
        cli_no_set_tunnel_description_cmd,
        "no description TUNNEL_DESCRIPTION",
        TUNNEL_STR
        "Remove the vxlan tunnel description\n")
{
    return unset_vxlan_tunnel_description(argv[0]);
}

static int
set_mcast_group_ip(const char *mcast_ip)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, (int64_t)vty->index);
    if (logical_switch_row)
        ovsrec_logical_switch_set_mcast_group_ip(logical_switch_row, mcast_ip);
    else
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_set_multicast_group_ip,
        cli_set_multicast_group_ip_cmd,
        "mcast-group-ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set multicast group ip\n")
{
    return set_mcast_group_ip(argv[0]);
}


static int
unset_vxlan_tunnel_mcast_group_ip(const char *mcast_ip)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->mcast_group_ip)
            if (strncmp(logical_switch_row->mcast_group_ip, mcast_ip, strlen(logical_switch_row->mcast_group_ip)) == 0)
                ovsrec_logical_switch_set_mcast_group_ip(logical_switch_row, NULL);
            else
                vty_out(vty,"Mcast group ip %s not found for the current tunnel config%s", mcast_ip, VTY_NEWLINE);
        else
            vty_out(vty,"Multicast group ip not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
    {
        vty_out(vty,"Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);
    }

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_no_set_multicast_group_ip,
        cli_no_set_multicast_group_ip_cmd,
        "no mcast-group-ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the multicast group ip\n")
{
    return unset_vxlan_tunnel_mcast_group_ip(argv[0]);
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
        "vlan VLAN_NUMBER vni <1-16777216>",
        TUNNEL_STR
        "Set per-port vlan to vni mapping\n")
{
    const struct ovsrec_port *port_row = NULL;
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    bool port_found = false;
    bool ls_found = false;
    bool vlan_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    struct ovsrec_vlan **vlan_list = NULL;
    struct ovsrec_logical_switch **tunnel_key_list = NULL;
    int i = 0;
    char *tunnel_name = NULL;
    char *vlan_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));
    vlan_name = xmalloc(MAX_VLAN_LENGTH * sizeof(char));
    memset(vlan_name, 0, MAX_VLAN_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);
    snprintf(vlan_name, MAX_VLAN_LENGTH, "%s%s","vlan", argv[0]);

    vty_out(vty, "tunnel_name %s vlan_name %s %s", tunnel_name, vlan_name, VTY_NEWLINE);
    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
        {
            vty_out(vty, "Port found %s", VTY_NEWLINE);
            port_found = true;
            break;
        }
    }

    OVSREC_LOGICAL_SWITCH_FOR_EACH(ls_row, idl)
    {
        if (ls_row->tunnel_key == (int64_t)atoi(argv[1]))
        {
            vty_out(vty, "LS found %s", VTY_NEWLINE);
            ls_found = true;
            break;
        }
    }
    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (strcasecmp(vlan_row->name, vlan_name) == 0)
        {
            vty_out(vty, "Vlan found %s", VTY_NEWLINE);
            vlan_found = true;
            break;
        }
    }
    if(port_found && ls_found && vlan_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        vlan_list = xmalloc((sizeof(*port_row->key_vlan_tunnel_keys) *
                       (port_row->n_vlan_tunnel_keys + 1)));
        for (i = 0; i < port_row->n_vlan_tunnel_keys; i++)
            vlan_list[i] = port_row->key_vlan_tunnel_keys[i];

        vlan_list[port_row->n_vlan_tunnel_keys] =
            CONST_CAST(struct ovsrec_vlan*, vlan_row);

        vty_out(vty, "i %d port_row->n_vlan_tunnel_keys %ld after vlan update %s",
                i, port_row->n_vlan_tunnel_keys, VTY_NEWLINE);
        vty_out(vty, "Vlan_name %s vlan_list %s %s", vlan_row->name,
                vlan_list[0]->name, VTY_NEWLINE);

        tunnel_key_list = xmalloc(sizeof (*port_row->value_vlan_tunnel_keys) *
                       (port_row->n_vlan_tunnel_keys + 1));
        for (i = 0; i < port_row->n_vlan_tunnel_keys; i++)
            tunnel_key_list[i] = port_row->value_vlan_tunnel_keys[i];

        tunnel_key_list[port_row->n_vlan_tunnel_keys] =
            CONST_CAST(struct ovsrec_logical_switch*, ls_row);

        vty_out(vty, "i %d port_row->n_vlan_tunnel_keys %ld after logical sw update %s",
                i, port_row->n_vlan_tunnel_keys, VTY_NEWLINE);

        vty_out(vty, "tunnel_key %ld tunnel_key_list %ld %s", ls_row->tunnel_key,
                tunnel_key_list[0]->tunnel_key, VTY_NEWLINE);
        ovsrec_port_set_vlan_tunnel_keys(port_row, vlan_list, tunnel_key_list,
                                         (port_row->n_vlan_tunnel_keys + 1));


        status_txn = cli_do_config_finish(tunnel_txn);

        free(vlan_list);
        free(tunnel_key_list);

        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify vlan to vni mapping."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vlan_to_vni_mapping,
        cli_no_set_vlan_to_vni_mapping_cmd,
        "no vlan VLAN_NUMBER vni <1-16777216>",
        TUNNEL_STR
        "Remove vlan to vni mapping\n")
{
    const struct ovsrec_port *port_row = NULL;
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    bool port_found = false;
    bool ls_found = false;
    bool vlan_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    struct ovsrec_vlan **vlans = NULL;
    struct ovsrec_logical_switch **tunnel_keys = NULL;
    int i = 0, j = 0;

    char *tunnel_name = NULL;
    char *vlan_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));
    vlan_name = xmalloc(MAX_VLAN_LENGTH * sizeof(char));
    memset(vlan_name, 0, MAX_VLAN_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);
    snprintf(vlan_name, MAX_VLAN_LENGTH, "%s%s", "vlan", argv[0]);

    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
        {
            port_found = true;
            break;
        }
    }

    OVSREC_LOGICAL_SWITCH_FOR_EACH(ls_row, idl)
    {
        if ((int)ls_row->tunnel_key == atoi(argv[1]))
        {
            ls_found = true;
            break;
        }
    }
    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (strcasecmp(vlan_row->name, vlan_name) == 0)
        {
            vlan_found = true;
            break;
        }
    }
    if(port_found && ls_found && vlan_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        vlans = xmalloc(sizeof *port_row->key_vlan_tunnel_keys *
                       (port_row->n_vlan_tunnel_keys - 1));
        tunnel_keys = xmalloc(sizeof *port_row->value_vlan_tunnel_keys *
                       (port_row->n_vlan_tunnel_keys + 1));

        for (i = 0, j = 0; i < port_row->n_vlan_tunnel_keys; i++)
        {
            if (strcmp(vlan_row->name, port_row->key_vlan_tunnel_keys[i]->name) == 0)
                continue;
            else
            {
                vlans[j++] = port_row->key_vlan_tunnel_keys[i];
                tunnel_keys[j++] = port_row->value_vlan_tunnel_keys[i];
            }
        }

        ovsrec_port_set_vlan_tunnel_keys(port_row, vlans, tunnel_keys,
                                         port_row->n_vlan_tunnel_keys - 1);

        status_txn = cli_do_config_finish(tunnel_txn);

        free(vlans);
        free(tunnel_keys);

        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify vlan to vni mapping."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

static int
set_global_vlan_to_vni_mapping(int argc, const char **argv)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_system *system_row = NULL;
    bool vlan_found = false;
    int64_t vlan_id = (int64_t)atoi(argv[0]);
    int64_t vni_id = (int64_t)atoi(argv[1]);
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (vlan_row->id == vlan_id)
        {
            vlan_found = true;
            break;
        }
    }

    if (vlan_found)
    {
        logical_switch_row = check_vni_id(logical_switch_row, vni_id);
        if (logical_switch_row)
            ovsrec_vlan_set_tunnel_key(vlan_row, logical_switch_row);
        else
            printf("Tunnel not found\n");
    }
    else
        printf("vlan not found\n");

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_set_global_vlan_to_vni_mapping,
        cli_set_global_vlan_to_vni_mapping_cmd,
        "vxlan vlan <1-4094> vni <1-16777216>",
        TUNNEL_STR
        "Global vlan to vni mapping\n")
{
    return set_global_vlan_to_vni_mapping(argc, argv);
}

static int
unset_global_vlan_to_vni_mapping(int argc, const char **argv)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_system *system_row = NULL;
    bool vlan_found = false;
    int64_t vlan_id = (int64_t)atoi(argv[0]);
    int64_t vni_id = (int64_t)atoi(argv[1]);
    bool txn_flag = false;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    logical_switch_row = check_vni_id(logical_switch_row, vni_id);
    if (logical_switch_row)
    {
        OVSREC_VLAN_FOR_EACH(vlan_row, idl)
        {
            if (vlan_row->id == vlan_id)
            {
                vlan_found = true;
                break;
            }
        }
    }

    if (vlan_found)
    {
        if (vlan_row->tunnel_key->tunnel_key == vni_id)
            ovsrec_vlan_set_tunnel_key(vlan_row, NULL);
        else
           printf("vlan vni pair not found\n");
    }
    else
        printf("vlan not found\n");

    txn_flag = transaction_status(status_txn);
    if (txn_flag)
    {
        return CMD_SUCCESS;
    }
    else
        return CMD_OVSDB_FAILURE;
}

DEFUN (cli_no_set_global_vlan_to_vni_mapping,
        cli_no_set_global_vlan_to_vni_mapping_cmd,
        "no vxlan vlan <1-4094> vni <1-16777216>",
        TUNNEL_STR
        "Unset global vlan to vni mapping\n")
{
    return unset_global_vlan_to_vni_mapping(argc, argv);
}

DEFUN (cli_set_vxlan_udp_port,
        cli_set_vxlan_udp_port_cmd,
        "vxlan udp-port <1-65535>",
        TUNNEL_STR
        "Set the vxlan udp port\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    vty_out(vty, "UDP-Port %s %s", argv[0], VTY_NEWLINE);
    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            vty_out(vty, "Interface %s %s", tunnel_name, VTY_NEWLINE);
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT,
                    argv[0]);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        vty_out(vty, "status_txn %d TXN_COMMITED %s", status_txn, VTY_NEWLINE);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vxlan_udp_port,
        cli_no_set_vxlan_udp_port_cmd,
        "no vxlan udp-port <1-65535>",
        TUNNEL_STR
        "Set the vxlan port to default (4789)\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_remove(&options, OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

DEFUN (cli_set_vni_list,
        cli_set_vni_list_cmd,
        "vni {vni_id1 vni_id2 ... vni_idn}",
        TUNNEL_STR
        "Set the list of VNIs used by an interface\n")
{
/*
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    bool intf_found = false;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = xmalloc(MAX_TUNNEL_LENGTH * sizeof(char));
    memset(tunnel_name, 0, MAX_TUNNEL_LENGTH * sizeof(char));

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s", (char *)vty->index);

    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            intf_found = true;
            break;
        }
    }

    if(intf_found)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_VNI_LIST,
                    argv[0]);
        smap_add(&options, "vxlan_udp_port", "4789");
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
*/
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vni_list,
        cli_no_set_vni_list_cmd,
        "vni {vni_id1 vni_id2 ... vni_idn}",
        TUNNEL_STR
        "Set the list of VNIs used by an interface\n")
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

    ovsdb_idl_add_table(idl, &ovsrec_table_vlan);
    ovsdb_idl_add_column(idl, &ovsrec_vlan_col_tunnel_key);
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
    vtysh_ret_val retval = e_vtysh_error;

    /* Installing interface vxlan related commands */
    install_element(CONFIG_NODE, &cli_create_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_delete_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_intf_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_vni_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_mac_table_cmd);
    install_element(CONFIG_NODE, &cli_show_vxlan_statistics_cmd);
    install_element(CONFIG_NODE, &cli_set_global_vlan_to_vni_mapping_cmd);
    install_element(CONFIG_NODE, &cli_no_set_global_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_tunnel_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_tunnel_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_source_intf_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_intf_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_source_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vxlan_tunnel_key_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vxlan_tunnel_key_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vxlan_udp_port_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vxlan_udp_port_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vni_list_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vni_list_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &vtysh_exit_tunnel_interface_cmd);
    //install_element (VXLAN_TUNNEL_INTERFACE_NODE, &vtysh_quit_tunnel_interface_cmd);
    install_element (VXLAN_TUNNEL_INTERFACE_NODE, &vtysh_end_all_cmd);

    /* Installing vni related commands */
    install_element(CONFIG_NODE, &cli_set_vxlan_tunnel_key_cmd);
    install_element(CONFIG_NODE, &cli_no_set_vxlan_tunnel_key_cmd);
    install_element(VNI_NODE, &cli_set_tunnel_description_cmd);
    install_element(VNI_NODE, &cli_no_set_tunnel_description_cmd);
    install_element(VNI_NODE, &cli_set_vxlan_tunnel_name_cmd);
    install_element(VNI_NODE, &cli_no_set_vxlan_tunnel_name_cmd);
    install_element(VNI_NODE, &cli_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_no_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_set_replication_group_ips_cmd);
    install_element(VNI_NODE, &cli_no_set_replication_group_ips_cmd);
    install_element(VNI_NODE, &vtysh_exit_vni_cmd);
    //install_element(VNI_NODE, &vtysh_quit_vni_cmd);
    install_element (VNI_NODE, &vtysh_end_all_cmd);

    /* Installing running config sub-context with global config context */
    retval = install_show_run_config_subcontext(e_vtysh_config_context,
                                     e_vtysh_config_context_tunnel,
                                     &vtysh_tunnel_context_clientcallback,
                                     NULL, NULL);
    if(e_vtysh_ok != retval)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                           "config context unable to add vni client callback");
        assert(0);
    }

    /* Installing running config sub-context with global config context */
    retval = install_show_run_config_subcontext(e_vtysh_config_context,
                                     e_vtysh_config_context_tunnel_intf,
                                     &vtysh_tunnel_intf_context_clientcallback,
                                     NULL, NULL);
    if(e_vtysh_ok != retval)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                           "config context unable to add tunnel interface client callback");
        assert(0);
    }

}
