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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ether.h>
#include "bridge.h"
#include "vrf.h"
#include "hash.h"
#include "shash.h"
#include "ofproto/ofproto.h"
#include "ofproto/ofproto-provider.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"
#include "switchd.h"
#include "timeval.h"

VLOG_DEFINE_THIS_MODULE(vrf);

#define NEIGHBOR_HIT_BIT_UPDATE_INTERVAL   10000

/* Each time this timer expires, go through Neighbor table and query th
** ASIC for data-path hit-bit for each and update DB. */
static int neighbor_timer_interval;
static long long int neighbor_timer = LLONG_MIN;

static void neighbor_modify(struct neighbor *neighbor,
                const struct ovsrec_neighbor *idl_neighbor);
static void neighbor_delete(struct vrf *vrf, struct neighbor *neighbor);
static void neighbor_create(struct vrf *vrf,
                const struct ovsrec_neighbor *idl_neighbor);

/* Even though VRF is a separate entity from a user and schema
 * perspective, it's essentially very similar to bridge. It has ports,
 * bundles, mirros, might provide sFlow, NetFLow etc.
 *
 * In the future, it may also provide OpenFlow datapath, with OFP_NORMAL
 * falling back to the regular routing. Current code makes basic preparation
 * for this option by establising ofproto, and managing ports through it,
 * but not taking care of Openflow configuration itself. The use of ofproto
 * also allows ofproto providers to share common port/bundle/mirrors/etc
 * code more easily.
 *
 * VRFs also have quite a few principal differences like routes, neightbours,
 * routing protocols and not having VLANs.
 * In order to reuse as much of Bridge code as possible, struct vrf
 * "inherits" struct bridge. While configuration of VRF has to read
 * from a different table, port_configure, mirror_configure and may
 * other functions would be shared with the bridge. */
/* All vrfs, indexed by name. */
struct hmap all_vrfs = HMAP_INITIALIZER(&all_vrfs);

/* global ecmp config (not per VRF) - default values set here */
struct ecmp ecmp_config = {true, true, true, true, true, true};

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
bool
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
bool
vrf_is_route_row_selected(const struct ovsrec_route *route_row)
{
    if (route_row->selected && route_row->selected[0]) {
        /* configured and value set as true */
        return true;
    }
    return false;
}

void
vrf_route_hash(char *from, char *prefix, char *hashstr, int hashlen)
{
    snprintf(hashstr, hashlen, "%s:%s", from, prefix);
}

char *
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
struct nexthop *
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
void
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
            nh = vrf_route_nexthop_lookup(route, ofp_route->nexthops[i].id, NULL);
        } else {
            nh = vrf_route_nexthop_lookup(route, NULL, ofp_route->nexthops[i].id);
        }
        if (nh && nh->idl_row) {
            struct smap nexthop_error;
            const char *error = smap_get(&nh->idl_row->status,
                                         OVSDB_NEXTHOP_STATUS_ERROR);

            if (ofp_route->nexthops[i].rc != 0) { /* ofproto error */
                smap_init(&nexthop_error);
                smap_add(&nexthop_error, OVSDB_NEXTHOP_STATUS_ERROR,
                         ofp_route->nexthops[i].err_str);
                VLOG_DBG("Update error status with '%s'",
                                            ofp_route->nexthops[i].err_str);
                ovsrec_nexthop_set_status(nh->idl_row, &nexthop_error);
                smap_destroy(&nexthop_error);
            } else { /* ofproto success */
                if (error) { /* some error was already set in db, clear it */
                    VLOG_DBG("Clear error status");
                    ovsrec_nexthop_set_status(nh->idl_row, NULL);
                }
            }
        }
        free(ofp_route->nexthops[i].id);
    }

}

