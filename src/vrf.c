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

#include <arpa/inet.h>
#include <errno.h>
#include "bridge.h"
#include "vrf.h"
#include "hash.h"
#include "shash.h"
#include "ofproto/ofproto.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"

VLOG_DEFINE_THIS_MODULE(vrf);

extern struct ovsdb_idl *idl;
extern unsigned int idl_seqno;

/* global ecmp config (not per VRF) - default values set here */
struct ecmp ecmp_config = {true, true, true, true, true, true};

static struct nexthop * vrf_nexthop_add(struct vrf *vrf, struct route *route,
                                        const struct ovsrec_nexthop *nh_row);

/* == Managing routes == */
/* VRF maintains a per-vrf route hash of Routes->hash(Nexthop1, Nexthop2, ...) per-vrf.
 * VRF maintains a per-vrf nexthop hash with backpointer to the route entry.
 * The nexthop hash is only maintained for nexthops with IP address and not for
 * nexthops that point to interfaces. This hash is maintained so that when a
 * neighbor ARP gets resolved, we can quickly look up the route entry that has
 * a nexthop with the same IP as the neighbor that got resolved and update the
 * route entry in the system.
 *
 * When route is created, Route hash is updated with the new route and the list
 * of nexthops in the route. ofproto API is called to program this route and the
 * list of nexthops. Use the egress id and MAC resolved fields from the neighbor
 * hash for this nexthop. Also, nexthop hash entry is created with this route.
 *
 * When route is deleted, route hash and all its next hops are deleted. ofproto
 * API is called to delete this route from system. nexthops are also deleted from
 * the nexthop hash.
 *
 * When route is modified (means nexthops are added/deleted from the route),
 * route hash's nexthop list is updated and ofproto API is called to delete
 * and add the new nexthops being added.
 *
 * When neighbor entry is created (means a neighbor IP got MAC resolved), the
 * nexthop hash is searched for all nexthops that has the same IP as the neighbor
 * that got resolved and the routes associated with the nexthops are updated
 * in the system.
 *
 * When neighbor entry is deleted, all routes in the nexthop hash matching the
 * neighbor IP will be updated in ofproto with the route->nexthop marked as MAC
 * unresolved.
 *
 * Note: Nexthops are assumed to have either IP or port, but not both.
 */

/* determine if nexthop row is selected. Default is true */
static bool
vrf_is_nh_row_selected(const struct ovsrec_nexthop *nh_row)
{
    if (!nh_row->selected) { /* if not configured, default is true */
        return true;
    } else if (nh_row->selected[0]) { /* configured and value set as true */
        return true;
    }

    return false;
}

/* determine if route row is selected. Default is false */
static bool
vrf_is_route_row_selected(const struct ovsrec_route *route_row)
{
    if (route_row->selected && route_row->selected[0]) {
        /* configured and value set as true */
        return true;
    }
    return false;
}

static void
vrf_route_hash(char *from, char *prefix, char *hashstr, int hashlen)
{
    snprintf(hashstr, hashlen, "%s:%s", from, prefix);
}

static char *
vrf_nh_hash(char *ip_address, char *port_name)
{
    char *hashstr;
    if (ip_address) {
        hashstr = ip_address;
    } else {
        hashstr = port_name;
    }
    return hashstr;
}

/* Try and find the nexthop matching the db entry in the route->nexthops hash */
static struct nexthop *
vrf_route_nexthop_lookup(struct route *route, char *ip_address, char *port_name)
{
    char *hashstr;
    struct nexthop *nh;

    hashstr = vrf_nh_hash(ip_address, port_name);
    HMAP_FOR_EACH_WITH_HASH(nh, node, hash_string(hashstr, 0), &route->nexthops) {
        /* match either the ip address or the first port name */
        if ((nh->ip_addr && (strcmp(nh->ip_addr, ip_address) == 0)) ||
            ((nh->port_name && (strcmp(nh->port_name, port_name) == 0)))) {
            return nh;
        }
    }
    return NULL;
}

/* call ofproto API to add this route and nexthops */
static void
vrf_ofproto_route_add(struct vrf *vrf, struct ofproto_route *ofp_route,
                      struct route *route)
{

    int i;
    int rc = 0;
    struct nexthop *nh;

    ofp_route->family = route->is_ipv6 ? OFPROTO_ROUTE_IPV6 : OFPROTO_ROUTE_IPV4;
    ofp_route->prefix = route->prefix;

    if ((rc = vrf_l3_route_action(vrf, OFPROTO_ROUTE_ADD, ofp_route)) == 0) {
        VLOG_DBG("Route added for %s", route->prefix);
    } else {
        VLOG_ERR("Unable to add route for %s. rc %d", route->prefix, rc);
    }

