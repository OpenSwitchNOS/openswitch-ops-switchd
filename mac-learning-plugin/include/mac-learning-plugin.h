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