/* call ofproto API to delete this route and nexthops */
void
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
            ofp_route.nexthops[0].state =
                        resolved ? OFPROTO_NH_RESOLVED : OFPROTO_NH_UNRESOLVED;
            if (resolved) {
                ofp_route.nexthops[0].l3_egress_id = neighbor->l3_egress_id;
            }
            ofp_route.nexthops[0].rc = 0;
            ofp_route.nexthops[0].type = OFPROTO_NH_IPADDR;
            ofp_route.nexthops[0].id = xstrdup(nh->ip_addr);
            ofp_route.n_nexthops = 1;
            vrf_ofproto_route_add(vrf, &ofp_route, nh->route);
        }
    }
}

/* populate the ofproto nexthop entry with information from the nh */
void
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
        if (neighbor) {
            ofp_nh->state = OFPROTO_NH_RESOLVED;
            ofp_nh->l3_egress_id = neighbor->l3_egress_id;
        } else {
            ofp_nh->state = OFPROTO_NH_UNRESOLVED;
        }
        ofp_nh->id = xstrdup(nh->ip_addr);
        VLOG_DBG("%s : nexthop IP : (%s), neighbor %s found", __func__,
                nh->ip_addr, neighbor ? "" : "not");
    }
}


/* Delete the nexthop from the route entry in the local cache */
int
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
struct nexthop *
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
    nh->idl_row = (struct ovsrec_nexthop *)nh_row;

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
struct route *
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
void
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
void
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
    for (i = 0; i < route_row->n_nexthops; i++) {
        nh_row = route_row->nexthops[i];
        /* valid IP or valid port. consider only one port for now */
        if (vrf_is_nh_row_selected(nh_row) && (nh_row->ip_address ||
           ((nh_row->n_ports > 0) && nh_row->ports[0]))) {
            if ((nh = vrf_nexthop_add(vrf, route, nh_row))) {
                vrf_ofproto_set_nh(vrf, &ofp_route.nexthops[ofp_route.n_nexthops],
                                   nh);
                ofp_route.n_nexthops++;
            }
        }
    }
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_add(vrf, &ofp_route, route);
    }

    route->vrf = vrf;
    route->idl_row = route_row;

    vrf_route_hash(route_row->from, route_row->prefix, hashstr, sizeof(hashstr));
    hmap_insert(&vrf->all_routes, &route->node, hash_string(hashstr, 0));

    VLOG_DBG("Cache add route %s/%s",
            route->from ? route->from : "", route->prefix ? route->prefix : "");
}

void
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
    SHASH_FOR_EACH(shash_idl_nh, &current_idl_nhs) {
        nh_row = shash_idl_nh->data;
        VLOG_DBG("DB Route %s/%s, nh_row %s", route->from, route->prefix,
                        nh_row->ip_address);
    }
    HMAP_FOR_EACH_SAFE(nh, next, node, &route->nexthops) {
        VLOG_DBG("Cached Route %s/%s, nh %s", route->from, route->prefix,
                    nh->ip_addr);
    }

    ofp_route.n_nexthops = 0;
    /* delete nexthops that got deleted from db */
    HMAP_FOR_EACH_SAFE(nh, next, node, &route->nexthops) {
        nh_hash_str = nh->ip_addr ? nh->ip_addr : nh->port_name;
        nh->idl_row = shash_find_data(&current_idl_nhs, nh_hash_str);
        if (!nh->idl_row) {
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
            if ((nh = vrf_nexthop_add(vrf, route, nh_row))) {
                vrf_ofproto_set_nh(vrf, &ofp_route.nexthops[ofp_route.n_nexthops],
                                   nh);
                ofp_route.n_nexthops++;
            }
        }
    }
    if (ofp_route.n_nexthops > 0) {
        vrf_ofproto_route_add(vrf, &ofp_route, route);
    }

    shash_destroy(&current_idl_nhs);
}