    if (VLOG_IS_DBG_ENABLED()) {
        VLOG_DBG("--------------------------");
        VLOG_DBG("ofproto add route. family (%d), prefix (%s), nhs (%d)",
                  ofp_route->family, route->prefix, ofp_route->n_nexthops);
        for (i = 0; i < ofp_route->n_nexthops; i++) {
            VLOG_DBG("NH : state (%d), l3_egress_id (%d), rc (%d)",
                      ofp_route->nexthops[i].state,
                      ofp_route->nexthops[i].l3_egress_id,
                      ofp_route->nexthops[i].rc);
        }
        VLOG_DBG("--------------------------");
    }

    /* process the nexthop return code */
    for (i = 0; i < ofp_route->n_nexthops; i++) {
        if (ofp_route->nexthops[i].type == OFPROTO_NH_IPADDR) {
            nh = vrf_route_nexthop_lookup(route, ofp_route->nexthops[i].id,
                                          NULL);
        } else {
            nh = vrf_route_nexthop_lookup(route, NULL,
                                          ofp_route->nexthops[i].id);
        }

        if (nh) {
            const struct ovsrec_nexthop *nh_idl_row;

            nh_idl_row = ovsrec_nexthop_get_for_uuid(idl,
                          (const struct uuid*)&nh->idl_row_uuid);
            if (nh_idl_row) {
                struct smap nexthop_error;
                const char *error = smap_get(&nh_idl_row->status,
                                         OVSDB_NEXTHOP_STATUS_ERROR);

                if (ofp_route->nexthops[i].rc != 0) { /* ofproto error */
                    smap_init(&nexthop_error);
                    smap_add(&nexthop_error, OVSDB_NEXTHOP_STATUS_ERROR,
                             ofp_route->nexthops[i].err_str);
                    VLOG_DBG("Update error status with '%s'",
                                            ofp_route->nexthops[i].err_str);
                    ovsrec_nexthop_set_status(nh_idl_row, &nexthop_error);
                    smap_destroy(&nexthop_error);
                } else { /* ofproto success */
                    if (error) { /* some error already set in db, clear it */
                        VLOG_DBG("Clear error status");
                        ovsrec_nexthop_set_status(nh_idl_row, NULL);
                    }
                }
            } else {
                VLOG_DBG("Nexthop %s already got deleted",
                         ofp_route->nexthops[i].id);
            }
        }

        /* Free temp info passed to PD */
        free(ofp_route->nexthops[i].id);
    }
}

/* call ofproto API to delete this route and nexthops */
static void
vrf_ofproto_route_delete(struct vrf *vrf, struct ofproto_route *ofp_route,
                         struct route *route, bool del_route)
{
    int i;
    int rc = 0;
    enum ofproto_route_action action;

    ofp_route->family = route->is_ipv6 ? OFPROTO_ROUTE_IPV6 : OFPROTO_ROUTE_IPV4;
    ofp_route->prefix = route->prefix;
    action = del_route ? OFPROTO_ROUTE_DELETE : OFPROTO_ROUTE_DELETE_NH;

    if ((rc = vrf_l3_route_action(vrf, action, ofp_route)) == 0) {
        VLOG_DBG("Route deleted for %s", route->prefix);
    } else {
        VLOG_ERR("Unable to delete route for %s. rc %d", route->prefix, rc);
    }
    for (i = 0; i < ofp_route->n_nexthops; i++) {
        free(ofp_route->nexthops[i].id);
    }

    if (VLOG_IS_DBG_ENABLED()) {
        VLOG_DBG("--------------------------");
        VLOG_DBG("ofproto delete route [%d] family (%d), prefix (%s), nhs (%d)",
                  del_route, ofp_route->family, route->prefix,
                  ofp_route->n_nexthops);
        for (i = 0; i < ofp_route->n_nexthops; i++) {
            VLOG_DBG("NH : state (%d), l3_egress_id (%d)",
                      ofp_route->nexthops[i].state,
                      ofp_route->nexthops[i].l3_egress_id);
        }
        VLOG_DBG("--------------------------");
    }
}

/* Update an ofproto route with the neighbor as [un]resolved. */
void
vrf_ofproto_update_route_with_neighbor(struct vrf *vrf,
                                       struct neighbor *neighbor, bool resolved)
{
    char *hashstr;
    struct nexthop *nh;
    struct ofproto_route ofp_route;

    VLOG_DBG("%s : neighbor %s, resolved : %d", __func__, neighbor->ip_address,
                                                resolved);
    hashstr = vrf_nh_hash(neighbor->ip_address, NULL);
    HMAP_FOR_EACH_WITH_HASH(nh, vrf_node, hash_string(hashstr, 0),
                            &vrf->all_nexthops) {
        /* match the neighbor's IP address */
        if (nh->ip_addr && (strcmp(nh->ip_addr, neighbor->ip_address) == 0)) {
            /* Fill ofp_route for PD and free after returning in
             * vrf_ofproto_route_add */
            ofp_route.nexthops[0].state =
                        resolved ? OFPROTO_NH_RESOLVED : OFPROTO_NH_UNRESOLVED;
            if (resolved) {
                ofp_route.nexthops[0].l3_egress_id = neighbor->l3_egress_id;
            }
            ofp_route.nexthops[0].rc = 0;
            ofp_route.nexthops[0].type = OFPROTO_NH_IPADDR;
            ofp_route.nexthops[0].id = xstrdup(nh->ip_addr);
            ovs_assert(ofp_route.nexthops[0].id);
            ofp_route.n_nexthops = 1;
            vrf_ofproto_route_add(vrf, &ofp_route, nh->route);
        }
    }
}

