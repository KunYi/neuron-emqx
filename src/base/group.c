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
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "define.h"
#include "errcodes.h"

#include "group.h"


/**
 * @struct tag_elem
 * @brief Structure representing a tag element within a group
 */
typedef struct tag_elem {
    char *name;              /**< Tag name */

    neu_datatag_t *tag;      /**< Pointer to the associated data tag */

    UT_hash_handle hh;       /**< Hash handle for UT hash table */
} tag_elem_t;

/**
 * @struct neu_group
 * @brief Structure representing a group
 */
struct neu_group {
    char *name;                  /**< Group name */

    tag_elem_t *tags;            /**< Hash table of tags in the group */
    uint32_t    interval;        /**< Update interval for the group */

    int64_t         timestamp;   /**< Timestamp of the last change in the group */
    pthread_mutex_t mtx;         /**< Mutex for thread safety */
};

static UT_array *to_array(tag_elem_t *tags);
static void      split_static_array(tag_elem_t *tags, UT_array **static_tags,
                                    UT_array **other_tags);
static void      update_timestamp(neu_group_t *group);


/**
 * @brief Creates a new Group
 * @param name Name of the group
 * @param interval Update interval for the group
 * @return Pointer to the newly created group
 */
neu_group_t *neu_group_new(const char *name, uint32_t interval)
{
    neu_group_t *group = calloc(1, sizeof(neu_group_t));

    group->name     = strdup(name);
    group->interval = interval;
    pthread_mutex_init(&group->mtx, NULL);

    return group;
}

/**
 * @brief Destroys a Group and its associated tags
 * @param group The group to be destroyed
 */
void neu_group_destroy(neu_group_t *group)
{
    tag_elem_t *el = NULL, *tmp = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_ITER(hh, group->tags, el, tmp)
    {
        HASH_DEL(group->tags, el);
        free(el->name);
        neu_tag_free(el->tag);
        free(el);
    }
    pthread_mutex_unlock(&group->mtx);

    pthread_mutex_destroy(&group->mtx);
    free(group->name);
    free(group);
}

/**
 * @brief Gets the name of a Group
 * @param group The group
 * @return Name of the group
 */
const char *neu_group_get_name(const neu_group_t *group)
{
    return group->name;
}

/**
 * @brief Sets the name of a Group
 * @param group The group
 * @param name New name for the group
 * @return NEU_ERR_SUCCESS on success, NEU_ERR_EINTERNAL on failure
 */
int neu_group_set_name(neu_group_t *group, const char *name)
{
    char *new_name = NULL;
    if (NULL == name || NULL == (new_name = strdup(name))) {
        return NEU_ERR_EINTERNAL;
    }

    free(group->name);
    group->name = new_name;
    return NEU_ERR_SUCCESS;
}

/**
 * @brief Gets the update interval of a Group
 * @param group The group
 * @return Update interval of the group
 */
uint32_t neu_group_get_interval(const neu_group_t *group)
{
    uint32_t interval = 0;

    interval = group->interval;

    return interval;
}

/**
 * @brief Sets the update interval of a Group
 * @param group The group
 * @param interval New update interval for the group
 */
void neu_group_set_interval(neu_group_t *group, uint32_t interval)
{
    group->interval = interval;
}

/**
 * @brief Updates the update interval of a Group and triggers a timestamp update
 * @param group The group
 * @param interval New update interval for the group
 * @return NEU_ERR_SUCCESS on success
 */
int neu_group_update(neu_group_t *group, uint32_t interval)
{
    if (group->interval != interval) {
        group->interval = interval;
        update_timestamp(group);
    }

    return NEU_ERR_SUCCESS;
}

/**
 * @brief Adds a tag to a Group
 * @param group The group
 * @param tag The tag to be added
 * @return NEU_ERR_SUCCESS on success, NEU_ERR_TAG_NAME_CONFLICT if the tag name already exists
 */
int neu_group_add_tag(neu_group_t *group, const neu_datatag_t *tag)
{
    tag_elem_t *el = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag->name, el);
    if (el != NULL) {
        pthread_mutex_unlock(&group->mtx);
        return NEU_ERR_TAG_NAME_CONFLICT;
    }

    el       = calloc(1, sizeof(tag_elem_t));
    el->name = strdup(tag->name);
    el->tag  = neu_tag_dup(tag);

    HASH_ADD_STR(group->tags, name, el);
    update_timestamp(group);
    pthread_mutex_unlock(&group->mtx);

    return NEU_ERR_SUCCESS;
}

/**
 * @brief Updates a tag in a Group
 * @param group The group
 * @param tag The tag with updated information
 * @return NEU_ERR_SUCCESS on success, NEU_ERR_TAG_NOT_EXIST if the tag doesn't exist
 */
