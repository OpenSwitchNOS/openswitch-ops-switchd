/* Copyright (C) 2016 Hewlett Packard Enterprise Development LP
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

#ifndef ACL_LOG_H
#define ACL_LOG_H

#include <stdint.h>

#include "ops-cls-asic-plugin.h"
#include "uuid.h"

#define ACL_LOG_INGRESS_PORT   0x00000001 /**< Indicates the ingress_port field contains valid data */
#define ACL_LOG_EGRESS_PORT    0x00000002 /**< Indicates the egress_port field contains valid data */
#define ACL_LOG_INGRESS_VLAN   0x00000004 /**< Indicates the ingress_vlan field contains valid data */
#define ACL_LOG_EGRESS_VLAN    0x00000008 /**< Indicates the egress_vlan field contains valid data */
#define ACL_LOG_NODE           0x00000010 /**< Indicates the node field contains valid data */
#define ACL_LOG_IN_COS         0x00000020 /**< Indicates the in_cos field contains valid data */
#define ACL_LOG_ENTRY_NUM      0x00000040 /**< Indicates the entry_num field contains valid data */
#define ACL_LOG_LIST_TYPE      0x00000080 /**< Indicates the list_type field contains valid data */
#define ACL_LOG_LIST_NAME      0x00000100 /**< Indicates the list_name field contains valid data */
#define ACL_LOG_LIST_ID        0x00000200 /**< Indicates the list_id field contains valid data */

struct acl_log_info {
   /**< Data needed from the ASIC */
   uint32_t    valid_fields; /**< Some ASICs may not provide all of the fields
                               in this struct. Bits in this member indicate
                               which other members of the struct actually have
                               valid values. The bit definitions are given by
                               the ACL_LOG_* defines. */
   uint32_t    ingress_port; /**< The port that the packet ingresses on. */
   uint32_t    egress_port;  /**< The destination port (if available),
                               generally for unicast packets. */
   uint16_t    ingress_vlan; /**< The ID of the VLAN that the packet ingresses
                               on. */
   uint16_t    egress_vlan;  /**< The ID of the VLAN that the packet egresses
                               on. This will be different from the ingress VLAN
                               only for routed packets. */
   uint8_t     node;         /**< The node/ASIC number that received the
                               packet. */
   uint8_t     in_cos;       /**< The COS of the packet on ingress. */
   /**< Information about the ACE that the packet matched */
   uint32_t    entry_num;    /**< The entry number of the ACL that the packet
                               matched. Note that this is the index into the
                               list of ACE's rather than the ACE sequence
                               number that is exposed in the config. The PI
                               code is responsible for translating the
                               entry_num into a sequence number. */
   enum ops_cls_type list_type; /**< The ACL type, e.g., IPv4. */
   char        list_name[64 + 1]; /**< The name of the ACL. */
   struct uuid list_id;      /**< The UUID of the ACL. */
   /**< Packet data including the header */
   uint16_t    total_pkt_len;  /**< The size of the packet received */
   uint16_t    pkt_buffer_len; /**< The number of packet bytes in the data
                                   buffer */
   uint8_t     pkt_data[256]; /**< The beginning of the packet including the
                                header. */
};

/**
 * The main loop of some processes, such as the main loop of switchd, will wake
 * up when the value of a registered seq struct changes. The seq struct may be
 * referred to as a sequence number (it gets monotonically incremented) in
 * other parts of the code, but to avoid ambiguity, it is referred to as a seq
 * struct here.  This function returns a pointer to the seq struct that is used
 * for signaling that an ACL logging packet has been received and is ready for
 * processing.
 *
 * @return The ACL logging seq struct.
 *
 */
struct seq *acl_log_pktrx_seq_get(void);

/**
 * This function returns the information about a packet received for ACL
 * logging. The function will not return information for the same packet twice.
 * The information is returned by value to avoid potential problems with parts
 * of a struct being written while others are being read.
 *
 * @param pkt_info_to_get A pointer to an acl_log_info struct instance into
 *                        which information about a received packet will be
 *                        put.
 *
 */
void acl_log_pkt_data_get(struct acl_log_info *pkt_info_to_get);

/**
 * This function accepts information about a packet received for ACL
 * logging.
 *
 * @param new_pkt A pointer to available information about a received packet.
 *
 */
void acl_log_pkt_data_set(struct acl_log_info *new_pkt);

#endif /* ACL_LOG_H */
