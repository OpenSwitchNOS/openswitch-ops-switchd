/* SFLOW CLI commands
 *
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * File: sflow_vty.c
 *
 * Purpose:  To add SFLOW CLI configuration and display commands.
 */
#include <inttypes.h>
#include <netdb.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <lib/version.h>
#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"
#include "ovsdb-idl.h"
#include "smap.h"
#include "sflow.h"
#include "sflow_vty.h"
#include "vswitch-idl.h"
#include "vtysh/vtysh.h"
#include "vtysh/vtysh_user.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/intf_vtysh_utils.h"

VLOG_DEFINE_THIS_MODULE(vtysh_sflow_cli);
extern struct ovsdb_idl *idl;

/*
 * Create sFlow entry for storing sFlow related configuration.
 */
const struct ovsrec_sflow*
sflow_insert(struct ovsdb_idl_txn *txn)
{
    const struct ovsrec_sflow *sflow_row;
    sflow_row = ovsrec_sflow_insert(txn);
    ovsrec_sflow_set_name(sflow_row, OVSDB_SFLOW_GLOBAL_ROW_NAME);
    return sflow_row;
}

/*
 * This function is used to aggregate the total number of ingress and egress
 * samples on all interfaces.
 */
static uint64_t
sflow_aggregate_sample_count(void)
{
    uint64_t total_sample_count = 0;
    uint64_t packet_count = 0;
    unsigned int index;

    const struct ovsrec_interface *ifrow;
    const struct ovsdb_datum *datum;
    union ovsdb_atom atom;

    OVSREC_INTERFACE_FOR_EACH (ifrow, idl)
    {
        datum = ovsrec_interface_get_statistics(ifrow, OVSDB_TYPE_STRING,
                                                OVSDB_TYPE_INTEGER);
        if (datum) {
            /* Fetch the number of sFlow ingress packets */
            atom.string = "sflow_ingress_packets";
            index = ovsdb_datum_find_key(datum, &atom, OVSDB_TYPE_STRING);
            packet_count =
                    (index == UINT_MAX) ? 0 : datum->values[index].integer;
            total_sample_count = total_sample_count + packet_count;

            /* Fetch the number of sFlow egress packets */
            atom.string = "sflow_egress_packets";
            index = ovsdb_datum_find_key(datum, &atom, OVSDB_TYPE_STRING);
            packet_count =
                    (index == UINT_MAX) ? 0 : datum->values[index].integer;
            total_sample_count = total_sample_count + packet_count;
        }
    }

    return total_sample_count;
}

