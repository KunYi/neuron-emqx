/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#ifndef NEURON_PLUGIN_H
#define NEURON_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "utils/utextend.h"
#include "utils/zlog.h"

#include "event/event.h"

#include "adapter.h"
#include "define.h"
#include "type.h"

#define NEURON_PLUGIN_VER_1_0 \
    NEU_VERSION(NEU_VERSION_MAJOR, NEU_VERSION_MINOR, NEU_VERSION_FIX)

#define NEU_PLUGIN_REGISTER_METRIC(plugin, name, init) \
    plugin->common.adapter_callbacks->register_metric( \
        plugin->common.adapter, name, name##_HELP, name##_TYPE, init)

#define NEU_PLUGIN_UPDATE_METRIC(plugin, name, val, grp)                    \
    plugin->common.adapter_callbacks->update_metric(plugin->common.adapter, \
                                                    name, val, grp)

extern int64_t global_timestamp;

/**
 * @brief Common structure shared among Neuron plugins.
 *
 * This structure defines common attributes for Neuron plugins, including a magic
 * number, adapter information, name, link state, log level, and logging category.
 */
typedef struct neu_plugin_common {
    uint32_t magic;                                     /**< Magic number for identification. */

    neu_adapter_t *            adapter;                 /**< Pointer to the adapter. */
    const adapter_callbacks_t *adapter_callbacks;       /**< Pointer to adapter callbacks. */
    char                       name[NEU_NODE_NAME_LEN]; /**< Name of the plugin. */

    neu_node_link_state_e link_state;                   /**< Link state of the plugin. */
    char                  log_level[NEU_LOG_LEVEL_LEN]; /**< Log level for the plugin. */

    zlog_category_t *log;                               /**< Logging category for the plugin. */
} neu_plugin_common_t;

/**
 * @brief Structure representing a Neuron plugin.
 */
typedef struct neu_plugin neu_plugin_t;

/**
 * @brief Structure representing a group of Neuron plugins.
 *
 * This structure holds information about a group of plugins, including the group name,
 * associated tags, user data, and a function for freeing the group.
 */
typedef struct neu_plugin_group neu_plugin_group_t;

/**
 * @brief Function pointer type for freeing a Neuron plugin group.
 * @param pgp Pointer to the plugin group to be freed.
 */
typedef void (*neu_plugin_group_free)(neu_plugin_group_t *pgp);

/**
 * @brief Structure representing a group of Neuron plugins.
 *
 * This structure holds information about a group of plugins, including the group name,
 * associated tags, user data, and a function for freeing the group.
 */
struct neu_plugin_group {
    char *    group_name;               /**< Name of the plugin group. */
    UT_array *tags;                     /**< Array of associated tags. */

    void *                user_data;    /**< User data associated with the group. */
    neu_plugin_group_free group_free;   /**< Function pointer for freeing the group. */
};

/**
 * @brief Function pointer type for validating a Neuron plugin tag.
 *
 * This function is used to validate a Neuron plugin tag.
 *
 * @param tag Pointer to the Neuron plugin tag to be validated.
 * @return Integer indicating validation result (0 for success, non-zero for failure).
 */
typedef int (*neu_plugin_tag_validator_t)(const neu_datatag_t *tag);

/**
 * @brief Structure representing a tag-value pair for a Neuron plugin.
 *
 * This structure holds information about a Neuron plugin tag and its associated value.
 */
typedef struct {
    neu_datatag_t *tag;     /**< Pointer to the Neuron plugin tag. */
    neu_value_u    value;   /**< Value associated with the Neuron plugin tag. */
} neu_plugin_tag_value_t;

/**
 * @brief Interface functions for a Neuron plugin.
 *
 * This structure defines a set of functions that a Neuron plugin must implement.
 * It includes functions for opening, closing, initializing, uninitializing, starting,
 * and stopping the plugin. Additionally, there are functions for handling settings,
 * making requests, and specific functions for different plugin types (e.g., driver).
 */
typedef struct neu_plugin_intf_funs {

    /**
     * @brief Function pointer to open a Neuron plugin.
     * @return A pointer to the opened plugin.
     */
    neu_plugin_t *(*open)(void);

    /**
     * @brief Function pointer to close a Neuron plugin.
     * @param plugin Pointer to the plugin to be closed.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*close)(neu_plugin_t *plugin);

    /**
     * @brief Function pointer to initialize a Neuron plugin.
     * @param plugin Pointer to the plugin to be initialized.
     * @param load Flag indicating whether to load the plugin.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*init)(neu_plugin_t *plugin, bool load);

    /**
     * @brief Function pointer to uninitialize a Neuron plugin.
     * @param plugin Pointer to the plugin to be uninitialized.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*uninit)(neu_plugin_t *plugin);

    /**
     * @brief Function pointer to start a Neuron plugin.
     * @param plugin Pointer to the plugin to be started.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*start)(neu_plugin_t *plugin);

    /**
     * @brief Function pointer to stop a Neuron plugin.
     * @param plugin Pointer to the plugin to be stopped.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*stop)(neu_plugin_t *plugin);

    /**
     * @brief Function pointer to handle setting configurations for a Neuron plugin.
     * @param plugin Pointer to the plugin.
     * @param setting Configuration setting to be applied.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*setting)(neu_plugin_t *plugin, const char *setting);

    /**
     * @brief Function pointer to handle requests for a Neuron plugin.
     * @param plugin Pointer to the plugin.
     * @param head Pointer to the request/response header.
     * @param data Pointer to the data associated with the request.
     * @return NEU_ERR_SUCCESS on success, an error code on failure.
     */
    int (*request)(neu_plugin_t *plugin, neu_reqresp_head_t *head, void *data);

    /**
     * @brief Union containing driver-specific functions for the plugin.
     */
    union {
        struct {
            /**
             * @brief Function pointer to validate a tag for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param tag Pointer to the data tag to be validated.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*validate_tag)(neu_plugin_t *plugin, neu_datatag_t *tag);

            /**
             * @brief Function pointer to handle group timer for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param group Pointer to the plugin group.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*group_timer)(neu_plugin_t *plugin, neu_plugin_group_t *group);

            /**
             * @brief Function pointer to handle group synchronization for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param group Pointer to the plugin group.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*group_sync)(neu_plugin_t *plugin, neu_plugin_group_t *group);

            /**
             * @brief Function pointer to write a tag for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param req Pointer to the request.
             * @param tag Pointer to the data tag.
             * @param value Value to be written to the tag.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*write_tag)(neu_plugin_t *plugin, void *req,
                             neu_datatag_t *tag, neu_value_u value);

            /**
             * @brief Function pointer to write multiple tags for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param req Pointer to the request.
             * @param tag_values Pointer to an array containing tag-value pairs.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*write_tags)(
                neu_plugin_t *plugin, void *req,
                UT_array *tag_values); // UT_array {neu_datatag_t, neu_value_u}

            /**
             * @brief Validator function pointer for data tags in a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param tags Pointer to an array of data tags.
             * @param n_tag Number of tags in the array.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            neu_plugin_tag_validator_t tag_validator;

            /**
             * @brief Function pointer to load tags from the database for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param group Group identifier for the tags.
             * @param tags Pointer to an array to store loaded tags.
             * @param n_tag Number of tags to load.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*load_tags)(
                neu_plugin_t *plugin, const char *group, neu_datatag_t *tags,
                int n_tag); // create tags using data from the database

            /**
             * @brief Function pointer to add tags using the API for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param group Group identifier for the tags.
             * @param tags Pointer to an array of tags to be added.
             * @param n_tag Number of tags to add.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*add_tags)(neu_plugin_t *plugin, const char *group,
                            neu_datatag_t *tags,
                            int            n_tag); // create tags by API
            /**
             * @brief Function pointer to delete tags for a driver plugin.
             * @param plugin Pointer to the plugin.
             * @param n_tag Number of tags to delete.
             * @return NEU_ERR_SUCCESS on success, an error code on failure.
             */
            int (*del_tags)(neu_plugin_t *plugin, int n_tag);
        } driver;
    };

} neu_plugin_intf_funs_t;

/**
 * @brief Structure representing a plugin module.
 *
 * This structure holds information about a plugin module, including its version,
 * schema, module name, descriptions in English and Chinese, interface functions,
 * node type, kind, display flag, singleton flag, singleton name (if applicable),
 * timer type, and tag cache type.
 */typedef struct neu_plugin_module {
    const uint32_t                version;          ///< Plugin module version.
    const char *                  schema;           ///< Plugin schema.
    const char *                  module_name;      ///< Name of the plugin module.
    const char *                  module_descr;     ///< Description of the plugin module (English).
    const char *                  module_descr_zh;  ///< Description of the plugin module (Chinese).
    const neu_plugin_intf_funs_t *intf_funs;        ///< Pointer to plugin interface functions.
    neu_node_type_e               type;             ///< Node type associated with the plugin module.
    neu_plugin_kind_e             kind;             ///< Kind of the plugin module.
    bool                          display;          ///< Flag indicating whether the plugin should be displayed.
    bool                          single;           ///< Flag indicating whether the plugin is a singleton.
    const char *                  single_name;      ///< Name of the singleton instance (if applicable).
    neu_event_timer_type_e        timer_type;       ///< Timer type associated with the plugin module.
    neu_tag_cache_type_e          cache_type;       ///< Tag cache type associated with the plugin module.
} neu_plugin_module_t;

inline static neu_plugin_common_t *
neu_plugin_to_plugin_common(neu_plugin_t *plugin)
{
    return (neu_plugin_common_t *) plugin;
}

/**
 * @brief when creating a node, initialize the common parameter in
 *        the plugin.
 *
 * @param[in] common refers to common parameter in plugins.
 */
void neu_plugin_common_init(neu_plugin_common_t *common);

/**
 * @brief Check the common parameter in the plugin.
 *
 * @param[in] plugin represents plugin information.
 * @return  Returns true if the check is correct,false otherwise.
 */
bool neu_plugin_common_check(neu_plugin_t *plugin);

/**
 * @brief Encapsulate the request,convert the request into a message and send.
 *
 * @param[in] plugin
 * @param[in] head the request header function.
 * @param[in] data that different request headers correspond to different data.
 *
 * @return 0 for successful message processing, non-0 for message processing
 * failure.
 */
int neu_plugin_op(neu_plugin_t *plugin, neu_reqresp_head_t head, void *data);

#ifdef __cplusplus
}
#endif

#endif
