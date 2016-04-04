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
 * Defines those data structures that are used in the QoS API between ASIC
 * providers and platform-independent code.
 */

#ifndef QOS_ASIC_PROVIDER_H
#define QOS_ASIC_PROVIDER_H 1

#include "smap.h"
#include "ofproto/ofproto.h"

#ifdef  __cplusplus
extern "C" {
#endif


/** @def QOS_ASIC_PLUGIN_INTERFACE_NAME
 *  @brief asic plugin name definition
 */
#define QOS_ASIC_PLUGIN_INTERFACE_NAME     "QOS_ASIC_PLUGIN"

/** @def QOS_ASIC_PLUGIN_INTERFACE_MAJOR
 *  @brief plugin major version definition
 */
#define QOS_ASIC_PLUGIN_INTERFACE_MAJOR    1

/** @def QOS_ASIC_PLUGIN_INTERFACE_MINOR
 *  @brief plugin minor version definition
 */
#define QOS_ASIC_PLUGIN_INTERFACE_MINOR    1


/* in System or Port table, possible values in qos_config column */
enum qos_trust {
    QOS_TRUST_NONE = 0,
    QOS_TRUST_COS,
    QOS_TRUST_DSCP,
    QOS_TRUST_MAX /* Used for validation only! */
};

/* collection of parameters to set_port_qos_cfg API */
struct qos_port_settings {
    enum qos_trust  qos_trust;
    bool            cos_override_enable;
    bool            dscp_override_enable;
    uint8_t         cos_override_value;
    uint8_t         dscp_override_value;
    const struct smap *other_config;
};

/* in QoS_DSCP_Map or QoS_COS_Map, possible values for color column */
enum cos_color {
    COS_COLOR_GREEN = 0,
    COS_COLOR_YELLOW,
    COS_COLOR_RED,
    COS_COLOR_MAX
};

/* single row from QoS_DSCP_Map table */
struct dscp_map_entry {
    enum cos_color  color;
    int codepoint;
    int local_priority;
    int cos;
    struct smap *other_config;
};

/* 1 or more rows in QoS_DSCP_Map passed to set_dscp_map API */
struct dscp_map_settings {
    int n_entries;
    struct dscp_map_entry *entries;   /* array of 'struct dscp_map_entry' */
};

/* single row from QoS_COS_Map table */
struct cos_map_entry {
    enum cos_color color;
    int codepoint;
    int local_priority;
    struct smap *other_config;
};

/* 1 or more rows in QoS_COS_Map passed to set_cos_map API */
struct cos_map_settings {
    int n_entries;
    struct cos_map_entry *entries;   /* array of 'struct cos_map_entry' */
};

/* One or more local priority entries per schedule-profile entry. */
struct local_priority_entry {
    unsigned local_priority;  /* Number */
    /* TBD: ECN, CAP threshold, et.al. WRED parameters */
};

enum qos_queue_profile_mode {
    QUEUE_PROFILE_DEFAULT = 0,
    QUEUE_PROFILE_LOSSLESS,
    QUEUE_PROFILE_LOW_LATENCY,
    QUEUE_PROFILE_MAX /* Used for validation only! */
};

/* single queue-profile row (from Q_Profile->Q_Settings table) */
struct queue_profile_entry {
    unsigned queue;              /* queue number */
    int n_local_priorities;      /* length of local_priorities array, may be 0 */
    struct local_priority_entry **local_priorities; /* variable-length array of */
                                                    /* 'struct local_priority_entry' */
                                                    /* ptrs. May be NULL */
    enum qos_queue_profile_mode mode;
    struct smap *other_config;   /* pass-through from Q_Settings row */
    /* TBD: min & max shaping parameters */
};

/* 1 or more rows in Q_Profile passed to set_queue_profile API */
struct queue_profile_settings {
    int n_entries;
    struct queue_profile_entry **entries; /* variable-length array of */
                                          /* 'struct queue_profile_entry' */
                                          /* ptrs. May be NULL */
    struct smap *other_config;   /* pass-through from Q_Profile row */
};

enum schedule_algorithm {
    ALGORITHM_STRICT,
    ALGORITHM_DWRR,
    ALGORITHM_MAX
};

/* single schedule-profile row (from QoS->Queue table) */
struct schedule_profile_entry {
    unsigned queue;              /* queue number */
    enum schedule_algorithm algorithm; /* must have some scheduling algorithm */
    int weight;                /* weight, if queue type is WRR */
    struct smap *other_config; /* pass-through from Queue row */
};

/* 1 or more rows in QoS passed to set_schedule_profile API */
struct schedule_profile_settings {
    int     n_entries;
    struct schedule_profile_entry **entries; /* variable-length array of */
                                             /* 'struct schedule_profile_entry' */
                                             /* ptrs. May be NULL */
    /* TBD: scheduling type */
    struct smap *other_config;   /* pass-through from QoS row */
};

/** @struct qos_asic_plugin_interface
 *  @brief qos_asic_plugin_interface enforces the interface that an QOS_ASIC plugin must
 *  provide to be compatible with SwitchD Asic plugin infrastructure.
 *  When an external plugin attempts to register itself as an QOS_ASIC plugin, the
 *  code will validate that the interface provided meets the requirements for
 *  MAJOR and MINOR versions.
 *
 *  - The QOS_ASIC_PLUGIN_INTERFACE_NAME identifies the registered interface as an
 *  QOS_ASIC plugin. All asic plugins must use the same interface name. The plugin
 *  infrastructure will enforce that only one asic plugin can be registered at a
 *  time. Asic plugins from vendors will have different names but they will
 *  register the same interface name.
 *
 *  - The QOS_ASIC_PLUGIN_INTERFACE_MAJOR identifies any large change in the fields
 *  of struct qos_asic_plugin_interface that would break the ABI, so any extra
 *  fields added in the middle of previous fields, removal of previous fields
 *  would trigger a change in the MAJOR number.
 *
 *  - The QOS_ASIC_PLUGIN_INTERFACE_MINOR indentifies any incremental changes to the
 *  fields of struct qos_asic_plugin_interface that would not break the ABI but
 *  would just make the new fields unavailable to the older component.
 *
 *  For example if QOS_ASIC_PLUGIN_INTERFACE_MAJOR is 1 and
 *  QOS_ASIC_PLUGIN_INTERFACE_MINOR is 2, then a plugin can register itself as an
 *  asic plugin if the provided interface has a MAJOR=1 and MINOR>=2. This means
 *  that even if the plugin provides more functionality in the interface fields
 *  those would not be used by SwitchD. But if the plugin has a MAJOR=1 and
 *  MINOR=1 then it cannot be used as an asic plugin as SwitchD will see fields
 *  in the interface struct that are not provided by the plugin.
 *
 */

struct qos_asic_plugin_interface {

/*
 * set_port_qos_cfg
 *
 * configure several per-port QoS settings:
 * - trust
 * - cos map override
 * - dscp map override
 *
 * @param ofproto   struct ofproto that describes either a bridge or a VRF.
 * @param aux       pointer to struct port that is used to look up a
 *                  previously-added bundle
 * @param settings  pointer to qos_port_settings, describes how the port's
 *                  QOS should be configured.
 *
 * @return int      API status:
 *                  0               success
 *                  EOPNOTSUPP      this API not supported by this provider
 *                  other value     ASIC provider dependent error
 */
    int (*set_port_qos_cfg)(struct ofproto *ofproto,
                            void *aux,
                            const struct qos_port_settings *settings);

/*
 * set_cos_map
 *
 * configure one or more entries in the global, or per-port, COS map
 *
 * @param ofproto   struct ofproto that describes either a bridge or a VRF.
 * @param aux       pointer to struct port that is used to look up a
 *                  previously-added bundle.  If NULL, the global default
 *                  COS map should be programmed.
 * @param settings  pointer to cos_map_settings, describes how the
 *                  COS map should be configured.
 *
 * @return int      API status:
 *                  0               success
 *                  EOPNOTSUPP      this API not supported by this provider
 *                  other value     ASIC provider dependent error
 */
    int (*set_cos_map)(struct ofproto *ofproto,
                        void *aux,
                        const struct cos_map_settings *settings);

/*
 * set_dscp_map
 *
 * configure one or more entries in the global, or per-port, DSCP map
 *
 * @param ofproto   struct ofproto that describes either a bridge or a VRF.
 * @param aux       pointer to struct port that is used to look up a
 *                  previously-added bundle.  If NULL, the global default
 *                  DSCP map should be programmed.
 * @param settings  pointer to cos_map_settings, describes how the
 *                  DSCP map should be configured.
 *
 * @return int      API status:
 *                  0               success
 *                  EOPNOTSUPP      this API not supported by this provider
 *                  other value     ASIC provider dependent error
 */
    int (*set_dscp_map)(struct ofproto *ofproto,
                        void *aux,
                        const struct dscp_map_settings *settings);

/*
 * apply_qos_profile
 *
 * configure the global or per-port queue and schedule profiles
 *
 * @param ofproto       struct ofproto that describes either a bridge or a VRF.
 * @param aux           pointer to struct port that is used to look up a
 *                      previously-added bundle.  If NULL, the global default
 *                      queue & schedule profiles should be programmed.
 * @param s_settings    schedule profile
 * @param q_settings    queue profile
 *
 * @return int      API status:
 *                  0               success
 *                  EOPNOTSUPP      this API not supported by this provider
 *                  other value     ASIC provider dependent error
 */
    int (*apply_qos_profile)(struct ofproto *ofproto,
                             void *aux,
                             const struct schedule_profile_settings *s_settings,
                             const struct queue_profile_settings *q_settings);

};


#ifdef  __cplusplus
}
#endif

#endif /* qos_asic_provider.h */
