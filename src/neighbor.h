/* Copyright (C) 2015. 2016 Hewlett-Packard Development Company, L.P.
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

#ifndef SWITCHD_NEIGHBOR_H
#define SWITCHD_NEIGHBOR_H 1

#include "hmap.h"

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

void neighbor_hash_delete(struct vrf *vrf, struct neighbor *neighbor);
int neighbor_set_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor);
int neighbor_delete_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor);
struct neighbor*
neighbor_hash_lookup(const struct vrf *vrf, const char *ip_address);

void neighbor_run(void);

#endif /* neighbor.h */
