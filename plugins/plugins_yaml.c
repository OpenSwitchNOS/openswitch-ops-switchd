
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

#include <config.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ltdl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include "list.h"
#include "yaml.h"
#include "openvswitch/vlog.h"
#include "plugins_yaml.h"

VLOG_DEFINE_THIS_MODULE(plugins_yaml);

/* This file is based on sysd_util.c file from ops-sysd module for yaml usage.
 *
 * Yaml files path depends on manufacturer and product names.
 * To access yaml config files is necesary to generate the path
 * dynamically using dmidecode command to get the hardware information.
 *
 * Following functions are based on sysd_util.c respective ones:
 *    dmidecode_exists
 *    get_sys_cmd_out
 *    get_manuf_and_prodname
 */

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)
#define DMIDECODE_NAME "dmidecode"
#define GENERIC_X86_MANUFACTURER "Generic-x86"
#define GENERIC_X86_PRODUCT_NAME "X86-64"
#define MAX_CMD_LENGHT 50
#define YAML_PATH_LENGHT 100

/* Determine whether the host system supports dmidecode command or not */
static int
dmidecode_exists(char *cmd_path)
{
    char *paths[] = {"/usr/sbin", "/sbin", "/bin", "/usr/bin"};
    static char buf[50];
    unsigned int i = 0;
    struct stat sbuf;

    /* Look for "dmidecode" command */
    for (i = 0; i < sizeof(paths); i++) {
        snprintf(buf, sizeof(buf), "%s/%s", paths[i], DMIDECODE_NAME);
        if ((stat(buf, &sbuf) == 0) && (sbuf.st_mode & S_IXUSR)) {
            strncpy(cmd_path, buf, sizeof(buf));
            return 0;
        }
    }
    return EINVAL;
}

/* Execute commands in host system */
static void
get_sys_cmd_out(char *cmd, char **output)
{
    FILE   *fd = NULL;
    char   *buf = NULL;
    size_t nbytes = 0, size = 0;

    *output = NULL;
    fd = popen(cmd, "r");
    if (fd == (FILE *) NULL) {
        VLOG_ERR("Popen failed for %s: Error: %s", cmd, ovs_strerror(errno));
        return;
    }

    while (1) {
        buf = NULL;
        size = 0;
        nbytes = getline(&buf, &size, fd);
        if (nbytes <= 0) {
            VLOG_ERR("Failed to parse output. rc=%s", ovs_strerror(errno));
            return;
        }

        /* Ignore the comments that starts with '#'. */
        if (buf[0] == '#') {
            continue;
        } else if (buf[0] != '\0') {
            /* Return the buffer, caller will free the buffer. */
            buf[nbytes-1] = '\0';
            *output = buf;
            break;
        }
        /* getline allocates buffer, it should be freed. */
        free(buf);
    }
    pclose(fd);
}

/* Obtain host system's manufacturer and product names */
static int
get_manuf_and_prodname(char *cmd_path, char **manufacturer, char **product_name)
{
    char dmid_cmd[256];

    snprintf(dmid_cmd, sizeof(dmid_cmd), "%s -s %s", cmd_path, "system-manufacturer");
    get_sys_cmd_out(dmid_cmd, manufacturer);

    if (*manufacturer == NULL) {
        VLOG_ERR("Unable to get manufacturer name.");
        goto error;
    }

    snprintf(dmid_cmd, sizeof(dmid_cmd), "%s -s %s", cmd_path, "system-product-name");
    get_sys_cmd_out(dmid_cmd, product_name);

    if (*product_name == NULL){
        VLOG_ERR("Unable to get product name.");
        goto error;
    }

    return 0;

 error:
    return EINVAL;
}

static int
concat_path(char **file_path, char *manuf, char *product)
{
    int hw_desc_len;

    hw_desc_len = strlen(manuf) + strlen(product) + YAML_PATH_LENGHT;
    *file_path = xmalloc(hw_desc_len);
    if (*file_path == NULL) {
        VLOG_ERR("Unable allocate memory for file path");
        goto error;
    }
    snprintf(*file_path, hw_desc_len,
    STRINGIZE_VALUE_OF(YAML_PATH)"/%s/%s/plugins.yaml",
    manuf, product);
    return 0;

 error:
    return EINVAL;
}