/* This function displays the sflow configuration set in the sFlow table */
static int
sflow_show(void)
{
    const struct ovsrec_system *system_row;
    const struct ovsrec_sflow *sflow_row;
    size_t i = 0;
    char temp_ip[MAX_COLLECTOR_LENGTH];
    char *collector_ip, *collector_port, *collector_vrf;

    sflow_row = ovsrec_sflow_first(idl);
    if (!sflow_row) {
        vty_out(vty, "sFlow not yet configured.%s", VTY_NEWLINE);
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        return CMD_SUCCESS;
    }
    system_row = ovsrec_system_first(idl);
    if (!system_row) {
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        return CMD_SUCCESS;
    }

    vty_out(vty, "%ssFlow Configuration %s",
            VTY_NEWLINE, VTY_NEWLINE);
    vty_out(vty, "-----------------------------------------%s",
            VTY_NEWLINE);
    if (system_row->sflow != NULL) {
        vty_out(vty, "sFlow                         enabled%s",
                VTY_NEWLINE);
    } else {
        vty_out(vty, "sFlow                         disabled%s",
                VTY_NEWLINE);
    }

    if (sflow_row->n_targets > 0) {
        vty_out(vty, "Collector IP/Port/Vrf");
    } else {
        vty_out(vty, "Collector IP/Port/Vrf         Not set%s",
                VTY_NEWLINE);
    }

    for (i = 0; i < sflow_row->n_targets; i++) {
        memset(temp_ip, 0, sizeof(temp_ip));
        strncpy(temp_ip, sflow_row->targets[i], MAX_COLLECTOR_LENGTH);
        collector_ip = strtok(temp_ip, "/");
        collector_port = strtok(NULL, "/");
        collector_vrf = strtok(NULL, "\0");
        if (!collector_vrf && collector_port) {
            if (!atoi(collector_port)) {
                collector_vrf = collector_port;
                collector_port = NULL;
            }
        }

        /* We need to check for i = 0 because the first collector entry
           has to deal with the "Collector IP/Port/Vrf" string. */
        if (collector_ip && collector_port && collector_vrf) {
            if (i == 0) {
                vty_out(vty, "         %s%s", sflow_row->targets[i],
                        VTY_NEWLINE);
            } else {
                vty_out(vty, "                              %s%s",
                        sflow_row->targets[i], VTY_NEWLINE);
            }
        }
        if (collector_ip && collector_port && !collector_vrf) {
            if (i == 0) {
                vty_out(vty, "         %s/%s%s", sflow_row->targets[i],
                        DEFAULT_VRF_NAME, VTY_NEWLINE);
            } else {
                vty_out(vty, "                              %s/%s%s",
                        sflow_row->targets[i], DEFAULT_VRF_NAME, VTY_NEWLINE);
            }
        }
        if (collector_ip && !collector_port && collector_vrf) {
            if (i == 0) {
                vty_out(vty, "         %s/%d/%s%s", collector_ip,
                        SFL_DEFAULT_COLLECTOR_PORT, collector_vrf,
                        VTY_NEWLINE);
            } else {
                vty_out(vty, "                              %s/%d/%s%s",
                        collector_ip, SFL_DEFAULT_COLLECTOR_PORT, collector_vrf,
                        VTY_NEWLINE);
            }
        }
        if (collector_ip && !collector_port && !collector_vrf) {
            if (i == 0) {
                vty_out(vty, "         %s/%d/%s%s", sflow_row->targets[i],
                        SFL_DEFAULT_COLLECTOR_PORT,
                        DEFAULT_VRF_NAME, VTY_NEWLINE);
            } else {
                vty_out(vty, "                              %s/%d/%s%s",
                        sflow_row->targets[i], SFL_DEFAULT_COLLECTOR_PORT,
                        DEFAULT_VRF_NAME, VTY_NEWLINE);
            }
        }
    }

    if (sflow_row->agent != NULL) {
        vty_out(vty, "Agent Interface               %s%s", sflow_row->agent,
                VTY_NEWLINE);
    } else {
        vty_out(vty, "Agent Interface               Not set%s",
                VTY_NEWLINE);
    }

    if (sflow_row->agent_addr_family != NULL) {
        vty_out(vty, "Agent Address Family          %s%s",
                sflow_row->agent_addr_family, VTY_NEWLINE);
    } else {
        vty_out(vty, "Agent Address Family          ipv4%s",
                VTY_NEWLINE);
    }

    if (sflow_row->sampling != NULL) {
        vty_out(vty, "Sampling Rate                 %lld%s",
                *(sflow_row->sampling), VTY_NEWLINE);
    } else {
        vty_out(vty, "Sampling Rate                 %d%s",
                SFL_DEFAULT_SAMPLING_RATE, VTY_NEWLINE);
    }

    if (sflow_row->polling != NULL) {
        vty_out(vty, "Polling Interval              %lld%s",
                *(sflow_row->polling), VTY_NEWLINE);
    } else {
        vty_out(vty, "Polling Interval              %d%s",
                SFL_DEFAULT_POLLING_INTERVAL, VTY_NEWLINE);
    }

    if (sflow_row->header != NULL) {
        vty_out(vty, "Header Size                   %lld%s",
                *(sflow_row->header), VTY_NEWLINE);
    } else {
        vty_out(vty, "Header Size                   %d%s",
                SFL_DEFAULT_HEADER_SIZE, VTY_NEWLINE);
    }

    if (sflow_row->max_datagram != NULL) {
        vty_out(vty, "Max Datagram Size             %lld%s",
                *(sflow_row->max_datagram), VTY_NEWLINE);
    } else {
        vty_out(vty, "Max Datagram Size             %d%s",
                SFL_DEFAULT_DATAGRAM_SIZE, VTY_NEWLINE);
    }

    vty_out(vty, "Number of Samples             %"PRIu64"%s",
            sflow_aggregate_sample_count(), VTY_NEWLINE);

    return CMD_SUCCESS;
}

