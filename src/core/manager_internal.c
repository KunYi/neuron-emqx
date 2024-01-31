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
#include <dlfcn.h>

#include "utils/log.h"
#include "json/neu_json_param.h"

#include "adapter.h"
#include "errcodes.h"

#include "adapter/adapter_internal.h"
#include "adapter/driver/driver_internal.h"
#include "base/msg_internal.h"

#include "manager_internal.h"
#include "storage.h"

/**
 * @brief Add a plugin to the specified manager.
 *
 * This function is a convenience wrapper for adding a plugin to the plugin manager
 * associated with the given manager. It delegates the actual plugin addition to
 * neu_plugin_manager_add.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param library Name of the plugin dynamic library to be added.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see neu_plugin_manager_add
 */
int neu_manager_add_plugin(neu_manager_t *manager, const char *library)
{
    // Delegate the plugin addition to the plugin manager
    return neu_plugin_manager_add(manager->plugin_manager, library);
}

/**
 * @brief Remove a plugin from the specified manager.
 *
 * This function is a convenience wrapper for removing a plugin from the plugin manager
 * associated with the given manager. It delegates the actual plugin removal to
 * neu_plugin_manager_del.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param plugin Name of the plugin to be removed.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see neu_plugin_manager_del
 */
int neu_manager_del_plugin(neu_manager_t *manager, const char *plugin)
{
    return neu_plugin_manager_del(manager->plugin_manager, plugin);
}

/**
 * @brief Get the list of plugins associated with the specified manager.
 *
 * This function retrieves the list of plugins managed by the plugin manager
 * associated with the given manager. It delegates the actual retrieval to
 * neu_plugin_manager_get.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @return A pointer to UT_array containing plugin names, or NULL on failure.
 * @see neu_plugin_manager_get
 */
UT_array *neu_manager_get_plugins(neu_manager_t *manager)
{
    return neu_plugin_manager_get(manager->plugin_manager);
}

/**
 * @brief Add a node to the specified manager.
 *
 * This function adds a node with the specified name, associated plugin, setting,
 * running state, and load state to the node manager associated with the given manager.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param node_name Name of the node to be added.
 * @param plugin_name Name of the associated plugin for the node.
 * @param setting Setting information for the node (can be NULL).
 * @param state Running state of the node.
 * @param load Flag indicating whether to load the node.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 */
int neu_manager_add_node(neu_manager_t *manager, const char *node_name,
                         const char *plugin_name, const char *setting,
                         neu_node_running_state_e state, bool load)
{
    neu_adapter_t *       adapter      = NULL;
    neu_plugin_instance_t instance     = { 0 };
    neu_adapter_info_t    adapter_info = {
        .name = node_name,
    };
    neu_resp_plugin_info_t info = { 0 };
    int                    ret =
        neu_plugin_manager_find(manager->plugin_manager, plugin_name, &info);

    if (ret != 0) {
        return NEU_ERR_LIBRARY_NOT_FOUND;
    }

    if (info.single) {
        return NEU_ERR_LIBRARY_NOT_ALLOW_CREATE_INSTANCE;
    }

    adapter = neu_node_manager_find(manager->node_manager, node_name);
    if (adapter != NULL) {
        return NEU_ERR_NODE_EXIST;
    }

    ret = neu_plugin_manager_create_instance(manager->plugin_manager, info.name,
                                             &instance);
    if (ret != 0) {
        return NEU_ERR_LIBRARY_FAILED_TO_OPEN;
    }
    adapter_info.handle = instance.handle;
    adapter_info.module = instance.module;

    adapter = neu_adapter_create(&adapter_info, load);
    if (adapter == NULL) {
        return neu_adapter_error();
    }
    neu_node_manager_add(manager->node_manager, adapter);
    neu_adapter_init(adapter, state);

    if (NULL != setting &&
        0 != (ret = neu_adapter_set_setting(adapter, setting))) {
        neu_node_manager_del(manager->node_manager, node_name);
        neu_adapter_uninit(adapter);
        neu_adapter_destroy(adapter);
        return ret;
    }

    return NEU_ERR_SUCCESS;
}

