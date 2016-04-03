/*
 * Copyright (c) 2016 Hewlett-Packard Enterprise Development, LP
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


#include <stdlib.h>
#include <errno.h>
#include "list.h"
#include "openvswitch/vlog.h"
#include "vswitch-idl.h"
#include "stats-blocks.h"
#include "stats-copp-plugin.h"
#include "plugin-extensions.h"
#include "copp-asic-provider.h"
#include "vswitch-idl.h"


VLOG_DEFINE_THIS_MODULE(stats_copp_plugin);

/* Globals */
struct plugin_extension_interface g_copp_asic_plugin;
bool g_copp_initialized;

void copp_stats_per_bridge_cb(void * arg);




void
copp_stats_init(void) {
    int rc;
    struct plugin_extension_interface* asic_intf;

    /* find the previously registered asic copp plugin */
    g_copp_initialized = false;
    rc = find_plugin_extension(COPP_ASIC_PLUGIN_INTERFACE_NAME,
        COPP_ASIC_PLUGIN_INTERFACE_MAJOR,
        COPP_ASIC_PLUGIN_INTERFACE_MINOR,
        &asic_intf);

    if (!rc) {
        g_copp_asic_plugin.plugin_interface = asic_intf->plugin_interface;
        g_copp_initialized = true;
    }
    else {
        /* log something */
    }

    /* FIXME: also register for STATS_PER_PORT and STATS_PER_IFACE and STATS_PER_VRF */
    register_stats_callback(copp_stats_per_bridge_cb, STATS_PER_BRIDGE, 0);

    VLOG_INFO("copp stats callback copp_stats_per_bridge_cb() registered");

}




void copp_stats_per_bridge_cb(void * arg){

    struct ovsdb_idl *idl = (struct ovsdb_idl *)arg;
    int class, rc;
    struct copp_asic_plugin_interface* asic_intf = (struct copp_asic_plugin_interface*)g_copp_asic_plugin.plugin_interface;
    struct copp_protocol_stats  stats;
    struct copp_hw_status  hw_status;


    for (class=0; class<COPP_NUM_CLASSES; class++) {
        /* collect from asic */
        /* FIXME, use correct hw_asic_id */
        /* FIXME, Trimm this temporary logging after implementing the publish to DB parts */
        rc = asic_intf->copp_stats_get(0, class, &stats);
        VLOG_INFO("asic_intf->copp_stats_get returned %d with stats %lu, %lu, %lu, %lu",
            rc, stats.bytes_dropped, stats.bytes_passed, stats.packets_dropped, stats.packets_passed);
        rc = asic_intf->copp_hw_status_get(0, class, &hw_status);
        VLOG_INFO("asic_intf->copp_hw_status_get returned %d with stats %lu, %lu, %lu",
            rc, hw_status.burst, hw_status.local_priority, hw_status.rate);

        /* publish to db */
        /* TBD */
        /* FIXME, Implement me */
    }

}
