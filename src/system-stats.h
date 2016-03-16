/* Copyright (c) 2010, 2012 Nicira, Inc.
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

#ifndef VSWITCHD_SYSTEM_STATS
#define VSWITCHD_SYSTEM_STATS 1

#include <stdbool.h>
#include "vswitch-idl.h"

void system_stats_enable(bool enable);
void system_stats_run(void);
void system_stats_wait(void);
void reconfigure_system_stats(const struct ovsrec_open_vswitch *cfg);

#endif /* vswitchd/system-stats.h */