/**
 * @brief Remove a node from the specified manager.
 *
 * This function removes a node with the specified name from the node manager
 * associated with the given manager.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param node_name Name of the node to be removed.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 */
int neu_manager_del_node(neu_manager_t *manager, const char *node_name)
{
    neu_adapter_t *adapter =
        neu_node_manager_find(manager->node_manager, node_name);

    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    neu_adapter_destroy(adapter);
    neu_subscribe_manager_remove(manager->subscribe_manager, node_name, NULL);
    neu_node_manager_del(manager->node_manager, node_name);
    return NEU_ERR_SUCCESS;
}

/**
 * @brief Get the list of nodes associated with the specified manager.
 *
 * This function retrieves the list of nodes managed by the node manager
 * associated with the given manager. It delegates the actual retrieval to
 * neu_node_manager_filter.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param type Type of nodes to filter (0 for all types).
 * @param plugin Name of the associated plugin for filtering (can be NULL).
 * @param node Name of the node for filtering (can be NULL).
 * @return A pointer to UT_array containing node names, or NULL on failure.
 * @see neu_node_manager_filter
 */
UT_array *neu_manager_get_nodes(neu_manager_t *manager, int type,
                                const char *plugin, const char *node)
{
    return neu_node_manager_filter(manager->node_manager, type, plugin, node);
}

/**
 * @brief Update the name of a node associated with the specified manager.
 *
 * This function updates the name of a node with the specified name to a new name.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param node Current name of the node to be updated.
 * @param new_name New name for the node.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 */
int neu_manager_update_node_name(neu_manager_t *manager, const char *node,
                                 const char *new_name)
{
    int ret = 0;
    if (neu_node_manager_is_driver(manager->node_manager, node)) {
        ret = neu_subscribe_manager_update_driver_name(
            manager->subscribe_manager, node, new_name);
    } else {
        ret = neu_subscribe_manager_update_app_name(manager->subscribe_manager,
                                                    node, new_name);
    }
    if (0 == ret) {
        ret =
            neu_node_manager_update_name(manager->node_manager, node, new_name);
    }
    return ret;
}

/**
 * @brief Update the name of a group associated with the specified manager.
 *
 * This function updates the name of a group with the specified name to a new name.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param driver Name of the driver to which the group belongs.
 * @param group Current name of the group to be updated.
 * @param new_name New name for the group.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 */
int neu_manager_update_group_name(neu_manager_t *manager, const char *driver,
                                  const char *group, const char *new_name)
{
    return neu_subscribe_manager_update_group_name(manager->subscribe_manager,
                                                   driver, group, new_name);
}

/**
 * @brief Allocate and create a new plugin instance for the specified plugin.
 *
 * This function allocates memory for a new neu_plugin_instance_t structure and
 * creates an instance of the specified plugin using neu_plugin_manager_create_instance.
 *
 * @param plugin_manager Pointer to the neu_plugin_manager_t structure.
 * @param plugin Name of the plugin for which to create an instance.
 * @return A pointer to the newly created plugin instance, or NULL on failure.
 * @see neu_plugin_manager_create_instance
 */
static inline neu_plugin_instance_t *
new_plugin_instance(neu_plugin_manager_t *plugin_manager, const char *plugin)
{
    neu_plugin_instance_t *inst = calloc(1, sizeof(*inst));
    if (NULL == inst) {
        return NULL;
    }

    if (0 != neu_plugin_manager_create_instance(plugin_manager, plugin, inst)) {
        free(inst);
        return NULL;
    }

    return inst;
}

/**
 * @brief Free the resources associated with the specified plugin instance.
 *
 * This function releases resources associated with a previously allocated
 * neu_plugin_instance_t structure, including closing the dynamic library handle.
 *
 * @param inst Pointer to the neu_plugin_instance_t structure to be freed.
 */
static inline void free_plugin_instance(neu_plugin_instance_t *inst)
{
    if (inst) {
        dlclose(inst->handle);
        free(inst);
    }
}

