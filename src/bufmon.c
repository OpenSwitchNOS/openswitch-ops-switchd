/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015 Nicira, Inc.
 * Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
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

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <compiler.h>
#include "coverage.h"
#include <dynamic-string.h>
#include <fatal-signal.h>
#include <hash.h>
#include <ovsdb-idl.h>
#include <openvswitch/vlog.h>
#include <ovs-thread.h>
#include <openswitch-idl.h>
#include <poll-loop.h>
#include <shash.h>
#include <unixctl.h>
#include <util.h>
#include <vswitch-idl.h>
#include "bufmon-provider.h"
#include "latch.h"
#include "plugins.h"
#include "seq.h"
#include "smap.h"
#include "timeval.h"


VLOG_DEFINE_THIS_MODULE(bufmon);

COVERAGE_DEFINE(bufmon_reconfigure);

#define DEFAULT_COLLECTION_INTERVAL 5 /* seconds */
#define DEFAULT_TRIGGER_RATE_LIMIT_COUNT 60 /* per Min */
#define DEFAULT_TRIGGER_RATE_LIMIT_DURATION 60 /* seconds */
#define DEFAULT_TRIGGER_REPORT_INTERVAL 100 /* msec */
#define COUNTER_MODE_PEAK "peak"

/* OVSDB IDL used to obtain configuration. */
extern struct ovsdb_idl *idl;

/* Most recently processed IDL sequence number. */
static unsigned int idl_seqno;

static struct ovs_mutex bufmon_mutex = OVS_MUTEX_INITIALIZER;

static bufmon_system_config_t bufmon_cfg; OVS_GUARDED_BY(bufmon_mutex);

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

OVS_NO_RETURN static void *bufmon_stats_thread(void *);

static bufmon_counter_info_t *g_counter_list OVS_GUARDED_BY(bufmon_mutex) = NULL;

static int num_active_counters OVS_GUARDED_BY(bufmon_mutex) = 0;

static struct latch bufmon_latch OVS_GUARDED_BY(bufmon_mutex);

void
bufmon_init(void)
{
    ovsdb_idl_omit_alert(idl, &ovsrec_bufmon_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_bufmon_col_counter_value);
    ovsdb_idl_omit_alert(idl, &ovsrec_open_vswitch_col_bufmon_info);

    idl_seqno = ovsdb_idl_get_seqno(idl);

} /* bufmon_init */

static void
bufmon_enable_stats(bool enable)
                    OVS_REQUIRES(bufmon_mutex)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (enable) {
        if (ovsthread_once_start(&once)) {

            /* Register the bufmon provider class */
            plugins_bufmon_register();

            /* Create struct seq to get trigger notification */
            bufmon_trigger_seq_get();

            /* Create the new thread to poll the stats */
            ovs_thread_create("bufmon_stats", bufmon_stats_thread, NULL);

            ovsthread_once_done(&once);
            latch_init(&bufmon_latch);
        }
    }
} /* bufmon_enable_stats */

static int
bufmon_enabled_counters_count(void)
{
    const struct ovsrec_bufmon *counter_row = NULL;
    int count = 0;

    if (!bufmon_cfg.enabled)
        return count;

    OVSREC_BUFMON_FOR_EACH(counter_row, idl) {
        if (counter_row && counter_row->enabled) {
            count++;
        }
    }

    VLOG_DBG("bufmon_enabled_counters_count %d \n", count);
    return count;
} /* bufmon_enabled_counters_count */

static void
bufmon_discard_counter_list(void)
                    OVS_REQUIRES(bufmon_mutex)
{
    int i = 0;

    while (i < num_active_counters) {
        bufmon_counter_info_t *counter = &g_counter_list[i];
        smap_destroy(&counter->counter_vendor_specific_info);
        free(counter->name);
        i++;
    }

    free(g_counter_list);
    g_counter_list = NULL;
    num_active_counters = 0;
} /* bufmon_discard_counter_list */

static void
bufmon_create_counters_list(void)
{
    const struct ovsrec_bufmon *counter_row = NULL;
    int count = 0, i = 0;

    ovs_mutex_lock(&bufmon_mutex);

    if (g_counter_list != NULL) {
        bufmon_discard_counter_list();
    }

    count = bufmon_enabled_counters_count();

    if (!count) {
        goto End;
    }

    /* Allocate the memory for all the enabled counters */
    g_counter_list = xcalloc(count, sizeof *g_counter_list);

    if (g_counter_list == NULL) {
        VLOG_ERR("bufmon_create_counters_list alloc failed \n");
        goto End;
    }

    /* Copy the counter info from ovs to global list */
    OVSREC_BUFMON_FOR_EACH (counter_row, idl) {
        if (counter_row && counter_row->enabled) {
            bufmon_counter_info_t *counter = &g_counter_list[i];
            smap_clone(&counter->counter_vendor_specific_info,
                       &counter_row->counter_vendor_specific_info);
            counter->enabled = counter_row->enabled;
            counter->hw_unit_id = counter_row->hw_unit_id;
            counter->name = xstrdup(counter_row->name);
            i++;
        }
    }

End:
    num_active_counters = count;
    ovs_mutex_unlock(&bufmon_mutex);

} /* bufmon_create_counters_list */

