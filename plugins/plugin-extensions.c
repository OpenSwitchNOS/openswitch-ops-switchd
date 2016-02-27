
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "hash.h"
#include "shash.h"
#include "openvswitch/vlog.h"
#include "plugin-extensions.h"

VLOG_DEFINE_THIS_MODULE(plugin_extensions);

/**
    @fn plugin_extensions_init
    @brief  Initialization of the extensions hash table, needs to be run
    once before start the plugins registration.
    @return 0 if success, errno value otherwise.
 */
static int plugin_extensions_init(void);

static int init_done = 0;         /**< Holds the status boolean for hash_init */
static struct shash sh_extensions; /**< Main hash with the interfaces of plugins */

static int
plugin_extensions_init(void)
{
    if (init_done) {
        return 0;
    }
    shash_init(&sh_extensions);
    if (sh_extensions.map.one != 0) {
        VLOG_ERR("Hash initialization failed.");
        goto error;
    }
    init_done = 1;
    return 0;

error:
    return EPERM;
}

int
register_plugin_extension(struct plugin_extension_interface *new_extension)
{
    struct plugin_extension_interface *extension;

    if (plugin_extensions_init() != 0) {
        VLOG_ERR("Hash initialization error.");
        goto error;
    }

    if (new_extension == NULL) {
        VLOG_ERR("Cannot add extention with null plugin function.");
        goto error;
    }

    VLOG_INFO("Register plugin_name %s plugin_function %p.",
               new_extension->plugin_name, new_extension);
    extension = shash_find_data(&sh_extensions, new_extension->plugin_name);

    if (!extension) {
        extension = (struct plugin_extension_interface*)xmalloc(
                                     sizeof(struct plugin_extension_interface));

        extension->plugin_name = new_extension->plugin_name;
        extension->major = new_extension->major;
        extension->minor = new_extension->minor;
        extension->plugin_interface = new_extension->plugin_interface;
        if(!shash_add_once(&sh_extensions, new_extension->plugin_name,
                           extension)){
            free(extension);
            VLOG_ERR("Failed to add plugin into hash table.");
            goto error;
        }
        return 0;
    } else {
        VLOG_ERR("There is already an extension with the plugin_name [%s].",
                  new_extension->plugin_name);
        goto error;
    }

error:
    return EINVAL;
}

int
unregister_plugin_extension(const char *plugin_name)
{
    struct plugin_extension_interface *extension;
    extension = shash_find_data(&sh_extensions, plugin_name);

    if (extension) {
      if (!shash_find_and_delete(&sh_extensions, plugin_name)){
        VLOG_ERR("Unable to delete extension with plugin_name [%s].",
                  plugin_name);
        goto error;
      }
      /* release memory used by struct */
      free(extension);
      return 0;
    }

error:
    return EINVAL;
}

int
find_plugin_extension(const char *plugin_name, int major, int minor,
                          struct plugin_extension_interface **plugin_interface)
{
    struct plugin_extension_interface *extension;
    extension = shash_find_data(&sh_extensions, plugin_name);

    if (extension) {
        VLOG_INFO("Found plugin extension with plugin_name \
                 [%s] major [%d] minor [%d].",
                 extension->plugin_name, extension->major, extension->minor);
        /* found a registered extension, now do some sanity checks */

        if (major != extension->major) {
            VLOG_ERR("Found plugin extension major \
                      check fails. Extension has major [%d] requested \
                      major [%d].",
                      extension->major, major);
            goto error;
        }

        if (minor > extension->minor) {
            VLOG_ERR("Found plugin extension minor \
                      check fails. Extension has minor [%d] requested \
                      minor [%d].",
                      extension->minor, minor);
            goto error;
        }

        *plugin_interface = extension;
        return 0;
    } else {
        VLOG_ERR("Unable to find requested plugin \
                  extension with plugin_name [%s]", plugin_name);

        goto error;
    }

error:
    *plugin_interface = NULL;
    return EINVAL;
}