/**
 * @brief Get the list of driver groups associated with the specified manager.
 *
 * This function retrieves the list of driver groups managed by the node manager
 * associated with the given manager. It iterates over the drivers, gets their
 * associated groups, and creates an array of neu_resp_driver_group_info_t containing
 * information about each driver group.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @return A pointer to UT_array containing information about driver groups,
 *         or NULL on failure.
 */
UT_array *neu_manager_get_driver_group(neu_manager_t *manager)
{
    UT_array *drivers =
        neu_node_manager_get(manager->node_manager, NEU_NA_TYPE_DRIVER);
    UT_array *driver_groups = NULL;
    UT_icd    icd = { sizeof(neu_resp_driver_group_info_t), NULL, NULL, NULL };

    utarray_new(driver_groups, &icd);

    utarray_foreach(drivers, neu_resp_node_info_t *, driver)
    {
        neu_adapter_t *adapter =
            neu_node_manager_find(manager->node_manager, driver->node);
        UT_array *groups =
            neu_adapter_driver_get_group((neu_adapter_driver_t *) adapter);

        utarray_foreach(groups, neu_resp_group_info_t *, g)
        {
            neu_resp_driver_group_info_t dg = { 0 };

            strcpy(dg.driver, driver->node);
            strcpy(dg.group, g->name);
            dg.interval  = g->interval;
            dg.tag_count = g->tag_count;

            utarray_push_back(driver_groups, &dg);
        }

        utarray_free(groups);
    }

    utarray_free(drivers);

    return driver_groups;
}

/**
 * @brief Subscribe to a driver group with the specified parameters.
 *
 * This function subscribes to a driver group with the specified application,
 * driver, group, and parameters. It retrieves the address of the specified
 * application and delegates the subscription to neu_subscribe_manager_sub.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver to subscribe to.
 * @param group Name of the group to subscribe to.
 * @param params Additional subscription parameters.
 * @param app_port Output parameter to store the data port of the subscribing application.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see neu_subscribe_manager_sub
 */
static inline int manager_subscribe(neu_manager_t *manager, const char *app,
                                    const char *driver, const char *group,
                                    const char *params)
{
    int                ret  = NEU_ERR_SUCCESS;
    struct sockaddr_un addr = { 0 };
    neu_adapter_t *    adapter =
        neu_node_manager_find(manager->node_manager, driver);

    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    ret =
        neu_adapter_driver_group_exist((neu_adapter_driver_t *) adapter, group);
    if (ret != NEU_ERR_SUCCESS) {
        return ret;
    }

    addr = neu_node_manager_get_addr(manager->node_manager, app);
    return neu_subscribe_manager_sub(manager->subscribe_manager, driver, app,
                                     group, params, addr);
}


/**
 * @brief Subscribe to a driver group with the specified parameters.
 *
 * This function subscribes to a driver group with the specified application,
 * driver, group, and parameters. It retrieves the data port of the subscribing
 * application and guards against empty MQTT topic parameters. It then delegates
 * the subscription to manager_subscribe.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver to subscribe to.
 * @param group Name of the group to subscribe to.
 * @param params Additional subscription parameters.
 * @param app_port Output parameter to store the data port of the subscribing application.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see manager_subscribe
 */
int neu_manager_subscribe(neu_manager_t *manager, const char *app,
                          const char *driver, const char *group,
                          const char *params, uint16_t *app_port)
{
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, app);

    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    *app_port = neu_adapter_trans_data_port(adapter);

    // guard against empty mqtt topic parameter
    // this is not an elegant solution due to the current architecture
    if (params && 0 == strcmp(adapter->module->module_name, "MQTT")) {
        neu_json_elem_t elem = { .name = "topic", .t = NEU_JSON_STR };
        neu_parse_param(params, NULL, 1, &elem);
        if (elem.v.val_str && 0 == strlen(elem.v.val_str)) {
            free(elem.v.val_str);
            return NEU_ERR_MQTT_SUBSCRIBE_FAILURE;
        }
        free(elem.v.val_str);
    }

    if (NEU_NA_TYPE_APP != neu_adapter_get_type(adapter)) {
        return NEU_ERR_NODE_NOT_ALLOW_SUBSCRIBE;
    }

    return manager_subscribe(manager, app, driver, group, params);
}