int neu_group_update_tag(neu_group_t *group, const neu_datatag_t *tag)
{
    tag_elem_t *el  = NULL;
    int         ret = NEU_ERR_TAG_NOT_EXIST;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag->name, el);
    if (el != NULL) {
        neu_tag_copy(el->tag, tag);

        update_timestamp(group);
        ret = NEU_ERR_SUCCESS;
    }
    pthread_mutex_unlock(&group->mtx);

    return ret;
}

/**
 * @brief Deletes a tag from a Group
 * @param group The group
 * @param tag_name The name of the tag to be deleted
 * @return NEU_ERR_SUCCESS on success, NEU_ERR_TAG_NOT_EXIST if the tag doesn't exist
 */
int neu_group_del_tag(neu_group_t *group, const char *tag_name)
{
    tag_elem_t *el  = NULL;
    int         ret = NEU_ERR_TAG_NOT_EXIST;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag_name, el);
    if (el != NULL) {
        HASH_DEL(group->tags, el);
        free(el->name);
        neu_tag_free(el->tag);
        free(el);

        update_timestamp(group);
        ret = NEU_ERR_SUCCESS;
    }
    pthread_mutex_unlock(&group->mtx);

    return ret;
}

/**
 * @brief Gets an array of all tags in a Group
 * @param group The group
 * @return UT_array containing all tags in the group
 */
UT_array *neu_group_get_tag(neu_group_t *group)
{
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);
    array = to_array(group->tags);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief Filters tags based on a predicate function
 * @param tags The tag hash table
 * @param predicate Predicate function for filtering
 * @param data Additional data for the predicate function
 * @return UT_array containing filtered tags
 */
static inline UT_array *
filter_tags(tag_elem_t *tags,
            bool (*predicate)(const neu_datatag_t *, void *data), void *data)
{
    tag_elem_t *el = NULL, *tmp = NULL;
    UT_array *  array = NULL;

    utarray_new(array, neu_tag_get_icd());
    HASH_ITER(hh, tags, el, tmp)
    {
        if (predicate(el->tag, data)) {
            utarray_push_back(array, el->tag);
        }
    }

    return array;
}

/**
 * @brief Checks if a tag is readable
 * @param tag The tag
 * @param data Additional data (unused)
 * @return True if the tag is readable, false otherwise
 */
static inline bool is_readable(const neu_datatag_t *tag, void *data)
{
    (void) data;
    return neu_tag_attribute_test(tag, NEU_ATTRIBUTE_READ) ||
        neu_tag_attribute_test(tag, NEU_ATTRIBUTE_SUBSCRIBE) ||
        neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC);
}

/**
 * @brief Checks if a tag's name contains a specific string
 * @param tag The tag
 * @param data The string to search for
 * @return True if the tag's name contains the string, false otherwise
 */
static inline bool name_contains(const neu_datatag_t *tag, void *data)
{
    const char *name = data;
    return strstr(tag->name, name) != NULL ||
        (tag->description != NULL && strstr(tag->description, name) != NULL);
}

/**
 * @brief Checks if a tag's description contains a specific string
 * @param tag The tag
 * @param data The string to search for
 * @return True if the tag's description contains the string, false otherwise
 */
static inline bool description_contains(const neu_datatag_t *tag, void *data)
{
    const char *str = data;
    return tag->description && strstr(tag->description, str) != NULL;
}

/**
 * @struct query
 * @brief Structure representing a query for tag filtering
 */
struct query {
    char *name; /**< Tag name */
    char *desc; /**< Tag description */
};

/**
 * @brief Checks if a tag matches a query
 * @param tag The tag
 * @param data The query
 * @return True if the tag matches the query, false otherwise
 */
static inline bool match_query(const neu_datatag_t *tag, void *data)
{
    struct query *q = data;
    return (!q->name || name_contains(tag, q->name)) &&
        (!q->desc || description_contains(tag, q->desc));
}

/**
 * @brief Checks if a tag is readable and matches a query
 * @param tag The tag
 * @param data The query
 * @return True if the tag is readable and matches the query, false otherwise
 */
static inline bool is_readable_and_match_query(const neu_datatag_t *tag,
                                               void *               data)
{
    return is_readable(tag, NULL) && match_query(tag, data);
}

/**
 * @brief Queries tags in a Group based on name
 * @param group The group
 * @param name The name to search for in tag names
 * @return UT_array containing filtered tags
 */
UT_array *neu_group_query_tag(neu_group_t *group, const char *name)
{
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);
    array = filter_tags(group->tags, name_contains, (void *) name);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief Queries readable tags in a Neu Group based on name and description
 * @param group The group
 * @param name The name to search for in tag names
 * @param desc The description to search for in tag descriptions
 * @return UT_array containing filtered readable tags
 */
