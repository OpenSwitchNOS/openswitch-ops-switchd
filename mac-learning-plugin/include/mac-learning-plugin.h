/* (c) Copyright 2016 Hewlett Packard Enterprise Development LP
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

#ifndef MAC_LEARNING_PLUGIN_H
#define MAC_LEARNING_PLUGIN_H 1

#include "ofproto/ofproto.h"
#include "hmap.h"

#define MAC_LEARNING_PLUGIN_INTERFACE_NAME "MAC_LEARNING_PLUGIN"
#define MAC_LEARNING_PLUGIN_INTERFACE_MAJOR 1
#define MAC_LEARNING_PLUGIN_INTERFACE_MINOR 0

/*
 * struct: mac_learning_plugin_interface
 *
 * This interface needs to hold the API function pointer definitions
 * so that it can be exposed.
 */
struct mac_learning_plugin_interface {
    void (*mac_learning_trigger_callback) (void);
};

/*
 * Buffer size for hmap for mac learning
 */
#define BUFFER_SIZE  16384

#define PORT_NAME_SIZE 16
#define MAC_SOURCE_SIZE 10  /* for OVSREC_MAC_FROM_* values */

/** Platform Independent (PI) layer L2 MAC Hash table data structs **/

/* MAC hash table node structure
 * Notes:
 *   Hash key: 32-bit hash-key generated from {mac+vlan} pair.
 *
 *   We only support a single bridge instance for now, hence this structure
 *      doesn't care which bridge a MAC entry is associated with. When multiple
 *      bridges are supported a new element 'bridge' needs to be added and
 *      have it part of hash-key calculation.
 *
 *   Conflict between OVSDB MAC table schema and PI MAC hash table:
 *      OVSDB MAC table schema's index key:{bridge,mac,vlan,from}
 *      PI MAC table's index key: {mac,vlan}
 *      Since we only support single instance of bridge, not having bridge
 *          in PI table's key is OK.
 *      Ideally "from" should be removed from OVSDB MAC table schema's index
 *          and have public API available in ovs_util repo, that can be used
 *          by any other repo, to check for conflicts before making changes to
 *          OVSDB MAC table. After that, PI MAC table could simply be a
 *          reflection of the OVSDB table.
 *      Until we have that in place, each update from OVSDB needs to be checked
 *          for possible conflicts.
 */
struct mac_hash_table_entry {
    struct hmap_node   mac_hash_table_node; /* In 'mac_hash_table' */
    struct eth_addr    mac;
    uint16_t           vlan;
    char               dest[PORT_NAME_SIZE];
    char               from[MAC_SOURCE_SIZE]; /* source from where entry is
                                               learned (dynamic, hw-vtep etc) */
    unsigned int       idl_seqno;       /* last seen idl seq# for mark/sweep
                                           operation to identify deleted rows */
};

/* PI MAC Hash table */
static struct hmap mac_hash_table = HMAP_INITIALIZER(&mac_hash_table);

/** PD/PI communication data structs for L2 MAC table changes **/
/* Notes:
 *   With every change in L2 MAC table, coming from OVSDB, PI notifies
 *      PD about them, provided PD has registered update_l2_mac_table()
 *      via ASIC plug-in registration.
 */

/* New type to define how entry has changed */
typedef enum {
    MAC_TBL_ACTION_UNDEF,   /* undefined action */
    MAC_TBL_ADD,            /* add MAC table entry */
    MAC_TBL_DELETE,         /* delete MAC table entry */
    MAC_TBL_UPDATE          /* update MAC table entry */
} mac_table_action;

/* Updated MAC List node structure */
struct l2_mac_tbl_update_entry {
    struct ovs_list node;                 /* In 'struct ovs_list mac_list' */
    struct eth_addr mac;                  /* MAC address */
    uint16_t vlan;                        /* VLAN ID */
    mac_table_action action;              /* MAC table entry update action */
    char port_name[PORT_NAME_SIZE];       /* Destination interface */
};

/*
 * Mac learning
 */
typedef enum mac_event_ {
    MLEARN_UNDEFINED, /* undefined event */
    MLEARN_ADD,       /* add mac learn event */
    MLEARN_DEL,       /* delete mac learn event */
} mac_event;

struct mlearn_hmap_node {
    struct hmap_node hmap_node;     /* hmap node */
    int vlan;                       /* VLAN */
    int port;                       /* port_id */
    struct eth_addr mac;            /* MAC address */
    mac_event oper;                 /* action */
    int hw_unit;                    /* hw_unit */
    char port_name[PORT_NAME_SIZE]; /* Port name */
};

struct mlearn_hmap_node_buffer {
    int size;                                   /* max. size of this hmap */
    int actual_size;                            /* current size of hmap */
    struct mlearn_hmap_node nodes[BUFFER_SIZE]; /* statically allocated memory buffer */
};

struct mlearn_hmap {
    struct hmap table;                     /* hmap of (mlearn_hmap_node)*/
    struct mlearn_hmap_node_buffer buffer; /* buffer */
};

#endif /* mac-learning-plugin.h */