/* Populate the ofproto nexthop entry with only resolved ones first,
** if no resolved ones fill with one selected NH for ASIC to Copy2Cpu */
static int
vrf_ofproto_add_resolved_nh(struct vrf *vrf,
                            const struct ovsrec_route *route_row,
                            struct route *route_entry,
                            struct ofproto_route *ofp_route)
{
    int i;
    struct nexthop *nh_entry;
    const struct ovsrec_nexthop *nh_row;
    struct neighbor *neighbor = NULL;
    struct ofproto_route_nexthop *ofp_nh;

    /* First add all selected to local route->nh hash to handle modify later */
    for (i = 0; i < route_row->n_nexthops; i++) {
        nh_row = route_row->nexthops[i];

        /* valid IP or valid port */
        if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address ||
           ((nh_row->n_ports > 0) && nh_row->ports[0]))) {
            if ((nh_entry = vrf_nexthop_add(vrf, route_entry, nh_row))) {
                VLOG_DBG("Added NH to route->nh hash");
            } else {
                VLOG_DBG("Failed to add NH to route->nh hash");
            }
        }
    }

    /* Now add only resolved ip NH or port based NH for ofproto->asic */
    ofp_route->n_nexthops = 0;
    for (i = 0; i < route_row->n_nexthops; i++) {
        nh_row = route_row->nexthops[i];

        /* Check for NH in Neigbor hash if resolved */
        if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address ||
           ((nh_row->n_ports > 0) && nh_row->ports[0]))) {
            nh_entry = vrf_route_nexthop_lookup(route_entry, nh_row->ip_address,
                          nh_row->n_ports > 0 ? nh_row->ports[0]->name : NULL);
            if (nh_entry == NULL) {
                VLOG_ERR("NH not in route->nh hash");
                continue;
            }

            if (nh_entry->port_name) { /* nexthop is a port */
                ofp_nh = &ofp_route->nexthops[ofp_route->n_nexthops];
                ofp_nh->rc = 0;
                ofp_nh->state = OFPROTO_NH_UNRESOLVED;
                ofp_nh->type  = OFPROTO_NH_PORT;
                ofp_nh->id = xstrdup(nh_entry->port_name);
                VLOG_DBG("Adding: nexthop port : (%s)", nh_entry->port_name);
                ofp_route->n_nexthops++;
            } else {
                neighbor = neighbor_hash_lookup(vrf, nh_entry->ip_addr);
                if ( (neighbor) && (neighbor->l3_egress_id > 0) ) {
                    ofp_nh = &ofp_route->nexthops[ofp_route->n_nexthops];
                    ofp_nh->rc = 0;
                    ofp_nh->state = OFPROTO_NH_RESOLVED;
                    ofp_nh->l3_egress_id = neighbor->l3_egress_id;
                    ofp_nh->type  = OFPROTO_NH_IPADDR;
                    ofp_nh->id = xstrdup(nh_entry->ip_addr);
                    VLOG_DBG("Adding : resolved nexthop IP : (%s)",
                             nh_entry->ip_addr);
                    ofp_route->n_nexthops++;
                }
            }
        }
    }

    /* If none is resolved and no port based, fill atleast one for ASIC */
    /* Fill one ip based to Copy2Cpu */
    if (ofp_route->n_nexthops == 0) {
        VLOG_DBG("Filling atleast one un-resolved NH for asic");
        for (i = 0; i < route_row->n_nexthops; i++) {
            nh_row = route_row->nexthops[i];
            if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address)) {
                nh_entry = vrf_route_nexthop_lookup(route_entry,
                                                    nh_row->ip_address,
                                                    NULL);
                if (nh_entry == NULL) {
                    VLOG_ERR("NH not in route->nh hash");
                    continue;
                }

                ofp_nh = &ofp_route->nexthops[ofp_route->n_nexthops];
                ofp_nh->rc = 0;
                ofp_nh->type  = OFPROTO_NH_IPADDR;
                ofp_nh->state = OFPROTO_NH_UNRESOLVED;
                ofp_nh->id = xstrdup(nh_entry->ip_addr);
                VLOG_DBG("Adding: nexthop IP : (%s), with copy2cpu",
                         nh_entry->ip_addr);
                ofp_route->n_nexthops++;
                break;
           }
        }
    }

    VLOG_DBG("Returning with %d NH", ofp_route->n_nexthops);
    return ofp_route->n_nexthops;
}

/* Added newly added NH to the ofproto nexthop entry only if resolved ip NH
** or port based NH */
/* Need this function to not increment n_nexthops++ for un-resolved ones */
static void
vrf_ofproto_update_resolved_nh(struct vrf *vrf, struct ofproto_route *ofp_route,
                               struct nexthop *nh,
                               struct ofproto_route_nexthop *ofp_nh)
{
    struct neighbor *neighbor;