static void
bufmon_ovsdb_update(void)
{
    const struct ovsrec_bufmon *counter_row = NULL;
    const struct ovsrec_open_vswitch *system_cfg = NULL;
    int i = 0;
    char status[32];
    enum ovsdb_idl_txn_status txn_status = TXN_ERROR;
    char time_stamp[256];
    static struct ovsdb_idl_txn *bufmon_stats_txn = NULL;

    /* Update the counter values and status changes to the dB. */
    if (!bufmon_stats_txn) {

        bufmon_stats_txn = ovsdb_idl_txn_create(idl);

        system_cfg = ovsrec_open_vswitch_first(idl);

        /* Update the timestamp */
        if (system_cfg) {
            sprintf(time_stamp, "%lld", (long long)time_now());
            smap_replace((struct smap *)&system_cfg->bufmon_info,
                         BUFMON_INFO_MAP_LAST_COLLECTION_TIMESTAMP,
                         (const char *) time_stamp);
            ovsrec_open_vswitch_set_bufmon_info(system_cfg ,
                            (const struct smap *)&system_cfg->bufmon_info);
        }

        OVSREC_BUFMON_FOR_EACH (counter_row, idl) {
            if (counter_row && counter_row->enabled) {
                bufmon_counter_info_t *counter = &g_counter_list[i];
                ovsrec_bufmon_set_counter_value(counter_row,
                                                &counter->counter_value, 1);

                /* Update the counter status whether poll or trigger */
                if (counter->status == BUFMON_STATUS_TRIGGERED) {
                    strcpy(status, OVSREC_BUFMON_STATUS_TRIGGERED);
                } else {
                    strcpy(status, OVSREC_BUFMON_STATUS_OK);
                }

                ovsrec_bufmon_set_status(counter_row, status);
                i++;
            }
        }
    }

    /* TODO Retry OVSDB update*/
    if (bufmon_stats_txn) {
        /* Some OVSDB write needs to happen.*/
        txn_status = ovsdb_idl_txn_commit(bufmon_stats_txn);
        ovsdb_idl_txn_destroy(bufmon_stats_txn);
        bufmon_stats_txn = NULL;
    }

    VLOG_DBG("bufmon_ovsdb_update %d \n", txn_status);

} /* bufmon_ovsdb_update */

static void
bufmon_get_current_counters_value(bool triggered)
                                  OVS_REQUIRES(bufmon_mutex)
{
    int i = 0;

    /* Active counters list is empty */
    if (g_counter_list == NULL || !num_active_counters) {
        return;
    }

    bufmon_stats_get(g_counter_list, num_active_counters);

    /* Update the status */
    for (i =0; i < num_active_counters; i++) {
        bufmon_counter_info_t *counter = &g_counter_list[i];
        counter->status = (triggered ? BUFMON_STATUS_TRIGGERED
                           : BUFMON_STATUS_OK);
    }

    /* latch is set to indicate the bufmon_run to update the OVS-DB */
    latch_set(&bufmon_latch);
} /* bufmon_get_current_counters_value */

