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

#include <stdint.h>
#include "openvswitch/vlog.h"
#include "ofproto/ofproto-provider.h"
#include "connectivity.h"
#include "seq.h"
#include "poll-loop.h"
#include "vrf.h"
#include "switchd.h"

VLOG_DEFINE_THIS_MODULE(status);

/* Track changes to port connectivity. */
static uint64_t connectivity_seqno = LLONG_MIN;

/* When the status update transaction returns 'TXN_INCOMPLETE', should
 * register a timeout in 'STATUS_CHECK_AGAIN_MSEC' to check again. */
#define STATUS_CHECK_AGAIN_MSEC 100

static void
br_refresh_datapath_info(struct bridge *br)
{
    const char *version;

    version = (br->ofproto && br->ofproto->ofproto_class->get_datapath_version
               ? br->ofproto->ofproto_class->get_datapath_version(br->ofproto)
               : NULL);

    ovsrec_bridge_set_datapath_version(br->cfg,
                                       version ? version : "<unknown>");
}

/* Update bridge/port/interface status if necessary. */
void
run_status_update(void)
{
    if (!status_txn) {
        uint64_t seq;

        /* Rate limit the update.  Do not start a new update if the
         * previous one is not done. */
        seq = seq_read(connectivity_seq_get());
        if (seq != connectivity_seqno || status_txn_try_again) {
            struct bridge *br;
            struct vrf *vrf;
            connectivity_seqno = seq;
            status_txn = ovsdb_idl_txn_create(idl);
            HMAP_FOR_EACH (br, node, &all_bridges) {
                struct port *port;

                br_refresh_datapath_info(br);
                HMAP_FOR_EACH (port, hmap_node, &br->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_netdev_status(iface,
                                                    status_txn_try_again);
                        iface_refresh_ofproto_status(iface);
                    }
                }
            }

            HMAP_FOR_EACH (vrf, node, &all_vrfs) {
                struct port *port;

                HMAP_FOR_EACH (port, hmap_node, &vrf->up->ports) {
                    struct iface *iface;

                    LIST_FOR_EACH (iface, port_elem, &port->ifaces) {
                        iface_refresh_netdev_status(iface,
                                                    status_txn_try_again);
                        iface_refresh_ofproto_status(iface);
                    }
                }
            }
        }
    }

    /* Commit the transaction and get the status. If the transaction finishes,
     * then destroy the transaction. Otherwise, keep it so that we can check
     * progress the next time that this function is called. */
    if (status_txn) {
        enum ovsdb_idl_txn_status status;

        status = ovsdb_idl_txn_commit(status_txn);
        if (status != TXN_INCOMPLETE) {
            ovsdb_idl_txn_destroy(status_txn);
            status_txn = NULL;

            /* Sets the 'status_txn_try_again' if the transaction fails. */
            if (status == TXN_SUCCESS || status == TXN_UNCHANGED) {
                status_txn_try_again = false;
            } else {
                status_txn_try_again = true;
            }
        }
    }
}

void
status_update_wait(void)
{
    /* This prevents the process from constantly waking up on
     * connectivity seq, when there is no connection to ovsdb. */
    if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    /* If the 'status_txn' is non-null (transaction incomplete), waits for the
     * transaction to complete.  If the status update to database needs to be
     * run again (transaction fails), registers a timeout in
     * 'STATUS_CHECK_AGAIN_MSEC'.  Otherwise, waits on the global connectivity
     * sequence number. */
    if (status_txn) {
        ovsdb_idl_txn_wait(status_txn);
    } else if (status_txn_try_again) {
        poll_timer_wait_until(time_msec() + STATUS_CHECK_AGAIN_MSEC);
    } else {
        seq_wait(connectivity_seq_get(), connectivity_seqno);
    }
}