    ofp_nh->rc = 0;
    if (nh->port_name) { /* nexthop is a port */
        ofp_nh->state = OFPROTO_NH_UNRESOLVED;
        ofp_nh->type  = OFPROTO_NH_PORT;
        ofp_nh->id = xstrdup(nh->port_name);
        VLOG_DBG("%s : nexthop port : (%s)", __func__, nh->port_name);
        ofp_route->n_nexthops++;
    } else { /* nexthop has IP */
        neighbor = neighbor_hash_lookup(vrf, nh->ip_addr);
        if ( (neighbor) && (neighbor->l3_egress_id > 0) ) {
            ofp_nh->type  = OFPROTO_NH_IPADDR;
            ofp_nh->state = OFPROTO_NH_RESOLVED;
            ofp_nh->l3_egress_id = neighbor->l3_egress_id;
            ofp_nh->id = xstrdup(nh->ip_addr);
            VLOG_DBG("%s : nexthop IP : (%s), neighbor %s found", __func__,
                nh->ip_addr, neighbor ? "" : "not");
            ofp_route->n_nexthops++;
        }
    }
}

/* populate the ofproto nexthop entry with information from the nh */
static void
vrf_ofproto_set_nh(struct vrf *vrf, struct ofproto_route_nexthop *ofp_nh,
                   struct nexthop *nh)
{
    struct neighbor *neighbor;

    ofp_nh->rc = 0;
    if (nh->port_name) { /* nexthop is a port */
        ofp_nh->state = OFPROTO_NH_UNRESOLVED;
        ofp_nh->type  = OFPROTO_NH_PORT;
        ofp_nh->id = xstrdup(nh->port_name);
        VLOG_DBG("%s : nexthop port : (%s)", __func__, nh->port_name);
    } else { /* nexthop has IP */
        ofp_nh->type  = OFPROTO_NH_IPADDR;
        neighbor = neighbor_hash_lookup(vrf, nh->ip_addr);
        if ( (neighbor) && (neighbor->l3_egress_id > 0) ) {
            ofp_nh->state = OFPROTO_NH_RESOLVED;
            ofp_nh->l3_egress_id = neighbor->l3_egress_id;
        } else {
            ofp_nh->state = OFPROTO_NH_UNRESOLVED;
        }
        ofp_nh->id = xstrdup(nh->ip_addr);
        VLOG_DBG("%s : nexthop IP : (%s), neighbor %s found", __func__,
                nh->ip_addr, ( (neighbor) && (neighbor->l3_egress_id > 0) ) ?
                              "" : "not");
    }
}


/* Delete the nexthop from the route entry in the local cache */
static int
vrf_nexthop_delete(struct vrf *vrf, struct route *route, struct nexthop *nh)
{
    if (!route || !nh) {
        return -1;
    }

    VLOG_DBG("Cache delete NH %s/%s in route %s/%s",
              nh->ip_addr ? nh->ip_addr : "", nh->port_name ? nh->port_name : "",
              route->from, route->prefix);
    hmap_remove(&route->nexthops, &nh->node);
    if (nh->ip_addr) {
        hmap_remove(&vrf->all_nexthops, &nh->vrf_node);
        free(nh->ip_addr);
    }
    if (nh->port_name) {
        free(nh->port_name);
    }
    free(nh);

    return 0;
}

/* Add the nexthop into the route entry in the local cache */
static struct nexthop *
vrf_nexthop_add(struct vrf *vrf, struct route *route,
                const struct ovsrec_nexthop *nh_row)
{
    char *hashstr;
    struct nexthop *nh;

    if (!route || !nh_row) {
        return NULL;
    }

    nh = xzalloc(sizeof(*nh));
    /* NOTE: Either IP or Port, not both */
    if (nh_row->ip_address) {
        nh->ip_addr = xstrdup(nh_row->ip_address);
    } else if ((nh_row->n_ports > 0) && nh_row->ports[0]) {
        /* consider only one port for now */
        nh->port_name = xstrdup(nh_row->ports[0]->name);
    } else {
        VLOG_ERR("No IP address or port[0] in the nexthop entry");
        free(nh);
        return NULL;
    }
    nh->route = route;
    /* Store uuid for referring later instead of direct pointer to idl row */
    memcpy(&nh->idl_row_uuid,
           &OVSREC_IDL_GET_TABLE_ROW_UUID(nh_row), sizeof(struct uuid));

    hashstr = nh_row->ip_address ? nh_row->ip_address : nh_row->ports[0]->name;
    hmap_insert(&route->nexthops, &nh->node, hash_string(hashstr, 0));
    if (nh_row->ip_address) { /* only add nexthops with IP address */
        hmap_insert(&vrf->all_nexthops, &nh->vrf_node, hash_string(hashstr, 0));
    }

    VLOG_DBG("Cache add NH %s/%s from route %s/%s",
              nh->ip_addr ? nh->ip_addr : "", nh->port_name ? nh->port_name : "",
              route->from, route->prefix);
    return nh;
}