/* Counter stats polling */
static void
bufmon_run_stats_update(bool triggered)
                        OVS_EXCLUDED(bufmon_mutex)
{
    long long int now = time_msec();
    static long long int next_poll_interval;

    ovs_mutex_lock(&bufmon_mutex);

    /* periodic/trigger collection enabled? */
    if (!bufmon_cfg.enabled
        || !(bufmon_cfg.periodic_collection_enabled
        || bufmon_cfg.threshold_trigger_collection_enabled)) {
        goto End;
    }

    /* Time for a poll? */
    if ((!bufmon_cfg.periodic_collection_enabled ||
             (now < next_poll_interval)) && !triggered) {
        goto End;
    }

    /* Trigger Enabled? */
    if (triggered &&
        !bufmon_cfg.threshold_trigger_collection_enabled) {
        goto End;
    }

    bufmon_get_current_counters_value(triggered);
    next_poll_interval = now + (bufmon_cfg.collection_period * 1000);

    /* Reconfigure the System */
    if (triggered) {
        bufmon_set_system_config(&bufmon_cfg);
    }

End:
    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_run_stats_update */

static void
bufmon_system_config_update(const struct ovsrec_open_vswitch *row)
                            OVS_EXCLUDED(bufmon_mutex)
{
    const char *counter_mode = NULL;

    ovs_mutex_lock(&bufmon_mutex);

    bufmon_cfg.enabled = smap_get_bool(&row->bufmon_config,
                                       BUFMON_CONFIG_MAP_ENABLED, false);

    counter_mode = smap_get(&row->bufmon_config,
                            BUFMON_CONFIG_MAP_COUNTERS_MODE);

    if (counter_mode && !strcmp(counter_mode, COUNTER_MODE_PEAK)) {
        bufmon_cfg.counters_mode = MODE_PEAK;
    } else {
        bufmon_cfg.counters_mode = MODE_CURRENT;
    }

    bufmon_cfg.periodic_collection_enabled =
            smap_get_bool(&row->bufmon_config,
                          BUFMON_CONFIG_MAP_PERIODIC_COLLECTION_ENABLED,
                          false);

    bufmon_cfg.collection_period =
            smap_get_int(&row->bufmon_config,
                         BUFMON_CONFIG_MAP_COLLECTION_PERIOD,
                         DEFAULT_COLLECTION_INTERVAL);

    bufmon_cfg.collection_period = MAX(bufmon_cfg.collection_period,
                                       DEFAULT_COLLECTION_INTERVAL);

    bufmon_cfg.threshold_trigger_collection_enabled =
            smap_get_bool(&row->bufmon_config,
                          BUFMON_CONFIG_MAP_THRESHOLD_TRIGGER_COLLECTION_ENABLED,
                          false);

    bufmon_cfg.threshold_trigger_rate_limit =
            smap_get_int(&row->bufmon_config,
                         BUFMON_CONFIG_MAP_TRIGGER_RATE_LIMIT,
                         DEFAULT_TRIGGER_RATE_LIMIT_COUNT);

    bufmon_cfg.snapshot_on_threshold_trigger =
            smap_get_bool(&row->bufmon_config,
                          BUFMON_CONFIG_MAP_SNAPSHOT_ON_THRESHOLD_TRIGGER,
                          false);

    VLOG_DBG("update %d %d %d %d %d %d \n", bufmon_cfg.enabled,
             bufmon_cfg.periodic_collection_enabled,
             bufmon_cfg.collection_period,
             bufmon_cfg.threshold_trigger_collection_enabled,
             bufmon_cfg.threshold_trigger_rate_limit,
             bufmon_cfg.snapshot_on_threshold_trigger);

    bufmon_set_system_config(&bufmon_cfg);

    /* Spawn bufmon thread and trigger cond signal to start */
    if (bufmon_cfg.enabled) {
        bufmon_enable_stats(bufmon_cfg.enabled);
        xpthread_cond_signal(&cond);
    }

    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_system_config_update */

static void
bufmon_counter_config_update(const struct ovsrec_bufmon *row)
{
    bufmon_counter_info_t counter_info;

    ovs_mutex_lock(&bufmon_mutex);

    if (row->trigger_threshold) {
        counter_info.trigger_threshold = *row->trigger_threshold;
    } else {
        counter_info.trigger_threshold = 0;
    }

    counter_info.counter_vendor_specific_info =
                            row->counter_vendor_specific_info;
    counter_info.hw_unit_id = row->hw_unit_id;
    counter_info.name = row->name;
    counter_info.counter_value = 0;
    counter_info.enabled = row->enabled;

    /* Call the provider function to set the configuration */
    bufmon_set_counter_config(&counter_info);

    ovs_mutex_unlock(&bufmon_mutex);
}/* bufmon_system_config_update */

static void
bufmon_reconfigure(void)
{
    const struct ovsrec_open_vswitch *system_cfg = NULL;
    const struct ovsrec_bufmon *counter_row = NULL;
    bool bufmon_modified = false;
    bool bufmon_enabled = false;

    COVERAGE_INC(bufmon_reconfigure);

    system_cfg = ovsrec_open_vswitch_first(idl);

    /* Buffer monitoring configuration is empty? */
    if (system_cfg == NULL
        || smap_is_empty((const struct smap *)&system_cfg->bufmon_config)) {
        return;
    }

    if (OVSREC_IDL_IS_ROW_INSERTED(system_cfg, idl_seqno)
        || OVSREC_IDL_IS_ROW_MODIFIED(system_cfg, idl_seqno)) {
        bufmon_enabled = smap_get_bool(&system_cfg->bufmon_config,
                                       BUFMON_CONFIG_MAP_ENABLED, false);
        bufmon_modified = true;
    }

    /* Any changes in the bufmon table or system table row */
    OVSREC_BUFMON_FOR_EACH (counter_row, idl) {
        if (OVSREC_IDL_IS_ROW_INSERTED(counter_row, idl_seqno)
            || OVSREC_IDL_IS_ROW_MODIFIED(counter_row, idl_seqno)
            || bufmon_enabled) {
            bufmon_counter_config_update(counter_row);
            bufmon_modified = true;
        }
    }

    if (bufmon_modified) {
        bufmon_system_config_update(system_cfg);
        bufmon_create_counters_list();
    }
} /* bufmon_reconfigure */

void
bufmon_run(void)
{
    if (ovsdb_idl_is_lock_contended(idl)) {
        return;
    } else if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    if (ovsdb_idl_get_seqno(idl) != idl_seqno) {
        bufmon_reconfigure();
        idl_seqno = ovsdb_idl_get_seqno(idl);
    }

    ovs_mutex_lock(&bufmon_mutex);

    if (bufmon_cfg.enabled && latch_poll(&bufmon_latch)) {
        bufmon_ovsdb_update();
    }

    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_run */

void
bufmon_wait(void)
{
    ovs_mutex_lock(&bufmon_mutex);

    if (ovsdb_idl_has_lock(idl)) {
        if (bufmon_cfg.enabled) {
            latch_wait(&bufmon_latch);
        }
    }

    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_wait */

void
bufmon_exit(void)
{
    ovs_mutex_lock(&bufmon_mutex);

    bufmon_discard_counter_list();

    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_exit */

static void
bufmon_trigger_rate_limit(bool flag)
                OVS_EXCLUDED(bufmon_mutex)
{
   ovs_mutex_lock(&bufmon_mutex);

    /* Disabling the Trigger */
    if (flag) {
        bufmon_trigger_enable(false);
    } else if (bufmon_cfg.threshold_trigger_collection_enabled) {
        /* Reconfigure the System */
        bufmon_set_system_config(&bufmon_cfg);
        /* Enable Trigger notifications */
        bufmon_trigger_enable(true);
    }

    ovs_mutex_unlock(&bufmon_mutex);
} /* bufmon_trigger_rate_limit */

static void *
bufmon_stats_thread(void *arg OVS_UNUSED)
                OVS_EXCLUDED(bufmon_mutex)
{
    int trigger_reports_count = 0;
    int trigger_rate_limit;
    static bool trigger_disabled = false;
    uint64_t cur_seqno = seq_read(bufmon_trigger_seq_get());
    long long int next_poll_msec = 0, next_trigger_msec = 0;
    int last_rate_limit_time = time_now();

    pthread_detach(pthread_self());

    for (;;) {
        bool trigger_collection = false;

        ovs_mutex_lock(&bufmon_mutex);

        /* Condition wait if the stats collection not enabled */
        while (!bufmon_cfg.enabled) {
            ovs_mutex_cond_wait(&cond, &bufmon_mutex);
        }

        trigger_rate_limit = bufmon_cfg.threshold_trigger_rate_limit;
        next_poll_msec = time_msec() + (bufmon_cfg.collection_period * 1000);

        ovs_mutex_unlock(&bufmon_mutex);

        do {
            /* Register Timer event for periodic_collection */
            poll_timer_wait_until(next_poll_msec);

            /* Monitor the triggers notification in poll_block? */
            seq_wait(bufmon_trigger_seq_get(), cur_seqno);

            poll_block();

            /* Trigger Handling */
            if (cur_seqno != seq_read(bufmon_trigger_seq_get())) {
                trigger_reports_count++;
                cur_seqno =  seq_read(bufmon_trigger_seq_get());

                /* Trigger Rate limit is crossed? */
                if (trigger_reports_count > trigger_rate_limit
                    || time_msec() < next_trigger_msec) {
                    /* Disable Trigger notification */
                    trigger_disabled = true;
                    bufmon_trigger_rate_limit(trigger_disabled);
                } else {
                    /* Process the trigger notification */
                    trigger_collection = true;
                    next_trigger_msec =
                            time_msec() + DEFAULT_TRIGGER_REPORT_INTERVAL;
                    break;
                }
            } else { /* Periodic poll timeout? */
                /* If the rate limit disabled the trigger notification
                *  re-enable trigger notification after one minute
                */
                if ((time_now() - last_rate_limit_time) >
                    DEFAULT_TRIGGER_RATE_LIMIT_DURATION) {
                    /* Update the trigger rate limit time stamp */
                    last_rate_limit_time = time_now();

                    /* Reset the trigger reports count */
                    trigger_reports_count = 0;

                    if (trigger_disabled) {
                        trigger_disabled = false;
                        bufmon_trigger_rate_limit(false);
                    }
                }
                break; /* Periodic stats collection */
            }
        } while (1);

        bufmon_run_stats_update(trigger_collection);
    }
} /* bufmon_stats_thread */
