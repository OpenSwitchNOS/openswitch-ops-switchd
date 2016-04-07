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

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bufmon-provider.h"
#include <openvswitch/vlog.h>
#include "ovs-thread.h"

VLOG_DEFINE_THIS_MODULE(bufmon_provider);


static struct seq *trigger_seq = NULL;

static const struct bufmon_class *bufmon_class_registered = NULL;

/* Check bufmon provider for NULL pointer and call the function */
#define  BUFMON_PROVIDER_VALIDATE(FUNC)     \
                    if (!bufmon_class_registered ||     \
                        !bufmon_class_registered->FUNC) {   \
                        return;    \
                    }

/* Initializes and registers a new bufmon provider. After successful
 * registration, can be used to collect the counters from ASIC . */
int
bufmon_class_register(const struct bufmon_class *new_class)
{
    int error = 0;
    static bool bufmon_registered = false;

    if (bufmon_registered) {
        return error;
    }

    VLOG_DBG("register bufmon provider");

    error = new_class->init ? new_class->init() : 0;
    if (!error) {
        bufmon_class_registered = new_class;
        bufmon_registered = true;
    }

    return error;
}/* bufmon_class_register */

/* Provides a global seq for bufmon trigger notifications.
 * bufmon monitoring module should call seq_change() on the returned
 * object whenever the event trigger notification from the callback is called
 *
 * seq_wait() monitor on this object will get trigger notification changes to
 * collect the buffer monitoring counters */
struct seq *
bufmon_trigger_seq_get(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        trigger_seq = seq_create();
        ovsthread_once_done(&once);
    }

    return trigger_seq;
}/* bufmon_trigger_seq_get */

void
bufmon_trigger_callback(void)
{
    seq_change(bufmon_trigger_seq_get());
}

void
bufmon_stats_get(bufmon_counter_info_t *counter_list, int num_counter)
{
    BUFMON_PROVIDER_VALIDATE(bufmon_counter_stats_get);

    bufmon_class_registered->bufmon_counter_stats_get(counter_list,
                                                      num_counter);
}

void
bufmon_set_system_config(const bufmon_system_config_t *cfg)
{
    BUFMON_PROVIDER_VALIDATE(bufmon_system_config);

    bufmon_class_registered->bufmon_system_config(cfg);
}

void
bufmon_set_counter_config(bufmon_counter_info_t *counter)
{
    BUFMON_PROVIDER_VALIDATE(bufmon_counter_config);

    bufmon_class_registered->bufmon_counter_config(counter);
}

void
bufmon_trigger_enable(bool flag)
{
    BUFMON_PROVIDER_VALIDATE(bufmon_trigger_register);

    bufmon_class_registered->bufmon_trigger_register(flag);
}

void
bufmon_cell_to_byte_get(int *byte)
{
    BUFMON_PROVIDER_VALIDATE(bufmon_cell_to_byte);
    
    bufmon_class_registered->bufmon_cell_to_byte(byte);
}