void
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
    SHASH_FOR_EACH(shash_route_row, &current_idl_routes) {
        route_row_local = shash_route_row->data;
        VLOG_DBG("route in db '%s/%s'", route_row_local->from,
                                        route_row_local->prefix);
    }
    HMAP_FOR_EACH_SAFE(route, next, node, &vrf->all_routes) {
        VLOG_DBG("route in cache '%s/%s'", route->from, route->prefix);
    }

    route_row = ovsrec_route_first(idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_DELETED(route_row, idl_seqno)) {
        /* Delete the routes that are deleted from the db */
        HMAP_FOR_EACH_SAFE(route, next, node, &vrf->all_routes) {
            vrf_route_hash(route->from, route->prefix,
                           route_hash_str, sizeof(route_hash_str));
            route->idl_row = shash_find_data(&current_idl_routes, route_hash_str);
            if (!route->idl_row) {
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

/* FIXME : move neighbor functions from bridge.c to this file */

/*
** Function to add neighbors of given vrf and program in ofproto/asic
*/
void
vrf_add_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor;
    const struct ovsrec_neighbor *idl_neighbor;

    idl_neighbor = ovsrec_neighbor_first(idl);
    if (idl_neighbor == NULL)
    {
        VLOG_DBG("No rows in Neighbor table");
        return;
    }

    /* Add neighbors of this vrf */
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
       if (strcmp(vrf->cfg->name, idl_neighbor->vrf->name) == 0 ) {
           neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
           if (!neighbor && idl_neighbor->port) {
               neighbor_create(vrf, idl_neighbor);
           }
       }
    }

} /* vrf_add_neighbors */

/* Function to delete all neighbors of an vrf, when that vrf is deleted */
void
vrf_delete_all_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor, *next;

    /* Delete all neighbors of this vrf */
    HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
        if (neighbor) {
            neighbor_delete(vrf, neighbor);
        }
    }

} /* vrf_delete_all_neighbors */

/* Function to to delete the neighbors which are referencing the deleted vrf port */
void
vrf_delete_port_neighbors(struct vrf *vrf, struct port *port)
{
    struct neighbor *neighbor, *next;

    /* Delete the neighbors which are referencing the deleted vrf port */
    HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
        if ( (neighbor) &&
             (strcmp(neighbor->port_name, port->name) == 0) ) {
            neighbor_delete(vrf, neighbor);
        }
    }

} /* vrf_delete_port_neighbors */

