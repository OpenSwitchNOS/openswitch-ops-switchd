
/*
 * Copyright (c) 2016 Hewlett Packard Enterprise Development LP
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
 * QoS defines used by multiple modules.  Placed here so they can be
 * in a central repository, ops-switchd, that most modules already have
 * a dependency on.
 */

#ifndef SWITCHD_QOS_H
#define SWITCHD_QOS_H 1

#ifdef  __cplusplus
extern "C" {
#endif


#define NUM_QUEUES 8        /* Number of queues per interface. */


#ifdef  __cplusplus
}
#endif

#endif /* switchd_qos.h */