/* find a route entry in local cache matching the prefix,from in IDL route row */
static struct route *
vrf_route_hash_lookup(struct vrf *vrf, const struct ovsrec_route *route_row)
{
    struct route *route;
    char hashstr[VRF_ROUTE_HASH_MAXSIZE];

    vrf_route_hash(route_row->from, route_row->prefix, hashstr, sizeof(hashstr));
    HMAP_FOR_EACH_WITH_HASH(route, node, hash_string(hashstr, 0), &vrf->all_routes) {
        if ((strcmp(route->prefix, route_row->prefix) == 0) &&
            (strcmp(route->from, route_row->from) == 0)) {
            return route;
        }
    }
    return NULL;
}

/* delete route entry from cache */
static void
vrf_route_delete(struct vrf *vrf, struct route *route)
{
    struct nexthop *nh, *next;
    struct ofproto_route ofp_route;

    if (!route) {
        return;
    }

    VLOG_DBG("Cache delete route %s/%s",
            route->from ? route->from : "", route->prefix ? route->prefix : "");
    hmap_remove(&vrf->all_routes, &route->node);

    ofp_route.n_nexthops = 0;
    HMAP_FOR_EACH_SAFE(nh, next, node, &route->nexthops) {
        vrf_ofproto_set_nh(vrf, &ofp_route.nexthops[ofp_route.n_nexthops], nh);
        if (vrf_nexthop_delete(vrf, route, nh) == 0) {
            ofp_route.n_nexthops++;
        }
    }
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_delete(vrf, &ofp_route, route, true);
    }
    if (route->prefix) {
        free(route->prefix);
    }
    if (route->from) {
        free(route->from);
    }

    free(route);
}

/* Add the new route and its NHs into the local cache */
static void
vrf_route_add(struct vrf *vrf, const struct ovsrec_route *route_row)
{
    int i;
    struct route *route;
    struct nexthop *nh;
    const struct ovsrec_nexthop *nh_row;
    char hashstr[VRF_ROUTE_HASH_MAXSIZE];
    struct ofproto_route ofp_route;

    if (!route_row) {
        return;
    }

    route = xzalloc(sizeof(*route));
    route->prefix = xstrdup(route_row->prefix);
    route->from = xstrdup(route_row->from);
    if (route_row->address_family &&
        (strcmp(route_row->address_family, OVSREC_NEIGHBOR_ADDRESS_FAMILY_IPV6)
                                                                        == 0)) {
        route->is_ipv6 = true;
    }

    hmap_init(&route->nexthops);
    ofp_route.n_nexthops = 0;
    /* If ECMP check what is the status of nexthops
    ** -If atleast one resolved pass that only to asic.
    ** -If none of them are resolved pass only one to asic.
    ** -And pass only resolved ones from 2nd one onwards.
    */
    if (route_row->n_nexthops > 1) {
        vrf_ofproto_add_resolved_nh(vrf, route_row, route, &ofp_route);
    } else {
        /* If non-ECMP send to asic even if not resolved */
        for (i = 0; i < route_row->n_nexthops; i++) {
            nh_row = route_row->nexthops[i];

            /* valid IP or valid port. consider only one port for now */
            if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address ||
               ((nh_row->n_ports > 0) && nh_row->ports[0]))) {
                if ((nh = vrf_nexthop_add(vrf, route, nh_row))) {
                   vrf_ofproto_set_nh(vrf,
                                      &ofp_route.nexthops[ofp_route.n_nexthops],
                                      nh);
                   ofp_route.n_nexthops++;
                }
            }
        }
    }

    /* If got any valid/selected NH, pass it to asic */
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_add(vrf, &ofp_route, route);
    }

    /* Add this new route to vrf->route hash */
    route->vrf = vrf;
    /* Store uuid for referring later instead of direct pointer to idl row */
    memcpy(&route->idl_row_uuid,
           &OVSREC_IDL_GET_TABLE_ROW_UUID(route_row), sizeof(struct uuid));

    vrf_route_hash(route_row->from, route_row->prefix, hashstr, sizeof(hashstr));
    hmap_insert(&vrf->all_routes, &route->node, hash_string(hashstr, 0));

    VLOG_DBG("Cache add route %s/%s",
            route->from ? route->from : "", route->prefix ? route->prefix : "");
}