/* This function displays the interface sflow configuration set in the
 sFlow table.

 Sample Output:

 sFlow Configuration - Interface 3
 -----------------------------------------
 sFlow                         enabled
 Sampling Rate                 20
 Number of Samples             1
 */
static int
sflow_show_intf_statistics(const char *interface)
{
    const struct ovsrec_system *system_row;
    const struct ovsrec_sflow *sflow_row;
    const struct ovsrec_port *port_row;
    struct smap other_config = SMAP_INITIALIZER(&other_config);
    const char *value = NULL;
    bool int_found = false;
    uint64_t packet_count = 0;
    unsigned int index;

    const struct ovsrec_interface *ifrow;
    const struct ovsdb_datum *datum;
    union ovsdb_atom atom;

    sflow_row = ovsrec_sflow_first(idl);
    if (!sflow_row) {
        vty_out(vty, "sFlow not yet configured.%s", VTY_NEWLINE);
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        return CMD_SUCCESS;
    }

    system_row = ovsrec_system_first(idl);
    if (!system_row) {
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        return CMD_SUCCESS;
    }

    ifrow = interface_find(interface);

    if (!ifrow) {
        vty_out(vty, "Invalid interface.%s", VTY_NEWLINE);
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        return CMD_SUCCESS;
    }

    datum = ovsrec_interface_get_statistics(ifrow, OVSDB_TYPE_STRING,
                                            OVSDB_TYPE_INTEGER);

    /* Fetch the number of sFlow ingress packets */
    atom.string = "sflow_ingress_packets";
    index = ovsdb_datum_find_key(datum, &atom, OVSDB_TYPE_STRING);
    packet_count = (index == UINT_MAX) ? 0 : datum->values[index].integer;
    /* Fetch the number of sFlow egress packets */
    atom.string = "sflow_egress_packets";
    index = ovsdb_datum_find_key(datum, &atom, OVSDB_TYPE_STRING);
    packet_count += (index == UINT_MAX) ? 0 : datum->values[index].integer;

    vty_out(vty, "%ssFlow Configuration - Interface %s%s",
            VTY_NEWLINE, interface, VTY_NEWLINE);
    vty_out(vty, "-----------------------------------------%s",
            VTY_NEWLINE);

    port_row = port_find(interface);

    if (port_row != NULL) {
        value = smap_get(&port_row->other_config,
        PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_KEY_STR);
        if (value
                && (strcmp(value,
                        PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_VALUE_FALSE) == 0
                        || system_row->sflow == NULL)) {
            vty_out(vty, "sFlow                         disabled%s",
                    VTY_NEWLINE);
        } else {
            vty_out(vty, "sFlow                         enabled%s",
                    VTY_NEWLINE);
        }
    } else {
        vty_out(vty, "sFlow                         enabled%s",
                VTY_NEWLINE);
    }

    if (sflow_row->sampling != NULL) {
        vty_out(vty, "Sampling Rate                 %lld%s",
                *(sflow_row->sampling), VTY_NEWLINE);
    } else {
        vty_out(vty, "Sampling Rate                 %d%s",
                SFL_DEFAULT_SAMPLING_RATE, VTY_NEWLINE);
    }
    vty_out(vty, "Number of Samples             %"PRIu64"%s", packet_count,
            VTY_NEWLINE);

    return CMD_SUCCESS;
}