/**
 * @brief Update subscription parameters for an existing subscription.
 *
 * This function updates the parameters for an existing subscription between the
 * specified application, driver, and group. It delegates the update to
 * neu_subscribe_manager_update_params.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver to update the subscription for.
 * @param group Name of the group to update the subscription for.
 * @param params Updated subscription parameters.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see neu_subscribe_manager_update_params
 */
int neu_manager_update_subscribe(neu_manager_t *manager, const char *app,
                                 const char *driver, const char *group,
                                 const char *params)
{
    return neu_subscribe_manager_update_params(manager->subscribe_manager, app,
                                               driver, group, params);
}

/**
 * @brief Send a subscription request to an application and a driver.
 *
 * This function sends a subscription request to the specified application and
 * driver with the given group, data port, and parameters. It constructs and sends
 * NEU_REQ_SUBSCRIBE_GROUP messages to both the application and driver.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver to subscribe to.
 * @param group Name of the group to subscribe to.
 * @param app_port Data port of the subscribing application.
 * @param params Subscription parameters.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 */
int neu_manager_send_subscribe(neu_manager_t *manager, const char *app,
                               const char *driver, const char *group,
                               uint16_t app_port, const char *params)
{
    neu_req_subscribe_t cmd = { 0 };
    strcpy(cmd.app, app);
    strcpy(cmd.driver, driver);
    strcpy(cmd.group, group);
    cmd.port = app_port;

    if (params && NULL == (cmd.params = strdup(params))) {
        return NEU_ERR_EINTERNAL;
    }

    neu_msg_t *msg = neu_msg_new(NEU_REQ_SUBSCRIBE_GROUP, NULL, &cmd);
    if (NULL == msg) {
        free(cmd.params);
        return NEU_ERR_EINTERNAL;
    }
    neu_reqresp_head_t *header = neu_msg_get_header(msg);
    strcpy(header->sender, "manager");
    strcpy(header->receiver, app);

    struct sockaddr_un addr =
        neu_node_manager_get_addr(manager->node_manager, app);

    int ret = neu_send_msg_to(manager->server_fd, &addr, msg);
    if (0 != ret) {
        nlog_warn("send %s to %s app failed",
                  neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), app);
        free(cmd.params);
        neu_msg_free(msg);
    } else {
        nlog_notice("send %s to %s app",
                    neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), app);
    }
    cmd.params = NULL;

    msg = neu_msg_new(NEU_REQ_SUBSCRIBE_GROUP, NULL, &cmd);
    if (NULL == msg) {
        return NEU_ERR_EINTERNAL;
    }
    header = neu_msg_get_header(msg);
    strcpy(header->sender, "manager");
    strcpy(header->receiver, driver);
    addr = neu_node_manager_get_addr(manager->node_manager, driver);

    ret = neu_send_msg_to(manager->server_fd, &addr, msg);
    if (0 != ret) {
        nlog_warn("send %s to %s driver failed",
                  neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), driver);
        neu_msg_free(msg);
    } else {
        nlog_notice("send %s to %s driver",
                    neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), driver);
    }

    return 0;
}

/**
 * @brief Unsubscribe from a driver group.
 *
 * This function unsubscribes the specified application from the specified driver group.
 * It delegates the unsubscription to neu_subscribe_manager_unsub.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver to unsubscribe from.
 * @param group Name of the group to unsubscribe from.
 * @return NEU_ERR_SUCCESS on success, or an error code if the operation fails.
 * @see neu_subscribe_manager_unsub
 */
int neu_manager_unsubscribe(neu_manager_t *manager, const char *app,
                            const char *driver, const char *group)
{
    return neu_subscribe_manager_unsub(manager->subscribe_manager, driver, app,
                                       group);
}

/**
 * @brief Get the list of subscribed groups for the specified application.
 *
 * This function retrieves the list of groups that the specified application is subscribed to.
 * It delegates the retrieval to neu_subscribe_manager_get.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @return A pointer to UT_array containing information about subscribed groups,
 *         or NULL on failure.
 * @see neu_subscribe_manager_get
 */