static int
open_yaml_file(FILE **fh)
{
    char cmd_path[MAX_CMD_LENGHT];
    char *manufacturer = NULL;
    char *product_name = NULL;
    char *hw_desc_dir;

    /* Run dmidecode command (if it exists) to get system info. */
    memset(cmd_path, 0, sizeof(cmd_path));
    if (dmidecode_exists(cmd_path)) {
        VLOG_ERR("Unable to find dmidecode cmd");
        goto error;
    }

    get_manuf_and_prodname(cmd_path, &manufacturer, &product_name);
    if ((manufacturer == NULL) || (product_name == NULL)) {
        VLOG_ERR("Hardware information not available");
        goto error;
    }

    if (concat_path(&hw_desc_dir, manufacturer, product_name) != 0) {
        goto error;
    }
    VLOG_DBG("Location to HW descrptor files: %s", hw_desc_dir);
    *fh = fopen(hw_desc_dir, "r");

    if(*fh == NULL) {
        VLOG_DBG("Invalid descriptor path, trying sim path");
        manufacturer = strdup(GENERIC_X86_MANUFACTURER);
        product_name = strdup(GENERIC_X86_PRODUCT_NAME);
        if (concat_path(&hw_desc_dir, manufacturer, product_name) != 0) {
            goto error;
        }
        VLOG_DBG("Location to HW descrptor files: %s", hw_desc_dir);
        if ((*fh = fopen(hw_desc_dir, "r")) == NULL ) {
            goto error;
        }
    }
    return 0;

 error:
    return EINVAL;
}

struct ovs_list*
get_yaml_plugins(void)
{
    FILE *fh;
    yaml_parser_t parser;
    yaml_token_t  token;
    struct ovs_list *p_list;

    if(!yaml_parser_initialize(&parser)){
        VLOG_ERR("Failed to initialize parser");
        goto error;
    }
    if (open_yaml_file(&fh) != 0){
        VLOG_INFO("File yaml.plugins not found, using default initialization");
        goto error;
    }

    yaml_parser_set_input_file(&parser, fh);
    p_list = (struct ovs_list *) xmalloc (sizeof(struct ovs_list));
    if (!p_list) {
        VLOG_ERR("Unable to malloc list node");
        goto error;
    }
    list_init(p_list);

    do {
        struct list_node *p_node;
        size_t len = 0;
        yaml_parser_scan(&parser, &token);
        switch(token.type) {
            case YAML_SCALAR_TOKEN:

                VLOG_DBG("Plugin name %s", (char *)token.data.scalar.value);
                p_node = (struct list_node *) xmalloc (sizeof(struct list_node));
                if (!p_node) {
                    goto error_list;
                }

                len = strlen((char *)token.data.scalar.value) + 1;
                p_node->name = (char *) xmalloc (len);
                if (!p_node->name) {
                    free(p_node);
                    goto error_list;
                }

                memcpy(p_node->name, token.data.scalar.value, len);
                list_push_back(p_list, &p_node->node);
                break;

            case YAML_STREAM_START_TOKEN:
            case YAML_STREAM_END_TOKEN:
            case YAML_KEY_TOKEN:
            case YAML_VALUE_TOKEN:
            case YAML_BLOCK_SEQUENCE_START_TOKEN:
            case YAML_BLOCK_ENTRY_TOKEN:
            case YAML_BLOCK_END_TOKEN:
            case YAML_BLOCK_MAPPING_START_TOKEN:
            case YAML_VERSION_DIRECTIVE_TOKEN:
            case YAML_TAG_DIRECTIVE_TOKEN:
            case YAML_DOCUMENT_START_TOKEN:
            case YAML_DOCUMENT_END_TOKEN:
            case YAML_FLOW_SEQUENCE_START_TOKEN:
            case YAML_FLOW_SEQUENCE_END_TOKEN:
            case YAML_FLOW_MAPPING_START_TOKEN:
            case YAML_FLOW_MAPPING_END_TOKEN:
            case YAML_FLOW_ENTRY_TOKEN:
            case YAML_ALIAS_TOKEN:
            case YAML_ANCHOR_TOKEN:
            case YAML_TAG_TOKEN:
            case YAML_NO_TOKEN:

            default:
                VLOG_DBG("Got token of type %d", token.type);
        }
        if (token.type != YAML_STREAM_END_TOKEN) {
            yaml_token_delete(&token);
        }
    } while (token.type != YAML_STREAM_END_TOKEN);

    yaml_token_delete(&token);
    yaml_parser_delete(&parser);
    fclose(fh);
    return p_list;

 error_list:
    free(p_list);

 error:
    return NULL;
}

int
free_yaml_plugins(struct ovs_list *plugins_list) {
    struct list_node *l_node;
    struct ovs_list *ovs_node;
    while (!list_is_empty(plugins_list)) {
        ovs_node = list_pop_front(plugins_list);
        l_node = CONTAINER_OF(ovs_node, struct list_node, node);
        VLOG_DBG("Freeing plugin %s", l_node->name);
        free(l_node->name);
        free(l_node);
    }
    free(plugins_list);
}