/*
** Function to handle independent addition/deletion/modifications to
** neighbor table.  */
void
vrf_reconfigure_neighbors(struct vrf *vrf)
{
    struct neighbor *neighbor, *next;
    struct shash current_idl_neigbors;
    const struct ovsrec_neighbor *idl_neighbor;

    idl_neighbor = ovsrec_neighbor_first(idl);
    if (idl_neighbor == NULL)
    {
        VLOG_DBG("No rows in Neighbor table, delete if any in our hash");

        /* May be all neighbors got delete, cleanup if any in this vrf hash */
        HMAP_FOR_EACH_SAFE (neighbor, next, node, &vrf->all_neighbors) {
            if (neighbor) {
                neighbor_delete(vrf, neighbor);
            }
        }

        return;
    }

    if ( (!OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(idl_neighbor, idl_seqno)) &&
       (!OVSREC_IDL_ANY_TABLE_ROWS_DELETED(idl_neighbor, idl_seqno))  &&
       (!OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(idl_neighbor, idl_seqno)) )
    {
        VLOG_DBG("No modification in Neighbor table");
        return;
    }

    /* Collect all neighbors of this vrf */
    shash_init(&current_idl_neigbors);
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

        /* add only neighbors of this vrf */
        if (strcmp(vrf->cfg->name, idl_neighbor->vrf->name) == 0 ) {
            if (!shash_add_once(&current_idl_neigbors, idl_neighbor->ip_address,
                                idl_neighbor)) {
                VLOG_DBG("neighbor %s specified twice",
                          idl_neighbor->ip_address);
                VLOG_WARN_RL(&rl, "neighbor %s specified twice",
                             idl_neighbor->ip_address);
            }
        }
    }

    /* Delete the neighbors' that are deleted from the db */
    VLOG_DBG("Deleting which are no more in idl");
    HMAP_FOR_EACH_SAFE(neighbor, next, node, &vrf->all_neighbors) {
        neighbor->cfg = shash_find_data(&current_idl_neigbors,
                                        neighbor->ip_address);
        if (!neighbor->cfg) {
            neighbor_delete(vrf, neighbor);
        }
    }

    /* Add new neighbors. */
    VLOG_DBG("Adding newly added idl neighbors");
    OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
        neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
        if (!neighbor && idl_neighbor->port) {
            neighbor_create(vrf, idl_neighbor);
        }
    }

    /* Look for any modification of mac/port of this vrf neighbors */
    VLOG_DBG("Looking for any modified neighbors, mac, etc");
    idl_neighbor = ovsrec_neighbor_first(idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(idl_neighbor, idl_seqno)) {
        OVSREC_NEIGHBOR_FOR_EACH(idl_neighbor, idl) {
           if ( (OVSREC_IDL_IS_ROW_MODIFIED(idl_neighbor, idl_seqno)) &&
               !(OVSREC_IDL_IS_ROW_INSERTED(idl_neighbor, idl_seqno)) ) {

                VLOG_DBG("Some modifications in Neigbor %s",
                                      idl_neighbor->ip_address);

                neighbor = neighbor_hash_lookup(vrf, idl_neighbor->ip_address);
                if (neighbor) {
                    if(idl_neighbor->port)
                        neighbor_modify(neighbor, idl_neighbor);
                    else
                        neighbor_delete(vrf, neighbor);
                }
            }
        }
    }

    shash_destroy(&current_idl_neigbors);

} /* add_reconfigure_neighbors */

void
add_del_vrfs(const struct ovsrec_open_vswitch *cfg)
{
    struct vrf *vrf, *next;
    struct shash new_vrf;
    size_t i;

    /* Collect new vrfs' names */
    shash_init(&new_vrf);
    for (i = 0; i < cfg->n_vrfs; i++) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];

        if (strchr(vrf_cfg->name, '/')) {
            /* Prevent remote ovsdb-server users from accessing arbitrary
             * directories, e.g. consider a vrf named "../../../etc/". */
            VLOG_WARN_RL(&rl, "ignoring vrf with invalid name \"%s\"",
                         vrf_cfg->name);
        } else if (!shash_add_once(&new_vrf, vrf_cfg->name, vrf_cfg)) {
            VLOG_WARN_RL(&rl, "vrf %s specified twice", vrf_cfg->name);
        }
    }

    /* Get rid of deleted vrfs
     * Update 'cfg' of vrfs that still exist. */
    HMAP_FOR_EACH_SAFE (vrf, next, node, &all_vrfs) {
        vrf->cfg = shash_find_data(&new_vrf, vrf->up->name);
        if (!vrf->cfg) {
            vrf_destroy(vrf);
        }
    }

    /* Add new vrfs. */
    for (i = 0; i < cfg->n_vrfs; i++) {
        const struct ovsrec_vrf *vrf_cfg = cfg->vrfs[i];
        struct vrf *vrf = vrf_lookup(vrf_cfg->name);
        if (!vrf) {
            vrf_create(vrf_cfg);
        }
    }

    shash_destroy(&new_vrf);
}

void
vrf_create(const struct ovsrec_vrf *vrf_cfg)
{
    struct vrf *vrf;
    const struct ovsrec_open_vswitch *ovs = ovsrec_open_vswitch_first(idl);

    ovs_assert(!vrf_lookup(vrf_cfg->name));
    vrf = xzalloc(sizeof *vrf);

    vrf->up = xzalloc(sizeof(*vrf->up));
    vrf->up->name = xstrdup(vrf_cfg->name);
    vrf->up->type = xstrdup("vrf");
    vrf->cfg = vrf_cfg;

    /* Use system mac as default mac */
    memcpy(&vrf->up->default_ea, ether_aton(ovs->system_mac), ETH_ADDR_LEN);

    hmap_init(&vrf->up->ports);
    hmap_init(&vrf->up->ifaces);
    hmap_init(&vrf->up->iface_by_name);
    hmap_init(&vrf->all_neighbors);
    hmap_init(&vrf->all_routes);
    hmap_init(&vrf->all_nexthops);
    hmap_insert(&all_vrfs, &vrf->node, hash_string(vrf->up->name, 0));
}