static void
vrf_route_modify(struct vrf *vrf, struct route *route,
                 const struct ovsrec_route *route_row)
{
    int i;
    char *nh_hash_str;
    struct nexthop *nh, *next;
    struct shash_node *shash_idl_nh;
    struct shash current_idl_nhs;   /* NHs in IDL for this route */
    const struct ovsrec_nexthop *nh_row;
    struct ofproto_route ofp_route;

    /* Look for added/deleted NHs in the route. Don't consider
     * modified NHs because the fields in NH we are interested in
     * (ip address, port) are not mutable in db.
     */

    /* collect current selected NHs in idl */
    shash_init(&current_idl_nhs);
    for (i = 0; i < route_row->n_nexthops; i++) {
        nh_row = route_row->nexthops[i];
        /* valid IP or valid port. consider only one port for now */
        if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address ||
           ((nh_row->n_ports > 0) && nh_row->ports[0]))) {
            nh_hash_str = nh_row->ip_address ? nh_row->ip_address :
                          nh_row->ports[0]->name;
            if (!shash_add_once(&current_idl_nhs, nh_hash_str, nh_row)) {
                VLOG_DBG("nh %s specified twice", nh_hash_str);
            }
        }
    }

    if (VLOG_IS_DBG_ENABLED()) {
        SHASH_FOR_EACH(shash_idl_nh, &current_idl_nhs) {
            nh_row = shash_idl_nh->data;
            VLOG_DBG("DB Route %s/%s, nh_row %s", route->from, route->prefix,
                     nh_row->ip_address);
        }

        HMAP_FOR_EACH_SAFE(nh, next, node, &route->nexthops) {
            VLOG_DBG("Cached Route %s/%s, nh %s", route->from, route->prefix,
                     nh->ip_addr);
        }
    }

    ofp_route.n_nexthops = 0;
    /* delete nexthops that got deleted from db */
    HMAP_FOR_EACH_SAFE(nh, next, node, &route->nexthops) {
        nh_hash_str = nh->ip_addr ? nh->ip_addr : nh->port_name;
        nh_row = shash_find_data(&current_idl_nhs, nh_hash_str);
        if (!nh_row) {
            vrf_ofproto_set_nh(vrf, &ofp_route.nexthops[ofp_route.n_nexthops],
                               nh);
            if (vrf_nexthop_delete(vrf, route, nh) == 0) {
                ofp_route.n_nexthops++;
            }
        }
    }
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_delete(vrf, &ofp_route, route, false);
    }

    ofp_route.n_nexthops = 0;
    /* add new nexthops that got added in db */
    SHASH_FOR_EACH(shash_idl_nh, &current_idl_nhs) {
        nh_row = shash_idl_nh->data;
        nh = vrf_route_nexthop_lookup(route, nh_row->ip_address,
                nh_row->n_ports > 0 ? nh_row->ports[0]->name : NULL);
        if (!nh) {
            /* Add for asic only if NH is resolved and entry exists in
            **  Neighbor table - This will be ecmp case of more than 1 NH */
            if ((nh = vrf_nexthop_add(vrf, route, nh_row))) {
                if (route_row->n_nexthops > 1) {
                    vrf_ofproto_update_resolved_nh(vrf, &ofp_route, nh,
                                &ofp_route.nexthops[ofp_route.n_nexthops]);
                } else {
                    vrf_ofproto_set_nh(vrf,
                                    &ofp_route.nexthops[ofp_route.n_nexthops],
                                    nh);
                    ofp_route.n_nexthops++;
                }
            }
        }
    }
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_add(vrf, &ofp_route, route);
    }

    shash_destroy(&current_idl_nhs);
}