UT_array *neu_manager_get_sub_group(neu_manager_t *manager, const char *app)
{
    return neu_subscribe_manager_get(manager->subscribe_manager, app, NULL,
                                     NULL);
}

/**
 * @brief Get a deep copy of the list of subscribed groups for the specified application, driver, and group.
 *
 * This function retrieves a deep copy of the list of groups that the specified application is subscribed to,
 * considering the specific driver and group. It delegates the retrieval to neu_subscribe_manager_get
 * and ensures that the copied parameters are allocated separately.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param app Name of the subscribing application.
 * @param driver Name of the driver.
 * @param group Name of the group.
 * @return A deep copy of the UT_array containing information about subscribed groups,
 *         or NULL on failure.
 * @see neu_subscribe_manager_get
 */
UT_array *neu_manager_get_sub_group_deep_copy(neu_manager_t *manager,
                                              const char *   app,
                                              const char *   driver,
                                              const char *   group)
{
    UT_array *subs = neu_subscribe_manager_get(manager->subscribe_manager, app,
                                               driver, group);

    utarray_foreach(subs, neu_resp_subscribe_info_t *, sub)
    {
        if (sub->params) {
            sub->params = strdup(sub->params);
        }
    }

    // set vector element destructor
    subs->icd.dtor = (void (*)(void *)) neu_resp_subscribe_info_fini;

    return subs;
}

/**
 * @brief Get information about a node managed by the manager.
 *
 * This function retrieves information about a node with the specified name managed by the manager.
 * It looks up the node in the node manager and fills the provided neu_persist_node_info_t structure.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param name Name of the node.
 * @param info Pointer to the neu_persist_node_info_t structure to fill.
 * @return 0 on success, or -1 if the node is not found.
 */
int neu_manager_get_node_info(neu_manager_t *manager, const char *name,
                              neu_persist_node_info_t *info)
{
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, name);

    if (adapter != NULL) {
        info->name        = strdup(name);
        info->type        = adapter->module->type;
        info->plugin_name = strdup(adapter->module->module_name);
        info->state       = adapter->state;
        return 0;
    }

    return -1;
}

/**
 * @brief Delete a node from the manager.
 *
 * This function deletes the specified node from the manager, handling various cleanup tasks
 * such as unsubscribing, forwarding messages, and updating other components.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param node Name of the node to be deleted.
 * @return 0 on success, or an error code if the operation fails.
 */
static int del_node(neu_manager_t *manager, const char *node)
{
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, node);
    if (NULL == adapter) {
        return 0;
    }

    if (neu_node_manager_is_single(manager->node_manager, node)) {
        return NEU_ERR_NODE_NOT_ALLOW_DELETE;
    }

    if (neu_adapter_get_type(adapter) == NEU_NA_TYPE_APP) {
        UT_array *subscriptions = neu_subscribe_manager_get(
            manager->subscribe_manager, node, NULL, NULL);
        neu_subscribe_manager_unsub_all(manager->subscribe_manager, node);

        utarray_foreach(subscriptions, neu_resp_subscribe_info_t *, sub)
        {
            // NOTE: neu_req_unsubscribe_t and neu_resp_subscribe_info_t
            //       have compatible memory layout
            neu_msg_t *msg = neu_msg_new(NEU_REQ_UNSUBSCRIBE_GROUP, NULL, sub);
            if (NULL == msg) {
                break;
            }
            neu_reqresp_head_t *hd = neu_msg_get_header(msg);
            strcpy(hd->receiver, sub->driver);
            strcpy(hd->sender, "manager");
            forward_msg(manager, hd, hd->receiver);
        }
        utarray_free(subscriptions);
    }

    if (neu_adapter_get_type(adapter) == NEU_NA_TYPE_DRIVER) {
        neu_reqresp_node_deleted_t resp = { 0 };
        strcpy(resp.node, node);

        UT_array *apps = neu_subscribe_manager_find_by_driver(
            manager->subscribe_manager, node);
        utarray_foreach(apps, neu_app_subscribe_t *, app)
        {
            neu_msg_t *msg = neu_msg_new(NEU_REQRESP_NODE_DELETED, NULL, &resp);
            if (NULL == msg) {
                break;
            }
            neu_reqresp_head_t *hd = neu_msg_get_header(msg);
            strcpy(hd->receiver, app->app_name);
            strcpy(hd->sender, "manager");
            forward_msg(manager, hd, hd->receiver);
        }
        utarray_free(apps);
    }

    neu_adapter_uninit(adapter);
    neu_manager_del_node(manager, node);
    manager_storage_del_node(manager, node);
    return 0;
}