void
vrf_destroy(struct vrf *vrf)
{
    if (vrf) {
        struct port *port, *next_port;

        /* Delete any neighbors, etc of this vrf */
        vrf_delete_all_neighbors(vrf);

        HMAP_FOR_EACH_SAFE (port, next_port, hmap_node, &vrf->up->ports) {
            port_destroy(port);
        }

        hmap_remove(&all_vrfs, &vrf->node);
        ofproto_destroy(vrf->up->ofproto);
        hmap_destroy(&vrf->up->ifaces);
        hmap_destroy(&vrf->up->ports);
        hmap_destroy(&vrf->up->iface_by_name);
        hmap_destroy(&vrf->all_neighbors);
        hmap_destroy(&vrf->all_routes);
        hmap_destroy(&vrf->all_nexthops);
        free(vrf->up->name);
        free(vrf->up);
        free(vrf);
    }
}

void
vrf_collect_wanted_ports(struct vrf *vrf,
                         struct shash *wanted_ports)
{
    size_t i;

    shash_init(wanted_ports);

    for (i = 0; i < vrf->cfg->n_ports; i++) {
        const char *name = vrf->cfg->ports[i]->name;
        if (!shash_add_once(wanted_ports, name, vrf->cfg->ports[i])) {
            VLOG_WARN("bridge %s: %s specified twice as bridge port",
                      vrf->up->name, name);
        }
    }
}

struct vrf *
vrf_lookup(const char *name)
{
    struct vrf *vrf;

    HMAP_FOR_EACH_WITH_HASH (vrf, node, hash_string(name, 0), &all_vrfs) {
        if (!strcmp(vrf->up->name, name)) {
            return vrf;
        }
    }
    return NULL;
}

void
vrf_del_ports(struct vrf *vrf, const struct shash *wanted_ports)
{
    struct shash_node *port_node;
    struct port *port, *next;

    /* Get rid of deleted ports.
     * Get rid of deleted interfaces on ports that still exist. */
    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &vrf->up->ports) {
        port->cfg = shash_find_data(wanted_ports, port->name);
        if (!port->cfg) {
            /* Delete the neighbors referring the deleted vrf ports */
            vrf_delete_port_neighbors(vrf, port);
            port_destroy(port);
        } else {
            port_del_ifaces(port);
        }
    }

    /* Update iface->cfg and iface->type in interfaces that still exist. */
    SHASH_FOR_EACH (port_node, wanted_ports) {
        const struct ovsrec_port *port = port_node->data;
        size_t i;

        for (i = 0; i < port->n_interfaces; i++) {
            const struct ovsrec_interface *cfg = port->interfaces[i];
            struct iface *iface = iface_lookup(vrf->up, cfg->name);
            const char *type = iface_get_type(cfg, NULL);

            if (iface) {
                iface->cfg = cfg;
                iface->type = type;
            } else if (!strcmp(type, "null")) {
                VLOG_WARN_ONCE("%s: The null interface type is deprecated and"
                               " may be removed in February 2013. Please email"
                               " dev@openvswitch.org with concerns.",
                               cfg->name);
            } else {
                /* We will add new interfaces later. */
            }
        }
    }
}

int
vrf_l3_route_action(struct vrf *vrf, enum ofproto_route_action action,
                    struct ofproto_route *route)
{
    return ofproto_l3_route_action(vrf->up->ofproto, action, route);
}

