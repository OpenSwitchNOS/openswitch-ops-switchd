
/*
 * Copyright (C) 2016 Hewlett-Packard Development Company, L.P.
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

#ifndef __PLUGIN_EXTENSIONS_H__
#define __PLUGIN_EXTENSIONS_H__ 1

/**
 * Plugin Extensions API
 *
 *  SwitchD Plugin Infrastructure includes two types of plugins:
 *
 * - Feature plugins: are plugins that implement feature code that is
 *   indenpendent of the asic. A feature plugin can export public functions for
 *   other plugins or the main switchd code to use. A feature plugin will define
 *   its plublic interface with a major and minor number for versioning. The
 *   plugin infrastructure will provide methods to find and access the feature
 *   plugin interfaces; it will also validate that the requested major and minor
 *   numbers against the feature plugin public interface.
 *
 * plugin-extensions.h expose the following API:
 *
 * int register_plugin_extension (
 *                         const char plugin_name,
 *                         struct plugin_extension_interface *new_extension);
 * int find_plugin_extension (
 *                         const char plugin_name,
 *                         unsigned int mayor, unsigned int minor,
 *                         void **plugin_interface);
 * int unregister_plugin_extension (const char *plugin_name);
 *
 *    - register_plugin_extension: registers an extension interface with a given
 *      plugin_name. If the extension is registered successfully 0 is returned
 *      otherwise EINVAL is returned. A registration can fail, among other
 *      reasons, if there is already another extension registered with the same
 *      plugin_name.
 *
 *    - find_plugin_extension: searches for a plugin extension with the given
 *      plugin_name, major and minor numbers, if one is found 0 is returned and
 *      the interface parameter gets the interface pointer, EINVAL is returned
 *      otherwise. The requested major and minor number are validated against
 *      the registered feature plugin interface.
 *
 *    - unregister_plugin_extension: Unregistration of existing plugins, could
 *      be called from the plugin itself to delete its interface from the hash
 *      table.
 *
 *  Versioning
 *
 *  The guard against ABI breakage the following guidelines must be followed:
 *     - A Plugin will export via their public header their plugin name, major
 *       and minor numbers.
 *     - When another plugin is compiled to use its interface it will be
 *       compiled against the exported plugin name, mayor and minor numbers.
 *     - The interface minor number is increased if more items are added to the
 *       end of the interface structure.
 *     - No items can be added in the middle of the interface structure.
 *     - The interface mayor number is increased if any parameters of existing
 *       functions are modified.
 *     - The find_plugin_extension will enforce proper versioning, if no
 *       compatible match is found for the given parameters then a null result
 *       is returned.
 *
 *  Examples:
 *
 *     - Code that got compiled against a plugin version 1.2 can use an
 *       interface from a plugin running 1.3, it will just not have any
 *       visibility for the new items.
 *     - Code that got compiled against a plugin version 1.2 can not use an
 *       interface from a plugin running 1.1, as it doesn't have all the items
 *       it needs.
 *     - Code that got compiled against a plugin version 2.X can only use other
 *       plugins compiled with 2.X (and the same minor checking is also applied
 *       after that).
 **/

#include <stdint.h>
#include <stdbool.h>

/**
 @struct plugin_extension_interface
 @brief  Plugin interface structure, every plugin should register
         its own interface with pointers to internal functions.
*/
struct plugin_extension_interface {
    const char* plugin_name; /**< Key for the hash interface */
    int major; /**< Major number to check plugins version */
    int minor; /**< Minor number to check plugins version */
    void *plugin_interface; /**< Start of exported plugin functions */
};

/**
    @fn register_plugin_extension
    @brief  Registration of new plugins, should be called from the plugin
    itself inside <plugin>_init function to register the plugin's interface.
    @param[in] new_extension The pointer to the plugin's interface
                                    structure.
                                    The caller provides a struct pointer with
                                    the plugin infrastructure.
 */
int register_plugin_extension (struct plugin_extension_interface *new_extension);

/**
    @fn unregister_plugin_extension
    @brief  Unregistration of existing plugins, could be called from the plugin
    itself to delete its interface from the hash table
    @param[in] plugin_name The key for the hash table to find the respective
                           plugin interface
    @return 0 if success, errno value otherwise.
 */
int unregister_plugin_extension (const char *plugin_name);

/**
    @fn find_plugin_extension
    @brief  Lookup for registered interfaces, could be called either from a
    plugin or the main switchd code.
    @param[in] plugin_name The key for the hash table to find the respective
                           plugin interface.
    @param[in] major The major value for the plugin expected version.
    @param[in] minr  The minor value for the plugin expected version.
    @param[out] plugin_interface Returns the interface, NULL if not found.
    @return 0 if success, errno value otherwise.
 */
int find_plugin_extension (const char *plugin_name, int major, int minor,
                           struct plugin_extension_interface **plugin_interface);

#endif /*__plugin_EXTENSIONS_H__*/