/**
 * @brief Add a driver node to the manager.
 *
 * This function adds a driver node to the manager, handling the necessary cleanup
 * and validation steps. It first attempts to delete any existing node with the same name
 * to replace it with the new driver node.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param driver Pointer to the neu_req_driver_t structure containing driver information.
 * @return 0 on success, or an error code if the operation fails.
 */
static inline int add_driver(neu_manager_t *manager, neu_req_driver_t *driver)
{
    int ret = del_node(manager, driver->node);
    if (0 != ret) {
        return ret;
    }

    ret = neu_manager_add_node(manager, driver->node, driver->plugin,
                               driver->setting, false, false);
    if (0 != ret) {
        return ret;
    }

    neu_adapter_t *adapter =
        neu_node_manager_find(manager->node_manager, driver->node);

    neu_resp_add_tag_t resp = { 0 };
    neu_req_add_gtag_t cmd  = {
        .groups  = driver->groups,
        .n_group = driver->n_group,
    };

    if (0 != neu_adapter_validate_gtags(adapter, &cmd, &resp) ||
        0 != neu_adapter_try_add_gtags(adapter, &cmd, &resp) ||
        0 != neu_adapter_add_gtags(adapter, &cmd, &resp)) {
        neu_adapter_uninit(adapter);
        neu_manager_del_node(manager, driver->node);
    }

    return resp.error;
}

/**
 * @brief Add an array of driver nodes to the manager.
 *
 * This function adds an array of driver nodes to the manager, handling the necessary
 * cleanup and validation steps. It iterates through the array and adds each driver node.
 *
 * @param manager Pointer to the neu_manager_t structure.
 * @param req Pointer to the neu_req_driver_array_t structure containing the array of drivers.
 * @return 0 on success, or an error code if the operation fails.
 */
int neu_manager_add_drivers(neu_manager_t *manager, neu_req_driver_array_t *req)
{
    int ret = 0;

    // fast check
    for (uint16_t i = 0; i < req->n_driver; ++i) {
        neu_resp_plugin_info_t info   = { 0 };
        neu_req_driver_t *     driver = &req->drivers[i];

        ret = neu_plugin_manager_find(manager->plugin_manager, driver->plugin,
                                      &info);

        if (ret != 0) {
            return NEU_ERR_LIBRARY_NOT_FOUND;
        }

        if (info.single) {
            return NEU_ERR_LIBRARY_NOT_ALLOW_CREATE_INSTANCE;
        }

        if (NEU_NA_TYPE_DRIVER != info.type) {
            return NEU_ERR_PLUGIN_TYPE_NOT_SUPPORT;
        }

        if (driver->n_group > NEU_GROUP_MAX_PER_NODE) {
            return NEU_ERR_GROUP_MAX_GROUPS;
        }
    }

    for (uint16_t i = 0; i < req->n_driver; ++i) {
        ret = add_driver(manager, &req->drivers[i]);
        if (0 != ret) {
            nlog_notice("add i:%" PRIu16 " driver:%s fail", i,
                        req->drivers[i].node);
            while (i-- > 0) {
                nlog_notice("rollback i:%" PRIu16 " driver:%s", i,
                            req->drivers[i].node);
                neu_adapter_t *adapter = neu_node_manager_find(
                    manager->node_manager, req->drivers[i].node);
                neu_adapter_uninit(adapter);
                neu_manager_del_node(manager, req->drivers[i].node);
            }
            nlog_error("fail to add %" PRIu16 " drivers", req->n_driver);
            break;
        }
        nlog_notice("add i:%" PRIu16 " driver:%s success", i,
                    req->drivers[i].node);
    }

    return ret;
}
