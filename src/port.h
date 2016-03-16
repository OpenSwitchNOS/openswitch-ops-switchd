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

#ifndef SWITCHD_PORT_H
#define SWITCHD_PORT_H 1

#include "hmap.h"
#include "vswitch-idl.h"
#include "ofproto/ofproto.h"
#include "iface.h"

struct bridge;
struct vrf;

void port_init();

struct port {
    struct hmap_node hmap_node; /* Element in struct bridge's "ports" hmap. */
    struct bridge *bridge;
    char *name;

    const struct ovsrec_port *cfg;

    /* An ordinary bridge port has 1 interface.
     * A bridge port for bonding has at least 2 interfaces. */
    struct ovs_list ifaces;    /* List of "struct iface"s. */
    int bond_hw_handle;        /* Hardware bond identifier. */
};

struct port * port_create(struct bridge *br, const struct ovsrec_port *cfg);
void port_del_ifaces(struct port *port);
void port_destroy(struct port *port);
struct port * port_lookup(const struct bridge *br, const char *name);
bool enable_lacp(struct port *port, bool *activep);
void port_configure_bond(struct port *port, struct bond_settings *s);
void port_configure(struct port *port);
void bridge_delete_or_reconfigure_ports(struct bridge *);
void vrf_delete_or_reconfigure_ports(struct vrf *);

#endif /* port.h */