static void
vrf_reconfigure_ecmp(struct vrf *vrf)
{
    bool val = false;
    const struct ovsrec_system *ovs_row = ovsrec_system_first(idl);

    if (!ovs_row) {
        VLOG_ERR("Unable to access system table in db");
        return;
    }

    if (!OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_system_col_ecmp_config,
                                       idl_seqno)) {
        VLOG_DBG("ECMP column not modified in db");
        return;
    }

    val = smap_get_bool(&ovs_row->ecmp_config, SYSTEM_ECMP_CONFIG_STATUS,
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
    if (val != ecmp_config.enabled) {
        vrf_l3_ecmp_set(vrf, val);
        ecmp_config.enabled = val;
    }

    val = smap_get_bool(&ovs_row->ecmp_config,
                        SYSTEM_ECMP_CONFIG_HASH_SRC_IP,
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
    if (val != ecmp_config.src_ip_enabled) {
        vrf_l3_ecmp_hash_set(vrf, OFPROTO_ECMP_HASH_SRCIP, val);
        ecmp_config.src_ip_enabled = val;
    }
    val = smap_get_bool(&ovs_row->ecmp_config,
                        SYSTEM_ECMP_CONFIG_HASH_DST_IP,
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
    if (val != ecmp_config.dst_ip_enabled) {
        vrf_l3_ecmp_hash_set(vrf, OFPROTO_ECMP_HASH_DSTIP, val);
        ecmp_config.dst_ip_enabled = val;
    }
    val = smap_get_bool(&ovs_row->ecmp_config,
                        SYSTEM_ECMP_CONFIG_HASH_SRC_PORT,
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
    if (val != ecmp_config.src_port_enabled) {
        vrf_l3_ecmp_hash_set(vrf, OFPROTO_ECMP_HASH_SRCPORT, val);
        ecmp_config.src_port_enabled = val;
    }
    val = smap_get_bool(&ovs_row->ecmp_config,
                        SYSTEM_ECMP_CONFIG_HASH_DST_PORT,
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
    if (val != ecmp_config.dst_port_enabled) {
        vrf_l3_ecmp_hash_set(vrf, OFPROTO_ECMP_HASH_DSTPORT, val);
        ecmp_config.dst_port_enabled = val;
    }
    val = smap_get_bool(&ovs_row->ecmp_config,
                        //SYSTEM_ECMP_CONFIG_HASH_RESILIENT,
                        "resilient_hash_enabled",
                        SYSTEM_ECMP_CONFIG_ENABLE_DEFAULT);
        if (val != ecmp_config.resilient_hash_enabled) {
            vrf_l3_ecmp_hash_set(vrf, OFPROTO_ECMP_HASH_RESILIENT, val);
            ecmp_config.resilient_hash_enabled = val;
        }
}

/* For each route row in OVSDB, walk all the nexthops and
 * return TRUE if any nexthop is modified
 */
bool
is_route_nh_rows_modified (const struct ovsrec_route *route)
{
  const struct ovsrec_nexthop *nexthop = NULL;
  int index;

  if( !route) {
      return false;
  }

  for(index = 0; index < route->n_nexthops; index++) {
      nexthop = route->nexthops[index];
      if ((OVSREC_IDL_IS_ROW_MODIFIED(nexthop, idl_seqno)) &&
          !(OVSREC_IDL_IS_ROW_INSERTED(nexthop, idl_seqno))) {
          return true;
      }
  }

  return false;
}

void
vrf_reconfigure_routes(struct vrf *vrf)
{
    struct route *route, *next;
    struct shash current_idl_routes;
    struct shash_node *shash_route_row;
    char route_hash_str[VRF_ROUTE_HASH_MAXSIZE];
    const struct ovsrec_route *route_row = NULL, *route_row_local = NULL;

    vrf_reconfigure_ecmp(vrf);

    if (!vrf_has_l3_route_action(vrf)) {
        VLOG_DBG("No ofproto support for route management.");
        return;
    }

    route_row = ovsrec_route_first(idl);
    if (!route_row) {
        /* May be all routes got deleted, cleanup if any in this vrf hash */
        HMAP_FOR_EACH_SAFE (route, next, node, &vrf->all_routes) {
            vrf_route_delete(vrf, route);
        }
        return;
    }

    if ((!OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(route_row, idl_seqno)) &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_DELETED(route_row, idl_seqno))  &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(route_row, idl_seqno)) ) {
        return;
    }

    /* Collect all selected routes of this vrf */
    shash_init(&current_idl_routes);
    OVSREC_ROUTE_FOR_EACH(route_row, idl) {
        if (vrf_is_route_row_selected(route_row) &&
            strcmp(vrf->cfg->name, route_row->vrf->name) == 0) {
            vrf_route_hash(route_row->from, route_row->prefix,
                           route_hash_str, sizeof(route_hash_str));
            if (!shash_add_once(&current_idl_routes, route_hash_str,
                                route_row)) {
                VLOG_DBG("route %s specified twice", route_hash_str);
            }
        }
    }

    /* dump db and local cache */
    if (VLOG_IS_DBG_ENABLED()) {
        SHASH_FOR_EACH(shash_route_row, &current_idl_routes) {
            route_row_local = shash_route_row->data;
            VLOG_DBG("route in db '%s/%s'", route_row_local->from,
                                           route_row_local->prefix);
        }
        HMAP_FOR_EACH_SAFE(route, next, node, &vrf->all_routes) {
            VLOG_DBG("route in cache '%s/%s'", route->from, route->prefix);
        }
    }

    route_row = ovsrec_route_first(idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_DELETED(route_row, idl_seqno)) {
        /* Delete the routes that are deleted from the db */
        HMAP_FOR_EACH_SAFE(route, next, node, &vrf->all_routes) {
            vrf_route_hash(route->from, route->prefix,
                           route_hash_str, sizeof(route_hash_str));
            route_row_local = shash_find_data(&current_idl_routes,
                                              route_hash_str);
            if (!route_row_local) {
                vrf_route_delete(vrf, route);
            }
        }
    }

    if (OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(route_row, idl_seqno)) {
        /* Add new routes. We have the routes of interest in current_idl_routes */
        SHASH_FOR_EACH(shash_route_row, &current_idl_routes) {
            route_row_local = shash_route_row->data;
            route = vrf_route_hash_lookup(vrf, route_row_local);
            if (!route) {
                vrf_route_add(vrf, route_row_local);
            }
        }
    }

    /* Look for any modification of this route */
    if (OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(route_row, idl_seqno)) {
        OVSREC_ROUTE_FOR_EACH(route_row, idl) {
            if ((strcmp(vrf->cfg->name, route_row->vrf->name) == 0) &&
                (OVSREC_IDL_IS_ROW_MODIFIED(route_row, idl_seqno)) &&
                !(OVSREC_IDL_IS_ROW_INSERTED(route_row, idl_seqno))) {

               route = vrf_route_hash_lookup(vrf, route_row);
               if (vrf_is_route_row_selected(route_row)) {
                    if (route) {
                        vrf_route_modify(vrf, route, route_row);
                    } else {
                        /* maybe the route was unselected earlier and got
                         * selected now. it wouldn't be in our cache */
                        vrf_route_add(vrf, route_row);
                    }
                } else {
                    if (route) { /* route got unselected, delete from cache */
                        vrf_route_delete(vrf, route);
                    }
                }

            }
        }
    }
    shash_destroy(&current_idl_routes);

    /* dump our cache */
    if (VLOG_IS_DBG_ENABLED()) {
        struct nexthop *nh = NULL, *next_nh = NULL;
        HMAP_FOR_EACH_SAFE(route, next, node, &vrf->all_routes) {
            VLOG_DBG("Route : %s/%s", route->from, route->prefix);
            HMAP_FOR_EACH_SAFE(nh, next_nh, node, &route->nexthops) {
                VLOG_DBG("  NH : '%s/%s' ",
                         nh->ip_addr ? nh->ip_addr : "",
                         nh->port_name ? nh->port_name : "");
            }
        }
        HMAP_FOR_EACH_SAFE(nh, next_nh, vrf_node, &vrf->all_nexthops) {
            VLOG_DBG("VRF NH : '%s' -> Route '%s/%s'",
                    nh->ip_addr ? nh->ip_addr : "",
                    nh->route->from, nh->route->prefix);
        }
    }
    /* FIXME : for port deletion, delete all routes in ofproto that has
     * NH as the deleted port. */
    /* FIXME : for VRF deletion, delete all routes in ofproto that has
     * NH as any of the ports in the deleted VRF */
}

/* this function vrf_reconfigure_nexthops handles change in nexthop table
 * After that traverse the route table and look for modification of nexthop
 * for that particular route and modify the route accordingly
 * vrf_reconfigure_route will handle all route level insertions and deletions
 * of nexthops and thereby elimanting duplicate processing.
 */
void
vrf_reconfigure_nexthops(struct vrf *vrf)
{
    struct route *route;
    const struct ovsrec_route  *route_row = NULL;
    const struct ovsrec_nexthop *nexthop_row = NULL;

    nexthop_row = ovsrec_nexthop_first(idl);
    if (!nexthop_row) {
        VLOG_DBG("Nexthop table is NULL");
        return;
    }

    /* looking for any modification in  the nexthop table
     * generally checks if a nexthop has been changed from selected to unselected
     */

    if ((OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(nexthop_row, idl_seqno))) {
        OVSREC_ROUTE_FOR_EACH (route_row, idl) {
            if (route_row->n_nexthops > 0) {
                /* Check if any next hops are modified for that route */
                if (is_route_nh_rows_modified(route_row)) {
                    route = vrf_route_hash_lookup(vrf, route_row);
                    if (route) {
                        /* route is modified as one of the nexthops
                         * has been modified
                         */
                        vrf_route_modify(vrf, route, route_row);
                    }
                }
            }
        }
    }
}
/*
** Function to handle add/delete/modify of port ipv4/v6 address.
*/
void
vrf_port_reconfig_ipaddr(struct port *port,
                         struct ofproto_bundle_settings *bundle_setting)
{
    const struct ovsrec_port *idl_port = port->cfg;

    /* If primary ipv4 got changed */
    bundle_setting->ip_change = 0;
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip4_address,
                                      idl_seqno) ) {
        VLOG_DBG("ip4_address modified");
        bundle_setting->ip_change |= PORT_PRIMARY_IPv4_CHANGED;
        bundle_setting->ip4_address = idl_port->ip4_address;
    }

    /* If primary ipv6 got changed */
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip6_address,
                                      idl_seqno) ) {
        VLOG_DBG("ip6_address modified");
        bundle_setting->ip_change |= PORT_PRIMARY_IPv6_CHANGED;
        bundle_setting->ip6_address = idl_port->ip6_address;
    }
    /*
     * Configure secondary network addresses
     */
    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip4_address_secondary,
                                      idl_seqno) ) {
        VLOG_DBG("ip4_address_secondary modified");
        bundle_setting->ip_change |= PORT_SECONDARY_IPv4_CHANGED;
        bundle_setting->n_ip4_address_secondary =
                                      idl_port->n_ip4_address_secondary;
        bundle_setting->ip4_address_secondary =
                                      idl_port->ip4_address_secondary;
    }

    if (OVSREC_IDL_IS_COLUMN_MODIFIED(ovsrec_port_col_ip6_address_secondary,
                                      idl_seqno) ) {
        VLOG_DBG("ip6_address_secondary modified");
        bundle_setting->ip_change |= PORT_SECONDARY_IPv6_CHANGED;
        bundle_setting->n_ip6_address_secondary =
                                      idl_port->n_ip6_address_secondary;
        bundle_setting->ip6_address_secondary =
                                      idl_port->ip6_address_secondary;
    }
}
/* FIXME : move vrf functions from bridge.c to this file */
/* FIXME : move neighbor functions from bridge.c to this file */