/* [en/dis]able sflow on interface. */
static int
sflow_set_port_config(bool set)
{
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();
    struct smap other_config;
    const struct ovsrec_port *port_row;
    const char *value;
    const struct ovsrec_system *system_row;
    const struct ovsrec_sflow *sflow_row;
    const struct ovsdb_datum *datum;

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (system_row->sflow == NULL) {
        vty_out(vty, "sFlow must be enabled globally before configuring "
                "per-interface.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    sflow_row = ovsrec_sflow_first(idl);
    if (sflow_row == NULL) {
        vty_out(vty, "sFlow must have sampling rate and collectors "
                "configured before configuring per-interface.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    datum = ovsrec_sflow_get_sampling(sflow_row, OVSDB_TYPE_INTEGER);
    if (datum == NULL || datum->n == 0) {
        vty_out(vty, "sFlow sampling rate must be configured before "
                "configuring per-interface.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    datum = ovsrec_sflow_get_targets(sflow_row, OVSDB_TYPE_STRING);
    if (datum == NULL || datum->n == 0) {
        vty_out(vty, "sFlow collectors must be configured before "
                "configuring per-interface.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    port_row = port_find((const char *) vty->index);
    if (port_row == NULL) {
        VLOG_ERR("Null port row, can't [un]set sflow on it.");
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    smap_init(&other_config);
    value = set ? PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_VALUE_TRUE :
            PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_VALUE_FALSE;

    if (smap_is_empty(&port_row->other_config)) {
        smap_add(&other_config, PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_KEY_STR,
                 value);
    } else {
        smap_clone(&other_config, &port_row->other_config);
        smap_replace(&other_config,
                     PORT_OTHER_CONFIG_SFLOW_PER_INTERFACE_KEY_STR, value);
    }

    ovsrec_port_set_other_config(port_row, &other_config);
    smap_destroy(&other_config);

    txn_status = cli_do_config_finish(status_txn);

    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED) {
        return CMD_SUCCESS;
    } else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets the row name in the sFlow table and a reference
 * to it in the System table. This is done when sflow is [en/dis]abled
 * respectively in CLI in config node.
 */
static int
sflow_set_global_status(bool status)
{
    const struct ovsrec_system *system_row;
    const struct ovsrec_sflow *sflow_row;

    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        VLOG_ERR(OVSDB_ROW_FETCH_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (status) {
        if (system_row->sflow != NULL) {
            vty_out(vty, "sFlow already enabled.%s", VTY_NEWLINE);
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        } else {
            if (sflow_row == NULL) {
                sflow_row = sflow_insert(status_txn);
            }
            ovsrec_system_set_sflow(system_row, sflow_row);
        }
    } else {
        if (sflow_row && system_row->sflow != NULL) {
            ovsrec_system_set_sflow(system_row, NULL);
        } else {
            vty_out(vty, "sFlow already disabled.%s", VTY_NEWLINE);
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }
    }

    txn_status = cli_do_config_finish(status_txn);

    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED) {
        return CMD_SUCCESS;
    } else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets the sflow sampling rate provided by the user */
static int
sflow_set_sampling_rate(int64_t *rate)
{
    enum ovsdb_idl_txn_status txn_status;
    const struct ovsrec_sflow *sflow_row;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (!sflow_row && !rate) {
        vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (!sflow_row && rate) {
        sflow_row = sflow_insert(status_txn);
    }

    if (rate) {
        ovsrec_sflow_set_sampling(sflow_row, rate, 1);
    } else {
        ovsrec_sflow_set_sampling(sflow_row, rate, 0);
    }

    txn_status = cli_do_config_finish(status_txn);
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED)
        return CMD_SUCCESS;
    else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function adds a new collector to the
   existing list of sFlow collectors. */
static int
sflow_collector_add(const struct ovsrec_sflow *sflow_row,
                    char *collector, const char *ip, const char *port)
{
    int m;
    char temp_collector[MAX_COLLECTOR_LENGTH];
    char **target;
    char *collector_ip, *collector_port;

    for (m = 0; m < sflow_row->n_targets; m++) {
        memset(temp_collector, 0, sizeof(temp_collector));
        strncpy(temp_collector, sflow_row->targets[m], MAX_COLLECTOR_LENGTH);
        collector_ip = strtok(temp_collector, "/");
        collector_port = strtok(NULL, "/");
        /* Compares user provided ip with ip in database */
        if (strcmp(ip, collector_ip) == 0) {
            if (collector_port && port) {
                if (strcmp(port, collector_port) == 0) {
                    vty_out(vty, "sFlow collector already present.%s",
                            VTY_NEWLINE);
                    return 1;
                } else if (sflow_row->n_targets == 3) {
                    vty_out(vty, "Maximum of 3 sFlow collectors allowed.%s",
                            VTY_NEWLINE);
                    return 1;
                }
            } else if (!collector_port && !port) {
                vty_out(vty, "sFlow collector already present.%s",
                        VTY_NEWLINE);
                return 1;
            }
        }
    }
    if (sflow_row->n_targets == 3) {
        vty_out(vty, "Maximum of 3 sFlow collectors allowed.%s",
                VTY_NEWLINE);
        return 1;
    }

    target = xmalloc(MAX_COLLECTOR_LENGTH * (sflow_row->n_targets + 1));

    for (m = 0; m < sflow_row->n_targets; m++) {
        target[m] = sflow_row->targets[m];
    }

    target[sflow_row->n_targets] = collector;
    ovsrec_sflow_set_targets(sflow_row, target, sflow_row->n_targets + 1);
    free(target);

    return 0;
}

/* This function removes an existing sFlow collector. */
static int
sflow_collector_remove(const struct ovsrec_sflow *sflow_row,
                       char *collector)
{
    int m, n;
    bool sflow_collector_match = false;
    char **target;

    if (sflow_row->n_targets == 0) {
        vty_out(vty, "No sFlow collector present.%s", VTY_NEWLINE);
        return 1;
    }
    for (m = 0; m < sflow_row->n_targets; m++) {
        if (strcmp(collector, sflow_row->targets[m]) == 0) {
            sflow_collector_match = true;
            break;
        }
    }
    if (!sflow_collector_match) {
        vty_out(vty, "sFlow collector not found.%s", VTY_NEWLINE);
        return 1;
    }
    target = xmalloc(MAX_COLLECTOR_LENGTH * (sflow_row->n_targets - 1));

    for (m = n = 0; m < sflow_row->n_targets; m++) {
        if (strcmp(collector, sflow_row->targets[m]) != 0)
            target[n++] = sflow_row->targets[m];
    }
    ovsrec_sflow_set_targets(sflow_row, target, n);
    free(target);
    return 0;
}

/* This function sets/unsets the collector(target) ip, port number and
 * vrf provided by the user */
static int
sflow_set_collector(const char *ip, const char *port,
                    const char *vrf, bool set)
{
    enum ovsdb_idl_txn_status txn_status;
    const struct ovsrec_sflow *sflow_row;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();
    char collector[MAX_COLLECTOR_LENGTH];
    int ret;
    struct prefix p;
    char prefix_str[256];
    char *prefix_copy;

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    ret = str2prefix(ip, &p);
    if (ret <= 0) {
        vty_out(vty, "Malformed address format%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_WARNING;
    }

    memset(prefix_str, 0, sizeof(prefix_str));
    prefix2str((const struct prefix*) &p, prefix_str, sizeof(prefix_str));
    prefix_copy = strtok(prefix_str, "/");

    if (strcmp(prefix_copy, ip)) {
        vty_out(vty, "Invalid IP address. Valid address: %s", prefix_str);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    if (vrf != NULL) {
        if (strcmp(vrf, DEFAULT_VRF_NAME) != 0) {
            vty_out(vty, "Only vrf_default is permitted.%s", VTY_NEWLINE);
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (!sflow_row && !set) {
        vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (!sflow_row && set) {
        sflow_row = sflow_insert(status_txn);
    }

    memset(collector, 0, sizeof(collector));
    if (port == NULL && vrf == NULL) {
        snprintf(collector, MAX_COLLECTOR_LENGTH, "%s", ip);
    } else if (port != NULL && vrf == NULL) {
        snprintf(collector, MAX_COLLECTOR_LENGTH, "%s/%s", ip, port);
    } else if (port == NULL && vrf != NULL) {
        snprintf(collector, MAX_COLLECTOR_LENGTH, "%s/%s", ip, vrf);
    } else {
        snprintf(collector, MAX_COLLECTOR_LENGTH, "%s/%s/%s", ip, port, vrf);
    }

    if (set) {
        if (sflow_collector_add(sflow_row, collector, ip, port)) {
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }
    } else {
        if (sflow_collector_remove(sflow_row, collector)) {
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }
    }

    txn_status = cli_do_config_finish(status_txn);
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED) {
        return CMD_SUCCESS;
    } else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets agent interface and its address family to be
 * used to communicate with the collector. Interface needs to be an L3
 * interface.
 *
 * OPS_TODO: An interface can be configured to be sFlow's agent interface.
 * It may not have IPv4 (or v6) address configured on it. Backend is
 * supposed to handle it. Issues to be handled in near future: Once
 * configured, removing IPv4 (or v6) address from agent interface and
 * handling it in sFlow.
 */
static int
sflow_set_agent_interface(const char *interface, const char *family,
                          bool set)
{
    const struct ovsrec_sflow *sflow_row;
    const struct ovsrec_interface *intf_row;
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    if (set) {
        intf_row = interface_find(interface);

        if (!intf_row) {
            vty_out(vty, "Invalid interface%s", VTY_NEWLINE);
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }

        sflow_row = ovsrec_sflow_first(idl);
        if (!sflow_row) {
            sflow_row = sflow_insert(status_txn);
        }

        ovsrec_sflow_set_agent(sflow_row, interface);
        ovsrec_sflow_set_agent_addr_family(sflow_row, family);
    }
    /* Handling the "no" form of the command and setting fields to none */
    else {
        sflow_row = ovsrec_sflow_first(idl);
        if (!sflow_row) {
            vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
            cli_do_config_abort(status_txn);
            return CMD_SUCCESS;
        }
        ovsrec_sflow_set_agent(sflow_row, interface);
        ovsrec_sflow_set_agent_addr_family(sflow_row, family);
    }

    txn_status = cli_do_config_finish(status_txn);

    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED) {
        return CMD_SUCCESS;
    } else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets the sflow header size provided by the user */
static int
sflow_set_header_size(int64_t *size)
{
    enum ovsdb_idl_txn_status txn_status;
    const struct ovsrec_sflow *sflow_row;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (!sflow_row && !size) {
        vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (!sflow_row && size) {
        sflow_row = sflow_insert(status_txn);
    }

    if (size) {
        ovsrec_sflow_set_header(sflow_row, size, 1);
    } else {
        ovsrec_sflow_set_header(sflow_row, size, 0);
    }

    txn_status = cli_do_config_finish(status_txn);
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED)
        return CMD_SUCCESS;
    else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets the sflow max datagram size provided by the user */
static int
sflow_set_max_datagram_size(int64_t *size)
{
    enum ovsdb_idl_txn_status txn_status;
    const struct ovsrec_sflow *sflow_row;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (!sflow_row && !size) {
        vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (!sflow_row && size) {
        sflow_row = sflow_insert(status_txn);
    }

    if (size) {
        ovsrec_sflow_set_max_datagram(sflow_row, size, 1);
    } else {
        ovsrec_sflow_set_max_datagram(sflow_row, size, 0);
    }

    txn_status = cli_do_config_finish(status_txn);
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED)
        return CMD_SUCCESS;
    else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

/* This function sets/unsets the sflow polling interval provided by the
 user */
static int
sflow_set_polling(int64_t *interval)
{
    enum ovsdb_idl_txn_status txn_status;
    const struct ovsrec_sflow *sflow_row;
    struct ovsdb_idl_txn *status_txn = cli_do_config_start();

    if (status_txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(status_txn);
        return CMD_OVSDB_FAILURE;
    }

    sflow_row = ovsrec_sflow_first(idl);

    if (!sflow_row && !interval) {
        vty_out(vty, "No sFlow configuration present.%s", VTY_NEWLINE);
        cli_do_config_abort(status_txn);
        return CMD_SUCCESS;
    }

    if (!sflow_row && interval) {
        sflow_row = sflow_insert(status_txn);
    }

    if (interval) {
        ovsrec_sflow_set_polling(sflow_row, interval, 1);
    } else {
        ovsrec_sflow_set_polling(sflow_row, interval, 0);
    }

    txn_status = cli_do_config_finish(status_txn);
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED)
        return CMD_SUCCESS;
    else {
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

DEFUN (cli_sflow_set_global_status,
        cli_sflow_set_global_status_cmd,
        "sflow enable",
        SFLOW_STR
        "Enable or disable sflow feature\n")
{
    if (vty->node == INTERFACE_NODE) {
        return sflow_set_port_config(true);
    }
    return sflow_set_global_status(true);
}

DEFUN (cli_sflow_no_set_global_status,
        cli_sflow_no_set_global_status_cmd,
        "no sflow enable",
        NO_STR
        SFLOW_STR
        "Enable or disable sflow feature\n")
{
    if (vty->node == INTERFACE_NODE) {
        return sflow_set_port_config(false);
    }

    return sflow_set_global_status(false);
}

DEFUN (cli_flow_set_sampling_rate,
        cli_sflow_set_sampling_rate_cmd,
        "sflow sampling <1-1000000000>",
        SFLOW_STR
        "Set Sampling rate\n"
        "Sampling rate range (Default: 4096)\n")
{
    int64_t s_rate = (int64_t) atoi(argv[0]);
    return sflow_set_sampling_rate(&s_rate);
}

DEFUN (cli_sflow_no_set_sampling_rate,
        cli_sflow_no_set_sampling_rate_cmd,
        "no sflow sampling",
        NO_STR
        SFLOW_STR
        "Set sampling rate\n")
{
    return sflow_set_sampling_rate(NULL);
}

DEFUN (cli_sflow_set_collector,
        cli_sflow_set_collector_cmd,
        "sflow collector (A.B.C.D|X:X::X:X)"
        " {port <0-65535> | vrf VRF_NAME}",
        SFLOW_STR
        "Set collector IP address configuration\n"
        "IPv4 address\n"
        "IPv6 address\n"
        "Port information\n"
        "Port number (Default: 6343)\n"
        "Assign a VRF\n"
        "VRF name (Default: vrf_default)\n")
{
    return sflow_set_collector(argv[0], argv[1], argv[2], true);
}

DEFUN (cli_sflow_no_set_collector,
        cli_sflow_no_set_collector_cmd,
        "no sflow collector (A.B.C.D|X:X::X:X)"
        " {port <0-65535> | vrf VRF_NAME}",
        NO_STR
        SFLOW_STR
        "Set collector IP address configuration\n"
        "IPv4 address\n"
        "IPv6 address\n"
        "Port information\n"
        "Port number (Default: 6343)\n"
        "Assign a VRF\n"
        "VRF name (Default: vrf_default)\n")
{
    return sflow_set_collector(argv[0], argv[1], argv[2], false);
}

DEFUN (cli_sflow_set_agent_interface,
        cli_sflow_set_agent_interface_cmd,
        "sflow agent-interface INTERFACE",
        SFLOW_STR
        "Set agent interface\n"
        "Interface name\n")
{
    return sflow_set_agent_interface(argv[0], NULL, true);
}

DEFUN (cli_sflow_set_agent_interface_family,
        cli_sflow_set_agent_interface_family_cmd,
        "sflow agent-interface INTERFACE"
        " (ipv4|ipv6)",
        SFLOW_STR
        "Set agent interface\n"
        "Interface name\n"
        "IPv4 agent address family (Default)\n"
        "IPv6 agent address family\n")
{
    return sflow_set_agent_interface(argv[0], argv[1], true);
}

DEFUN (cli_sflow_no_set_agent_interface,
        cli_sflow_no_set_agent_interface_cmd,
        "no sflow agent-interface",
        NO_STR
        SFLOW_STR
        "Set agent interface\n")
{
    return sflow_set_agent_interface(NULL, NULL, false);
}

DEFUN (cli_sflow_set_header_size,
        cli_sflow_set_header_size_cmd,
        "sflow header-size <64-256>",
        SFLOW_STR
        "Configure sFlow header size\n"
        "Header size (Default: 128 bytes)\n")
{
    int64_t h_size = (int64_t) atoi(argv[0]);
    return sflow_set_header_size(&h_size);
}

DEFUN (cli_sflow_no_set_header_size,
        cli_sflow_no_set_header_size_cmd,
        "no sflow header-size",
        NO_STR
        SFLOW_STR
        "Configure sFlow header size\n")
{
    return sflow_set_header_size(NULL);
}

DEFUN (cli_sflow_set_max_datagram_size,
        cli_sflow_set_max_datagram_size_cmd,
        "sflow max-datagram-size <1-9000>",
        SFLOW_STR
        "Configure sFlow maximum datagram size\n"
        "Datagram size (Default: 1400 bytes)\n")
{
    int64_t d_size = (int64_t) atoi(argv[0]);
    return sflow_set_max_datagram_size(&d_size);
}

DEFUN (cli_sflow_no_set_max_datagram_size,
        cli_sflow_no_set_max_datagram_size_cmd,
        "no sflow max-datagram-size",
        NO_STR
        SFLOW_STR
        "Configure sFlow maximum datagram size\n")
{
    return sflow_set_max_datagram_size(NULL);
}

DEFUN (cli_sflow_set_polling,
        cli_sflow_set_polling_cmd,
        "sflow polling <0-3600>",
        SFLOW_STR
        "Configure polling interval\n"
        "Polling interval (Default: 30 seconds)\n")
{
    int64_t i_size = (int64_t) atoi(argv[0]);
    return sflow_set_polling(&i_size);
}

DEFUN (cli_sflow_no_set_polling,
        cli_sflow_no_set_polling_cmd,
        "no sflow polling",
        NO_STR
        SFLOW_STR
        "Configure polling interval\n")
{
    return sflow_set_polling(NULL);
}

DEFUN (cli_sflow_show,
        cli_sflow_show_cmd,
        "show sflow",
        SHOW_STR
        SFLOW_STR)
{
    return sflow_show();
}

DEFUN (cli_sflow_show_intf_statistics,
        cli_sflow_show_intf_statistics_cmd,
        "show sflow interface INTERFACE",
        SHOW_STR
        SFLOW_STR
        INTERFACE_STR
        "Interface name")
{
    return sflow_show_intf_statistics(argv[0]);
}

/* Install SFLOW related vty commands. */
void
sflow_vty_init(void)
{
    install_element(CONFIG_NODE, &cli_sflow_set_global_status_cmd);
    install_element(INTERFACE_NODE, &cli_sflow_set_global_status_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_global_status_cmd);
    install_element(INTERFACE_NODE, &cli_sflow_no_set_global_status_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_sampling_rate_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_polling_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_polling_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_sampling_rate_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_collector_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_collector_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_agent_interface_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_agent_interface_family_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_agent_interface_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_header_size_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_header_size_cmd);
    install_element(CONFIG_NODE, &cli_sflow_set_max_datagram_size_cmd);
    install_element(CONFIG_NODE, &cli_sflow_no_set_max_datagram_size_cmd);
    install_element(ENABLE_NODE, &cli_sflow_show_intf_statistics_cmd);
    install_element(ENABLE_NODE, &cli_sflow_show_cmd);
}
