/*
 *
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
 *
 * Hewlett-Packard Company Confidential (C) Copyright 2015 Hewlett-Packard Development Company, L.P.
 *
 */

#ifndef BUFMON__PROVIDER_H
#define BUFMON__PROVIDER_H 1

#include "simap.h"
#include "smap.h"
#include "seq.h"

/* Statistics collection mode */
typedef enum
{
    MODE_CURRENT = 0,
    MODE_PEAK
} collection_mode;

/*************************************************************************//**
 * bufmon's internal data structure for configuration data.
 ****************************************************************************/
typedef struct bufmon_system_config {
    bool                enabled;
    collection_mode     counters_mode;
    bool                periodic_collection_enabled;
    int                 collection_period;
    bool                threshold_trigger_collection_enabled;
    int                 threshold_trigger_rate_limit;
    bool                snapshot_on_threshold_trigger;
} bufmon_system_config_t;

/*************************************************************************//**
 * bufmon's internal data structure for counter information
 ****************************************************************************/
typedef struct bufmon_counter_info {
    /* counter_value column. */
    int64_t counter_value;

    /* counter_vendor_specific_info */
    struct smap counter_vendor_specific_info;

    /* enabled column. */
    bool enabled;

    /* ASIC_id */
    int hw_unit_id;

    /* counter name column. */
    char *name; /* Always nonnull. */

    /* status column. */
    int status;

    /* trigger_threshold */
    int64_t trigger_threshold;
} bufmon_counter_info_t;

struct bufmon_class {
/* ## ----------------- ## */
/* ## Factory Functions ## */
/* ## ----------------- ## */

    /* Initializes provider.*/
    int (*init)(void);

    /* buffer monitoring global configuration function */
    void (*bufmon_system_config)(const bufmon_system_config_t *cfg);

    /* buffer monitoring counter configuration function */
    void (*bufmon_counter_config)(bufmon_counter_info_t *counter);

    /* buffer monitoring function to get current counter stats value */
    void (*bufmon_counter_stats_get)(bufmon_counter_info_t *counter_list,
                                     int num_counter);

    /* Trigger register function to get notification once threhold is crossed  */
    void (*bufmon_trigger_register)(bool enable);

};

void bufmon_init(void);
void bufmon_exit(void);

void bufmon_run(void);
void bufmon_wait(void);


int bufmon_class_register(const struct bufmon_class *);
void bufmon_trigger_callback(void);
struct seq * bufmon_trigger_seq_get(void);
void bufmon_stats_get(bufmon_counter_info_t *counter_list, int num_counter);
void bufmon_set_system_config(const bufmon_system_config_t *cfg);
void bufmon_set_counter_config(bufmon_counter_info_t *counter);
void bufmon_trigger_enable(bool flag);
#endif /* bufmon-provider.h */