bool
vrf_has_l3_route_action(struct vrf *vrf)
{
    return vrf->up->ofproto->ofproto_class->l3_route_action ? true : false;
}

int
vrf_l3_ecmp_set(struct vrf *vrf, bool enable)
{
    return ofproto_l3_ecmp_set(vrf->up->ofproto, enable);
}

int
vrf_l3_ecmp_hash_set(struct vrf *vrf, unsigned int hash, bool enable)
{
    return ofproto_l3_ecmp_hash_set(vrf->up->ofproto, hash, enable);
}

/* Function to create new neighbor hash entry and configure asic */
static void
neighbor_create(struct vrf *vrf,
                const struct ovsrec_neighbor *idl_neighbor)
{
    struct neighbor *neighbor;

    VLOG_DBG("In neighbor_create for neighbor %s",
              idl_neighbor->ip_address);
    ovs_assert(!neighbor_hash_lookup(vrf, idl_neighbor->ip_address));

    neighbor = xzalloc(sizeof *neighbor);
    neighbor->ip_address = xstrdup(idl_neighbor->ip_address);
    neighbor->mac = xstrdup(idl_neighbor->mac);

    if (strcmp(idl_neighbor->address_family,
                             OVSREC_NEIGHBOR_ADDRESS_FAMILY_IPV6) == 0) {
        neighbor->is_ipv6_addr = true;
    }
    neighbor->port_name = xstrdup(idl_neighbor->port->name);
    neighbor->cfg = idl_neighbor;
    neighbor->vrf = vrf;
    neighbor->l3_egress_id = -1;

    hmap_insert(&vrf->all_neighbors, &neighbor->node,
                hash_string(neighbor->ip_address, 0));

    /* Add ofproto/asic neighbors */
    neighbor_set_l3_host_entry(vrf, neighbor);
    vrf_ofproto_update_route_with_neighbor(vrf, neighbor, true);
}

/* Function to delete neighbor in hash and also from ofproto/asic */
static void
neighbor_delete(struct vrf *vrf, struct neighbor *neighbor)
{
    VLOG_DBG("In neighbor_delete for neighbor %s", neighbor->ip_address);
    if (neighbor) {

        /* Update routes before deleting the l3 host entry */
        vrf_ofproto_update_route_with_neighbor(vrf, neighbor, false);
        /* Delete from ofproto/asic */
        neighbor_delete_l3_host_entry(vrf, neighbor);

        /* Delete from hash */
        neighbor_hash_delete(vrf, neighbor);
    }
}

/* Function to handle modifications to neighbor entry and configure asic */
static void
neighbor_modify(struct neighbor *neighbor,
                const struct ovsrec_neighbor *idl_neighbor)
{
    VLOG_DBG("In neighbor_modify for neighbor %s",
              idl_neighbor->ip_address);

    /* TODO: Get status, if failed or incomplete delete the entry */
    /* OPENSWITCH_TODO : instead of delete/add, reprogram the entry in ofproto */

    if ( (strcmp(neighbor->port_name, idl_neighbor->port->name) != 0) ||
        (strcmp(neighbor->mac, idl_neighbor->mac) != 0 ) ) {
        struct ether_addr *ether_mac = NULL;

        /* Delete earlier egress/host entry */
        neighbor_delete_l3_host_entry(neighbor->vrf, neighbor);

        /* Update and add new one */
        free(neighbor->port_name);
        free(neighbor->mac);
        neighbor->mac = xstrdup(idl_neighbor->mac);
        neighbor->port_name = xstrdup(idl_neighbor->port->name);

        /* Configure provider/asic only if valid mac */
        ether_mac = ether_aton(idl_neighbor->mac);
        if (ether_mac != NULL) {
            neighbor_set_l3_host_entry(neighbor->vrf, neighbor);
        }
        /* entry stays in hash, and on modification add to asic */
    }
} /* neighbor_modify */

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
neighbor_update(void)
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
