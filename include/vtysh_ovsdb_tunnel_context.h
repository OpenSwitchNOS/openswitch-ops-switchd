/* Tunnel client callback registration header file.
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP.
 *
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
 * File: vtysh_ovsdb_tunnel_context.h
 *
 * Purpose:  To add declarations required for vtysh_ovsdb_tunnel_context.c
 *
 */

#ifndef VTYSH_OVSDB_TUNNEL_CONTEXT_H
#define VTYSH_OVSDB_TUNNEL_CONTEXT_H

#define VTY_PRINT(cbmsg, vty, ...) {\
            if (vty) {\
                vty_out(vty, __VA_ARGS__);\
                vty_out(vty, VTY_NEWLINE);\
            } else {\
                vtysh_ovsdb_cli_print(cbmsg, __VA_ARGS__);\
            }\
        }

vtysh_ret_val vtysh_tunnel_context_clientcallback(void *p_private);
vtysh_ret_val vtysh_tunnel_intf_context_clientcallback(void *p_private);
vtysh_ret_val vtysh_global_vlan_vni_mapping_context_clientcallback(void *p_private);
void print_tunnel_intf_run_cfg(const struct ovsrec_interface *if_row,
                               struct ovsdb_idl *idl,
                               vtysh_ovsdb_cbmsg_ptr p_msg,
                               struct vty *vty);

#endif /* VTYSH_OVSDB_TUNNEL_CONTEXT_H */
