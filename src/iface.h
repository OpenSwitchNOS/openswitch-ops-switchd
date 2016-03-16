/* Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
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

#ifndef SWITCHD_IFACE_H
#define SWITCHD_IFACE_H 1

#include "vswitch-idl.h"
#include "bridge.h"

extern long long int stats_timer;

struct port;

struct iface {
    /* These members are always valid.
     *
     * They are immutable: they never change between iface_create() and
     * iface_destroy(). */
    struct ovs_list port_elem;  /* Element in struct port's "ifaces" list. */
    struct hmap_node name_node; /* In struct bridge's "iface_by_name" hmap. */
    struct hmap_node ofp_port_node; /* In struct bridge's "ifaces" hmap. */
    struct port *port;          /* Containing port. */
    char *name;                 /* Host network device name. */
    struct netdev *netdev;      /* Network device. */
    ofp_port_t ofp_port;        /* OpenFlow port number. */
    uint64_t change_seq;

    /* These members are valid only within bridge_reconfigure(). */
    const char *type;           /* Usually same as cfg->type. */
    const struct ovsrec_interface *cfg;
};

bool iface_create(struct bridge *, const struct ovsrec_interface *,
                  const struct ovsrec_port *);
void iface_destroy(struct iface *);
void iface_destroy__(struct iface *);
struct iface *iface_lookup(const struct bridge *, const char *name);
const char *iface_get_type(const struct ovsrec_interface *,
                           const struct ovsrec_bridge *);
struct iface *iface_from_ofp_port(const struct bridge *, ofp_port_t);
void iface_refresh_stats(struct iface *iface);
void vrf_delete_or_reconfigure_subintf(struct smap *sub_intf_info,
         const struct ovsrec_interface *);
void iface_refresh_netdev_status(struct iface *,
                                 bool status_txn_try_again);
void iface_refresh_ofproto_status(struct iface *);
ofp_port_t iface_pick_ofport(const struct ovsrec_interface *);

void iface_stats_run();
void iface_stats_wait();
#endif
