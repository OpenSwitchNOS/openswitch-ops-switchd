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

#include "openvswitch/vlog.h"
#include "ofproto/ofproto.h"
#include "vswitch-idl.h"
#include "switchd-ofproto.h"
#include "iface.h"
#include "vrf.h"

VLOG_DEFINE_THIS_MODULE(switchdofproto);

void
switchd_ofproto_init(const struct ovsrec_system *cfg)
{
    struct shash iface_hints;
    static bool initialized = false;
    int i;

    if (initialized) {
        return;
    }

    shash_init(&iface_hints);

    if (cfg) {
        for (i = 0; i < cfg->n_bridges; i++) {
            const struct ovsrec_bridge *br_cfg = cfg->bridges[i];
            int j;

            for (j = 0; j < br_cfg->n_ports; j++) {
                struct ovsrec_port *port_cfg = br_cfg->ports[j];
                int k;

                for (k = 0; k < port_cfg->n_interfaces; k++) {
                    struct ovsrec_interface *if_cfg = port_cfg->interfaces[k];
                    struct iface_hint *iface_hint;

                    iface_hint = xmalloc(sizeof *iface_hint);
                    iface_hint->br_name = br_cfg->name;
                    iface_hint->br_type = br_cfg->datapath_type;
                    iface_hint->ofp_port = iface_pick_ofport(if_cfg);
                    shash_add(&iface_hints, if_cfg->name, iface_hint);
                }
            }
        }

        for (i = 0; i < cfg->n_vrfs; i++) {
            const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];
            int j;

            for (j = 0; j < vrf_cfg->n_ports; j++) {
                struct ovsrec_port *port_cfg = vrf_cfg->ports[j];
                int k;

                for (k = 0; k < port_cfg->n_interfaces; k++) {
                    struct ovsrec_interface *if_cfg = port_cfg->interfaces[k];
                    struct iface_hint *iface_hint;

                    iface_hint = xmalloc(sizeof *iface_hint);
                    iface_hint->br_name = vrf_cfg->name;
                    iface_hint->br_type = "vrf";
                    iface_hint->ofp_port = iface_pick_ofport(if_cfg);
                    shash_add(&iface_hints, if_cfg->name, iface_hint);
                }
            }
        }

    }

    ofproto_init(&iface_hints);

    shash_destroy_free_data(&iface_hints);
    initialized = true;
}

void
switchd_ofproto_run(void)
{
    struct bridge *br;
    struct vrf *vrf;
    struct sset types;
    const char *type;

    /* Let each datapath type do the work that it needs to do. */
    sset_init(&types);
    ofproto_enumerate_types(&types);
    SSET_FOR_EACH (type, &types) {
        ofproto_type_run(type);
    }
    sset_destroy(&types);

    /* Let each bridge do the work that it needs to do. */
    HMAP_FOR_EACH (br, node, &all_bridges) {
        ofproto_run(br->ofproto);
    }

    HMAP_FOR_EACH (vrf, node, &all_vrfs) {
        ofproto_run(vrf->up->ofproto);
    }
}