UT_array *neu_group_query_read_tag(neu_group_t *group, const char *name,
                                   const char *desc)
{
    UT_array *   array = NULL;
    struct query q     = {
        .name = (char *) name,
        .desc = (char *) desc,
    };

    pthread_mutex_lock(&group->mtx);
    array = filter_tags(group->tags, is_readable_and_match_query, &q);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief Gets an array of all readable tags in a Group
 * @param group The group
 * @return UT_array containing all readable tags in the group
 */
UT_array *neu_group_get_read_tag(neu_group_t *group)
{
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);
    array = filter_tags(group->tags, is_readable, NULL);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief Gets the number of tags in a Group
 * @param group The group
 * @return Number of tags in the group
 */
uint16_t neu_group_tag_size(const neu_group_t *group)
{
    uint16_t size = 0;

    size = HASH_COUNT(group->tags);

    return size;
}

/**
 * @brief Finds a specific tag in a Group
 * @param group The group
 * @param tag The name of the tag to find
 * @return Pointer to the found tag, or NULL if not found
 */
neu_datatag_t *neu_group_find_tag(neu_group_t *group, const char *tag)
{
    tag_elem_t *   find   = NULL;
    neu_datatag_t *result = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag, find);
    if (find != NULL) {
        result = neu_tag_dup(find->tag);
    }
    pthread_mutex_unlock(&group->mtx);

    return result;
}

/**
 * @brief Splits the tags in a Group into static and non-static categories
 * @param group The group
 * @param static_tags Output for static tags
 * @param other_tags Output for non-static tags
 */
void neu_group_split_static_tags(neu_group_t *group, UT_array **static_tags,
                                 UT_array **other_tags)
{
    return split_static_array(group->tags, static_tags, other_tags);
}

/**
 * @brief Executes a function based on changes in a Group
 * @param group The group
 * @param timestamp The timestamp to check for changes
 * @param arg Additional argument to be passed to the function
 * @param fn Function to be executed if changes are detected
 */
void neu_group_change_test(neu_group_t *group, int64_t timestamp, void *arg,
                           neu_group_change_fn fn)
{
    if (group->timestamp != timestamp) {
        UT_array *static_tags = NULL, *other_tags = NULL;
        split_static_array(group->tags, &static_tags, &other_tags);
        fn(arg, group->timestamp, static_tags, other_tags, group->interval);
    }
}

/**
 * @brief Checks if a Group has changed since a specific timestamp
 * @param group The group
 * @param timestamp The timestamp to compare against
 * @return True if the group has changed, false otherwise
 */
bool neu_group_is_change(neu_group_t *group, int64_t timestamp)
{
    bool change = false;

    change = group->timestamp != timestamp;

    return change;
}

/**
 * @brief Updates the timestamp of a Group
 * @param group The group
 */
static void update_timestamp(neu_group_t *group)
{
    struct timeval tv = { 0 };

    gettimeofday(&tv, NULL);

    group->timestamp = (int64_t) tv.tv_sec * 1000 * 1000 + (int64_t) tv.tv_usec;
}

/**
 * @brief Converts a tag hash table to a UT_array
 * @param tags The tag hash table
 * @return UT_array containing the tags
 */
static UT_array *to_array(tag_elem_t *tags)
{
    tag_elem_t *el = NULL, *tmp = NULL;
    UT_array *  array = NULL;

    utarray_new(array, neu_tag_get_icd());
    HASH_ITER(hh, tags, el, tmp) { utarray_push_back(array, el->tag); }

    return array;
}

/**
 * @brief Splits the tags into static and non-static categories
 * @param tags The tag hash table
 * @param static_tags Output for static tags
 * @param other_tags Output for non-static tags
 */
static void split_static_array(tag_elem_t *tags, UT_array **static_tags,
                               UT_array **other_tags)
{
    tag_elem_t *el = NULL, *tmp = NULL;

    utarray_new(*static_tags, neu_tag_get_icd());
    utarray_new(*other_tags, neu_tag_get_icd());
    HASH_ITER(hh, tags, el, tmp)
    {
        if (neu_tag_attribute_test(el->tag, NEU_ATTRIBUTE_STATIC)) {
            utarray_push_back(*static_tags, el->tag);
        } else if (neu_tag_attribute_test(el->tag, NEU_ATTRIBUTE_SUBSCRIBE) ||
                   neu_tag_attribute_test(el->tag, NEU_ATTRIBUTE_READ)) {
            utarray_push_back(*other_tags, el->tag);
        }
    }
}
