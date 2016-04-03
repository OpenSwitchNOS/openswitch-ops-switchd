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
 *
 * Control Plane Policing (COPP) SwitchD ASIC Provider API
 *
 * Declares the functions and data structures that are used between the
 * SwitchD COPP feature and ASIC-specific providers.
 */

#ifndef COPP_PROVIDER_H
#define COPP_PROVIDER_H 1

#include <errno.h>
#include "smap.h"
#include "ofproto/ofproto.h"

#ifdef  __cplusplus
extern "C" {
#endif


void
copp_stats_init(void);


#ifdef  __cplusplus
}
#endif

#endif /* copp_provider.h */
