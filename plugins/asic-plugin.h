
/* Copyright (C) 2016 Hewlett-Packard Development Company, L.P.
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

#ifndef __ASIC_PLUGIN_H__
#define __ASIC_PLUGIN_H__ 1

/**
 * Asic Plugin
 *
 *  SwitchD Plugin Infrastructure includes two types of plugins:
 *
 * - Asic plugins: are plugins that implement the platform dependent asic code.
 *   There can be only one asic plugin loaded in the system. The asic plugin
 *   must implement all the functionality defined in asic_plugin.h. The plugin
 *   infrastructure will enforce that the asic plugin meets the major and minor
 *   versioning numbers specified in the asic_plugin.h file to guard against ABI
 *   breakage.
 *
 * - Feature plugins: are plugins that implement feature code that is
 *   indenpendent of the asic. A feature plugin can export public functions for
 *   other plugins or the main switchd code to use. A feature plugin will define
 *   its plublic interface with a major and minor number for versioning. The
 *   plugin infrastructure will provide methods to find and access the feature
 *   plugin interfaces; it will also validate that the requested major and minor
 *   numbers against the feature plugin public interface.
 *
 *   For asic plugins versioning is enforced by the plugin infrastructure based
 *   on the required version in asic_plugin.h file. For feature plugins
 *   versioning is enforced by the plugin infrastructure based on the exported
 *   plugin interface.
 */

#include <stdio.h>
#include <stdlib.h>

/** @def ASIC_PLUGIN_INTERFACE_NAME
 *  @brief asic plugin name definition
 */
#define ASIC_PLUGIN_INTERFACE_NAME     "ASIC_PLUGIN"

/** @def ASIC_PLUGIN_INTERFACE_MAJOR
 *  @brief plugin major version definition
 */
#define ASIC_PLUGIN_INTERFACE_MAJOR    1

/** @def ASIC_PLUGIN_INTERFACE_MINOR
 *  @brief plugin minor version definition
 */
#define ASIC_PLUGIN_INTERFACE_MINOR    1

/* structures */

/** @struct asic_plugin_interface
 *  @brief asic_plugin_interface enforces the interface that an ASIC plugin must
 *  provide to be compatible with SwitchD Asic plugin infrastructure.
 *  When an external plugin attempts to register itself as an ASIC plugin, the
 *  code will validate that the interface provided meets the requirements for
 *  MAJOR and MINOR versions.
 *
 *  - The ASIC_PLUGIN_INTERFACE_NAME identifies the registered interface as an
 *  ASIC plugin. All asic plugins must use the same interface name. The plugin
 *  infrastructure will enforce that only one asic plugin can be registered at a
 *  time. Asic plugins from vendors will have different names but they will
 *  register the same interface name.
 *
 *  - The ASIC_PLUGIN_INTERFACE_MAJOR identifies any large change in the fields
 *  of struct asic_plugin_interface that would break the ABI, so any extra
 *  fields added in the middle of previous fields, removal of previous fields
 *  would trigger a change in the MAJOR number.
 *
 *  - The ASIC_PLUGIN_INTERFACE_MINOR indentifies any incremental changes to the
 *  fields of struct asic_plugin_interface that would not break the ABI but
 *  would just make the new fields unavailable to the older component.
 *
 *  For example if ASIC_PLUGIN_INTERFACE_MAJOR is 1 and
 *  ASIC_PLUGIN_INTERFACE_MINOR is 2, then a plugin can register itself as an
 *  asic plugin if the provided interface has a MAJOR=1 and MINOR>=2. This means
 *  that even if the plugin provides more functionality in the interface fields
 *  those would not be used by SwitchD. But if the plugin has a MAJOR=1 and
 *  MINOR=1 then it cannot be used as an asic plugin as SwitchD will see fields
 *  in the interface struct that are not provided by the plugin.
 *
 */

struct asic_plugin_interface {

    int (*set_port_qos_cfg)(struct ofproto *ofproto,
                            void *aux,
                            const struct qos_port_settings *settings);
    int (*set_cos_map)(struct ofproto *ofproto,
                        const void *aux,
                        const struct cos_map_settings *settings);
    int (*set_dscp_map)(struct ofproto *ofproto,
                        void *aux,
                        const struct dscp_map_settings *settings);
};

#endif /*__ASIC_PLUGIN_H__*/
