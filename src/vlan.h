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

#ifndef SWITCHD_VLAN_H
#define SWITCHD_VLAN_H 1

#include "hmap.h"
#include "unixctl.h"
#include "vswitch-idl.h"
#include "bridge.h"
#include "openvswitch/vlog.h"

struct vlan {
    struct hmap_node hmap_node;  /* In struct bridge's "vlans" hmap. */
    struct bridge *bridge;
    char *name;
    int vid;
    const struct ovsrec_vlan *cfg;
    bool enable;
};

void vlan_init();
struct vlan *vlan_lookup_by_name(const struct bridge *br, const char *name);
struct vlan *vlan_lookup_by_vid(const struct bridge *br, int vid);
void dump_vlan_data(struct ds *ds, struct vlan *vlan);
void vlan_create(struct bridge *br, const struct ovsrec_vlan *vlan_cfg);
void vlan_destroy(struct vlan *vlan);
void bridge_configure_vlans(struct bridge *br, unsigned int idl_seqno);
#endif /* vlan.h */
