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
#include "openswitch-idl.h"
#include "timeval.h"
#include "neighbor.h"
#include "vrf.h"
#include "switchd.h"

VLOG_DEFINE_THIS_MODULE(neighbor);

#define NEIGHBOR_HIT_BIT_UPDATE_INTERVAL   10000

/* Each time this timer expires, go through Neighbor table and query th
** ASIC for data-path hit-bit for each and update DB. */
static int neighbor_timer_interval;
static long long int neighbor_timer = LLONG_MIN;


/* Function to cleanup neighbor from hash, in case of any failures */
void
neighbor_hash_delete(struct vrf *vrf, struct neighbor *neighbor)
{
    VLOG_DBG("In neighbor_hash_delete for neighbor %s", neighbor->ip_address);
    if (neighbor) {
        hmap_remove(&vrf->all_neighbors, &neighbor->node);
        free(neighbor->ip_address);
        free(neighbor->port_name);
        free(neighbor->mac);
        free(neighbor);
    }
}

/* Add neighbor host entry into ofprotoc/asic */
int
neighbor_set_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor)
{
    const struct ovsrec_neighbor *idl_neighbor = neighbor->cfg;
    struct port *port;

    VLOG_DBG("neighbor_set_l3_host_entry called for ip %s and mac %s",
              idl_neighbor->ip_address, idl_neighbor->mac);

    /* Get port info */
    port = port_lookup(vrf->up, neighbor->port_name);
    if (port == NULL) {
        VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
        return 1;
    }

    /* Call Provider */
    if (!ofproto_add_l3_host_entry(vrf->up->ofproto, port,
                                   neighbor->is_ipv6_addr,
                                   idl_neighbor->ip_address,
                                   idl_neighbor->mac,
                                   &neighbor->l3_egress_id)) {
        VLOG_DBG("VRF %s: Added host entry for %s",
                  vrf->up->name, neighbor->ip_address);

        return 0;
    }
    else {
        VLOG_ERR("ofproto_add_l3_host_entry failed");

        /* if l3_intf not configured yet or any failure,
        ** delete from hash */
        neighbor_hash_delete(vrf, neighbor);

        return 1;
    }
} /* neighbor_set_l3_host_entry */

/* Delete port ipv4/ipv6 host entry */
int
neighbor_delete_l3_host_entry(struct vrf *vrf, struct neighbor *neighbor)
{
    struct port *port;

    VLOG_DBG("neighbor_delete_l3_host_entry called for ip %s",
              neighbor->ip_address);

    /* Get port info */
    port = port_lookup(vrf->up, neighbor->port_name);
    if (port == NULL) {
        VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
        return 1;
    }

    /* Call Provider */
    /* Note: Cannot access idl neighbor_cfg as it is already deleted */
    if (!ofproto_delete_l3_host_entry(vrf->up->ofproto, port,
                                      neighbor->is_ipv6_addr,
                                      neighbor->ip_address,
                                      &neighbor->l3_egress_id)) {
        VLOG_DBG("VRF %s: Deleted host entry for ip %s",
                  vrf->up->name, neighbor->ip_address);

        return 0;
    }
    else {
        VLOG_ERR("ofproto_delete_l3_host_entry failed");
        return 1;
    }
} /* neighbor_delete_l3_host_entry */

/* Function to find neighbor in vrf local hash */
struct neighbor*
neighbor_hash_lookup(const struct vrf *vrf, const char *ip_address)
{
    struct neighbor *neighbor;

    HMAP_FOR_EACH_WITH_HASH (neighbor, node, hash_string(ip_address, 0),
                             &vrf->all_neighbors) {
        if (!strcmp(neighbor->ip_address, ip_address)) {
            return neighbor;
        }
    }
    return NULL;
}

/* Read/Reset neighbors data-path hit-bit and update into db */
void
neighbor_run(void)
{
    const struct ovsrec_neighbor *idl_neighbor =
                                  ovsrec_neighbor_first(idl);
    struct neighbor *neighbor;
    int neighbor_interval;
    const struct vrf *vrf;
    struct port *port;
    struct ovsdb_idl_txn *txn;

    /* Skip if nothing to update */
    if (idl_neighbor ==  NULL) {
        return;
    }

    /* TODO: Add the timer-internval in some table/column */
    /* And decide on the interval */
    /* const struct ovsrec_system *idl_ovs =
    **                            ovsrec_system_first(idl);
    ** neighbor_interval = MAX(smap_get_int(&idl_ovs->other_config,
                                         "neighbor-update-interval",
                                         NEIGHBOR_HIT_BIT_UPDATE_INTERVAL),
                                         NEIGHBOR_HIT_BIT_UPDATE_INTERVAL); */
    neighbor_interval = NEIGHBOR_HIT_BIT_UPDATE_INTERVAL;
    if (neighbor_timer_interval != neighbor_interval) {
        neighbor_timer_interval = neighbor_interval;
        neighbor_timer = LLONG_MIN;
    }

    if (time_msec() >= neighbor_timer) {
        //enum ovsdb_idl_txn_status status;

        txn = ovsdb_idl_txn_create(idl);

        /* Rate limit the update.  Do not start a new update if the
        ** previous one is not done. */
        OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
            VLOG_DBG(" Checking hit-bit for %s", idl_neighbor->ip_address);

            vrf = vrf_lookup(idl_neighbor->vrf->name);
            neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
            if (neighbor == NULL) {
                VLOG_ERR("Neighbor not found in local hash");
                continue;
            }

            /* Get port/ofproto info */
            port = port_lookup(neighbor->vrf->up, neighbor->port_name);
            if (port == NULL) {
                VLOG_ERR("Failed to get port cfg for %s", neighbor->port_name);
                continue;
            }

            /* Call Provider */
            if (!ofproto_get_l3_host_hit(neighbor->vrf->up->ofproto, port,
                                        neighbor->is_ipv6_addr,
                                        idl_neighbor->ip_address,
                                        &neighbor->hit_bit)) {
                VLOG_DBG("Got host %s hit bit=0x%x",
                          idl_neighbor->ip_address, neighbor->hit_bit);

                struct smap smap;

                /* Write the hit bit status to status column */
                smap_clone(&smap, &idl_neighbor->status);
                if (neighbor->hit_bit) {
                    smap_replace(&smap, OVSDB_NEIGHBOR_STATUS_DP_HIT, "true");
                } else {
                    smap_replace(&smap, OVSDB_NEIGHBOR_STATUS_DP_HIT, "false");
                }
                ovsrec_neighbor_set_status(idl_neighbor, &smap);
                smap_destroy(&smap);
            }
            else {
                VLOG_ERR("!ofproto_get_l3_host_hit failed");
                continue;
            }
        } /* For each */

        /* No need to retry since we will update with latest state every 10sec */
        ovsdb_idl_txn_commit(txn);
        ovsdb_idl_txn_destroy(txn);

        neighbor_timer = time_msec() + neighbor_timer_interval;
    }
}
