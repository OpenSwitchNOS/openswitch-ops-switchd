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
#include "openswitch-idl.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/system_vtysh_utils.h"
#include "vtysh_ovsdb_tunnel_context.h"

/*-----------------------------------------------------------------------------
| Function : vtysh_vni_context_clientcallback
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
                              "vtysh_config_context_tunnel_clientcallback entered");

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
