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

#include "vlan.h"
#include "openswitch-idl.h"
#include "switchd.h"

VLOG_DEFINE_THIS_MODULE(vlan);

static void
vlan_unixctl_show(struct unixctl_conn *, int argc,
                  const char *argv[], void *aux OVS_UNUSED);

void
vlan_init()
{
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_admin);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_description);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_oper_state);
    ovsdb_idl_omit(idl, &ovsrec_vlan_col_oper_state_reason);

    unixctl_command_register("vlan/show", "[vid]", 0, 1,
                             vlan_unixctl_show, NULL);
}

struct vlan *
vlan_lookup_by_name(const struct bridge *br, const char *name)
{
    struct vlan *vlan;

    HMAP_FOR_EACH_WITH_HASH (vlan, hmap_node, hash_string(name, 0),
                             &br->vlans) {
        if (!strcmp(vlan->name, name)) {
            return vlan;
        }
    }
    return NULL;
}

struct vlan *
vlan_lookup_by_vid(const struct bridge *br, int vid)
{
    struct vlan *vlan;

    HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
        if (vlan->vid == vid) {
            return vlan;
        }
    }
    return NULL;
}

void
dump_vlan_data(struct ds *ds, struct vlan *vlan)
{
    ds_put_format(ds, "VLAN %d:\n", vlan->vid);
    ds_put_format(ds, "  name               :%s\n", vlan->name);
    ds_put_format(ds, "  cfg                :%p\n", vlan->cfg);
    ds_put_format(ds, "  hw_vlan_cfg:enable :%d\n", vlan->enable);
}


void
vlan_create(struct bridge *br, const struct ovsrec_vlan *vlan_cfg)
{
    struct vlan *new_vlan = NULL;

    /* Allocate structure to save state information for this VLAN. */
    new_vlan = xzalloc(sizeof(struct vlan));

    hmap_insert(&br->vlans, &new_vlan->hmap_node,
                hash_string(vlan_cfg->name, 0));

    new_vlan->bridge = br;
    new_vlan->cfg = vlan_cfg;
    new_vlan->vid = (int)vlan_cfg->id;
    new_vlan->name = xstrdup(vlan_cfg->name);

    /* Initialize state to disabled.  Will handle this later. */
    new_vlan->enable = false;
}

void
vlan_destroy(struct vlan *vlan)
{
    if (vlan) {
        struct bridge *br = vlan->bridge;
        hmap_remove(&br->vlans, &vlan->hmap_node);
        free(vlan->name);
        free(vlan);
    }
}

void
bridge_configure_vlans(struct bridge *br, unsigned int idl_seqno)
{
    size_t i;
    struct vlan *vlan, *next;
    struct shash sh_idl_vlans;
    struct shash_node *sh_node;

    /* Collect all the VLANs present in the DB. */
    shash_init(&sh_idl_vlans);
    for (i = 0; i < br->cfg->n_vlans; i++) {
        const char *name = br->cfg->vlans[i]->name;
        if (!shash_add_once(&sh_idl_vlans, name, br->cfg->vlans[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge VLAN",
                      br->name, name);
        }
    }

    /* Delete old VLANs. */
    HMAP_FOR_EACH_SAFE (vlan, next, hmap_node, &br->vlans) {
        const struct ovsrec_vlan *vlan_cfg;

        vlan_cfg = shash_find_data(&sh_idl_vlans, vlan->name);
        if (!vlan_cfg) {
            VLOG_DBG("Found a deleted VLAN %s", vlan->name);
            /* Need to update ofproto now since this VLAN
             * won't be around for the "check for changes"
             * loop below. */
            ofproto_set_vlan(br->ofproto, vlan->vid, 0);
            vlan_destroy(vlan);
        }
    }

    /* Add new VLANs. */
    SHASH_FOR_EACH (sh_node, &sh_idl_vlans) {
        vlan = vlan_lookup_by_name(br, sh_node->name);
        if (!vlan) {
            VLOG_DBG("Found an added VLAN %s", sh_node->name);
            vlan_create(br, sh_node->data);
        }
    }

    /* Check for changes in the VLAN row entries. */
    HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
        const struct ovsrec_vlan *row = vlan->cfg;

        /* Check for changes to row. */
        if (OVSREC_IDL_IS_ROW_INSERTED(row, idl_seqno) ||
            OVSREC_IDL_IS_ROW_MODIFIED(row, idl_seqno)) {
            bool new_enable = false;
            const char *hw_cfg_enable;

            // Check for hw_vlan_config:enable string changes.
            hw_cfg_enable = smap_get(&row->hw_vlan_config, VLAN_HW_CONFIG_MAP_ENABLE);
            if (hw_cfg_enable) {
                if (!strcmp(hw_cfg_enable, VLAN_HW_CONFIG_MAP_ENABLE_TRUE)) {
                    new_enable = true;
                }
            }

            if (new_enable != vlan->enable) {
                VLOG_DBG("  VLAN %d changed, enable=%d, new_enable=%d.  "
                         "idl_seq=%d, insert=%d, mod=%d",
                         vlan->vid, vlan->enable, new_enable, idl_seqno,
                         row->header_.insert_seqno,
                         row->header_.modify_seqno);

                vlan->enable = new_enable;
                ofproto_set_vlan(br->ofproto, vlan->vid, vlan->enable);
            }
        }
    }

    /* Destroy the shash of the IDL vlans */
    shash_destroy(&sh_idl_vlans);
}

static void
vlan_unixctl_show(struct unixctl_conn *conn, int argc,
                  const char *argv[], void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct vlan *vlan = NULL;
    struct bridge *br;

    HMAP_FOR_EACH (br, node, &all_bridges) {
        ds_put_format(&ds, "========== Bridge %s ==========\n", br->name);

        /* Check for optional VID parameter.  We'll accept
         * either an integer VID or name of VLAN. */
        if (argc > 1) {
            int vid = strtol(argv[1], NULL, 10);
            if (vid > 0) {
                vlan = vlan_lookup_by_vid(br, vid);
            } else {
                vlan = vlan_lookup_by_name(br, argv[1]);
            }
            if (vlan == NULL) {
                ds_put_format(&ds, "VLAN %s is not in this bridge.\n",
                              argv[1]);
                continue;
            }
        }

        if (vlan != NULL) {
            dump_vlan_data(&ds, vlan);
        } else {
            HMAP_FOR_EACH (vlan, hmap_node, &br->vlans) {
                dump_vlan_data(&ds, vlan);
            }
        }
    }

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}
