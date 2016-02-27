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

#ifndef VSWITCHD_VRF_H
#define VSWITCHD_VRF_H 1

#include <netinet/in.h>
#include "hmap.h"
#include "vswitch-idl.h"
#include "ofproto/ofproto.h"

#define VRF_IPV4_MAX_LEN        32
#define VRF_IPV6_MAX_LEN        128
#define VRF_ROUTE_HASH_MAXSIZE  64 /* max prefixlen (49) + maxlen of "from" */

struct bridge; /* forward declaration */
struct vrf {
    struct bridge *up;
    struct hmap_node node;              /* In 'all_vrfs'. */
    const struct ovsrec_vrf *cfg;
    struct hmap all_neighbors;
    struct hmap all_routes;
    struct hmap all_nexthops;
};

/* Local Neighbor struct to store in hash-map and handle add/modify/deletes */
struct neighbor {
    struct hmap_node node;               /* 'all_neighbors'. */
    char *ip_address;                    /* IP */
    char *mac;                           /* MAC */
    const struct ovsrec_neighbor *cfg;   /* IDL */
    bool is_ipv6_addr;                   /* Quick flag for type */
    bool hit_bit;                        /* Remember hit-bit */
    struct vrf *vrf;                     /* Things needed for delete case */
    char *port_name;
    int l3_egress_id;
};

struct route {
    struct hmap_node node;          /* vrf->all_routes */

    char *prefix;                   /* route prefix */
    char *from;                     /* routing protocol (BGP, OSPF) using this route */
    bool is_ipv6;                   /* IP V4/V6 */
    struct hmap nexthops;           /* list of selected next hops */

    struct vrf *vrf;
    const struct ovsrec_route *idl_row;
};

struct nexthop {
    char *ip_addr;                  /* next hop ip address */
    char *port_name;                /* port pointed to by next hop */
    bool hw_programmed;             /* is this next hop programmed in h/w? */
    struct hmap_node node;          /* route->nexthops */
    struct hmap_node vrf_node;      /* vrf->all_nexthops */
    struct route *route;            /* route pointing to this nexthop */

    struct ovsrec_nexthop *idl_row;
};

struct ecmp {
    bool enabled;
    bool src_port_enabled;
    bool dst_port_enabled;
    bool src_ip_enabled;
    bool dst_ip_enabled;
    bool resilient_hash_enabled;
};

void vrf_reconfigure_routes(struct vrf *vrf);
void vrf_ofproto_update_route_with_neighbor(struct vrf *vrf,
                                            struct neighbor *neighbor,
                                            bool resolved);
int vrf_l3_route_action(struct vrf *vrf, enum ofproto_route_action action,
                        struct ofproto_route *route);
bool vrf_has_l3_route_action(struct vrf *vrf);
struct neighbor *neighbor_hash_lookup(const struct vrf *vrf,
                                      const char *ip_address);
int vrf_l3_ecmp_set(struct vrf *vrf, bool enable);
int vrf_l3_ecmp_hash_set(struct vrf *vrf, unsigned int hash, bool enable);
void vrf_port_reconfig_ipaddr(struct port *port,
                              struct ofproto_bundle_settings *bundle_setting);
#endif /* vrf.h */
