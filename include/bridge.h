/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2014 Nicira, Inc.
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

#ifndef VSWITCHD_BRIDGE_H
#define VSWITCHD_BRIDGE_H 1

#ifdef OPS
#include <netinet/in.h>
#include "hmap.h"
#include "vswitch-idl.h"
#include "ofproto/ofproto.h"
#endif

struct simap;

#ifdef OPS
struct bridge {
    struct hmap_node node;      /* In 'all_bridges'. */
    char *name;                 /* User-specified arbitrary name. */
    char *type;                 /* Datapath type. */
    struct eth_addr ea;         /* Bridge Ethernet Address. */
    struct eth_addr default_ea; /* Default MAC. */
    const struct ovsrec_bridge *cfg;

    /* OpenFlow switch processing. */
    struct ofproto *ofproto;    /* OpenFlow switch. */

    /* Bridge ports. */
    struct hmap ports;          /* "struct port"s indexed by name. */
    struct hmap ifaces;         /* "struct iface"s indexed by ofp_port. */
    struct hmap iface_by_name;  /* "struct iface"s indexed by name. */

    /* Port mirroring. */
    struct hmap mirrors;        /* "struct mirror" indexed by UUID. */

    /* Bridge VLANs. */
    struct hmap vlans;          /* "struct vlan"s indexed by VID. */

    /* Used during reconfiguration. */
    struct shash wanted_ports;

    /* Synthetic local port if necessary. */
    struct ovsrec_port synth_local_port;
    struct ovsrec_interface synth_local_iface;
    struct ovsrec_interface *synth_local_ifacep;
};
#endif

struct port {
    struct hmap_node hmap_node; /* Element in struct bridge's "ports" hmap. */
    struct bridge *bridge;
    char *name;

    const struct ovsrec_port *cfg;

    /* An ordinary bridge port has 1 interface.
     * A bridge port for bonding has at least 2 interfaces. */
    struct ovs_list ifaces;    /* List of "struct iface"s. */
#ifdef OPS
    int bond_hw_handle;        /* Hardware bond identifier. */
#endif
};

#ifdef OPS
struct vlan {
    struct hmap_node hmap_node;  /* In struct bridge's "vlans" hmap. */
    struct bridge *bridge;
    char *name;
    int vid;
    const struct ovsrec_vlan *cfg;

    bool enable;
};
#endif

#ifdef OPS
#define LAG_PORT_NAME_PREFIX "lag"
#define LAG_PORT_NAME_PREFIX_LENGTH (3)
#endif

void bridge_init(const char *remote);
void bridge_exit(void);

void bridge_run(void);
void bridge_wait(void);

void bridge_get_memory_usage(struct simap *usage);

#ifdef OPS
void wait_for_config_complete(void);
struct bridge* get_bridge_from_port_name (char *port_name, struct port **port);
#endif

#endif /* bridge.h */
