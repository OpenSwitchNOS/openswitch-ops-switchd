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

void bridge_init(const char *remote);
void bridge_exit(void);

void bridge_run(void);
void bridge_wait(void);

void bridge_get_memory_usage(struct simap *usage);

#ifdef OPS
void wait_for_config_complete(void);
#endif

#endif /* bridge.h */
