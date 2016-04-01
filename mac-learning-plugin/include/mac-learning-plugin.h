/* Copyright (C) 2016 Hewlett-Packard Enterprise Development Company, L.P.
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

#ifndef VSWITCHD_MAC_LEARNING_H
#define VSWITCHD_MAC_LEARNING_H 1

#include "ofproto/ofproto.h"
#include "hmap.h"

#define MAC_LEARNING_PLUGIN_INTERFACE_NAME "MAC_LEARNING_PLUGIN"
#define MAC_LEARNING_PLUGIN_INTERFACE_MAJOR 1
#define MAC_LEARNING_PLUGIN_INTERFACE_MINOR 0

struct mac_learning_plugin_interface {
};

#endif /* mac-learning.h */
