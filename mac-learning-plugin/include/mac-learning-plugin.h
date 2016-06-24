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
#include "ops-utils.h"

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

/* mac_flush_params options */
enum mac_flush_options {
    L2MAC_FLUSH_BY_VLAN,
    L2MAC_FLUSH_BY_PORT,
    L2MAC_FLUSH_BY_PORT_VLAN,
    L2MAC_FLUSH_BY_TRUNK,
    L2MAC_FLUSH_BY_TRUNK_VLAN,
    L2MAC_FLUSH_ALL
};

/* struct mac_flush_params flags bit-fields */
#define L2MAC_STATIC_MAC            0x1  /* Static MAC */
#define L2MAC_NO_CALLBACKS          0x2  /* Suppress notifications */

typedef struct mac_flush_params_s {
    int vlan;               /* MAC entries learned on this VLAN to be flushed */
    char port_name[PORT_NAME_SIZE]; /* MAC entries learned on this Port to be flushed */
    int tgid;               /* Trunk group ID. */
    enum mac_flush_options options;   /* L2MAC_FLUSH_xxx options */
    unsigned int flags; /**< L2MAC_xxx flags. */
}mac_flush_params_t;

typedef struct l2mac_addr_s {
    unsigned int flags;                 /* L2_xxx flags. */
    unsigned char mac[6];               /* 802.3 MAC address. */
    int vid;                            /* VLAN identifier. */
    char port_name[PORT_NAME_SIZE];     /* Port name */
}l2mac_addr_t;

typedef enum mac_event_ {
    MLEARN_UNDEFINED, /* undefined event */
    MLEARN_ADD,       /* add mac learn event */
    MLEARN_DEL,       /* delete mac learn event */
    MLEARN_MOVE,       /* mac move event */
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
