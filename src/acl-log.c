/* Copyright (C) 2016 Hewlett Packard Enterprise Development LP
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

#include <config.h>

#include "acl-log.h"
#include "ovs-thread.h"
#include "poll-loop.h"
#include "seq.h"

static struct seq *acl_log_pktrx_seq;

static struct ovs_mutex acl_log_mutex = OVS_MUTEX_INITIALIZER;

static struct acl_log_info info OVS_GUARDED_BY(acl_log_mutex) = { .valid_fields = 0 };

/* Provides a global seq for acl logging events.
 *
 * ACL logging modules should call seq_change() on the returned object whenever
 * a packet is received for ACL logging.
 *
 * Clients can seq_wait() on this object to do the logging and tell all ASICs
 * to stop copying packets to the CPU. */
struct seq *
acl_log_pktrx_seq_get(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        acl_log_pktrx_seq = seq_create();
        ovsthread_once_done(&once);
    }

    return acl_log_pktrx_seq;
}

void
acl_log_pkt_data_get(struct acl_log_info *pkt_info_to_get)
{
   /* validate the input */
   if (!pkt_info_to_get)
      return;

   /* take the mutex */
   ovs_mutex_lock(&acl_log_mutex);

   /* copy the static value into the data to be returned */
   memcpy(&pkt_info_to_get, &info, sizeof(struct acl_log_info));

   /* zero out the static value to avoid returning the same packet info twice*/
   memset(&info, 0, sizeof(struct acl_log_info));

   /* give the mutex */
   ovs_mutex_unlock(&acl_log_mutex);
}

void
acl_log_pkt_data_set(struct acl_log_info *new_pkt)
{
   /* validate the input */
   if (!new_pkt)
      return;

   /* take the mutex */
   ovs_mutex_lock(&acl_log_mutex);

   /* copy the argument into the static value */
   memcpy(&info, new_pkt, sizeof(struct acl_log_info));

   /* give the mutex */
   ovs_mutex_unlock(&acl_log_mutex);
}
