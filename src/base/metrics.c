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

#include <dirent.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#ifndef NEU_CLIB
#include <gnu/libc-version.h>
#endif

#include "adapter.h"
#include "adapter/adapter_internal.h"
#include "metrics.h"
#include "utils/log.h"
#include "utils/time.h"

pthread_rwlock_t g_metrics_mtx_ = PTHREAD_RWLOCK_INITIALIZER;
neu_metrics_t    g_metrics_;
static uint64_t  g_start_ts_;

/**
 * @brief Retrieves operating system information and stores it in global metrics.
 */
static void find_os_info()
{
    const char *cmd =
        "if [ -f /etc/os-release ]; then . /etc/os-release;"
        "echo $NAME $VERSION_ID; else uname -s; fi; uname -r; uname -m";
    FILE *f = popen(cmd, "r");

    if (NULL == f) {
        nlog_error("popen command fail");
        return;
    }

    char buf[64] = {};

    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.distro, buf, sizeof(g_metrics_.distro));
    g_metrics_.distro[sizeof(g_metrics_.distro) - 1] = 0;

    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.kernel, buf, sizeof(g_metrics_.kernel));
    g_metrics_.kernel[sizeof(g_metrics_.kernel) - 1] = 0;

    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.machine, buf, sizeof(g_metrics_.machine));
    g_metrics_.kernel[sizeof(g_metrics_.machine) - 1] = 0;

    pclose(f);

#ifdef NEU_CLIB
    strncpy(g_metrics_.clib, NEU_CLIB, sizeof(g_metrics_.clib));
    strncpy(g_metrics_.clib_version, "unknow", sizeof(g_metrics_.clib_version));
#else
    strncpy(g_metrics_.clib, "glibc", sizeof(g_metrics_.clib));
    strncpy(g_metrics_.clib_version, gnu_get_libc_version(),
            sizeof(g_metrics_.clib_version));
#endif
}


/**
 * @brief Parses specific memory fields and returns the value.
 * @param col Column number to parse
 * @return Parsed memory value
 */
static size_t parse_memory_fields(int col)
{
    FILE * f       = NULL;
    char   buf[64] = {};
    size_t val     = 0;

    sprintf(buf, "free -b | awk 'NR==2 {print $%i}'", col);

    f = popen(buf, "r");
    if (NULL == f) {
        nlog_error("popen command fail");
        return 0;
    }

    if (NULL != fgets(buf, sizeof(buf), f)) {
        val = atoll(buf);
    } else {
        nlog_error("no command output");
    }

    pclose(f);
    return val;
}

/**
 * @brief Gets the total memory on the system.
 * @return Total memory size in bytes
 */
static inline size_t memory_total()
{
    return parse_memory_fields(2);
}

/**
 * @brief Gets the used memory on the system.
 * @return Used memory size in bytes
 */
static inline size_t memory_used()
{
    return parse_memory_fields(3);
}

/**
 * @brief Gets the memory used by the Neuron process.
 * @return Neuron process memory usage in bytes
 */
static inline size_t neuron_memory_used()
{
    FILE * f       = NULL;
    char   buf[32] = {};
    size_t val     = 0;
    pid_t  pid     = getpid();

    sprintf(buf, "ps -o rss= %ld", (long) pid);

    f = popen(buf, "r");
    if (NULL == f) {
        nlog_error("popen command fail");
        return 0;
    }

    if (NULL != fgets(buf, sizeof(buf), f)) {
        val = atoll(buf);
    } else {
        nlog_error("no command output");
    }

    pclose(f);
    return val * 1024;
}

/**
 * @brief Gets the size of memory cache.
 * @return Memory cache size in bytes
 */
static inline size_t memory_cache()
{
    return parse_memory_fields(6);
}

/**
 * @brief Retrieves disk usage information.
 * @param size_p Pointer to store total disk size
 * @param used_p Pointer to store used disk space
 * @param avail_p Pointer to store available disk space
 * @return 0 on success, -1 on failure
 */
static inline int disk_usage(size_t *size_p, size_t *used_p, size_t *avail_p)
{
    struct statvfs buf = {};
    if (0 != statvfs(".", &buf)) {
        return -1;
    }

    *size_p  = (double) buf.f_frsize * buf.f_blocks / (1 << 30);
    *used_p  = (double) buf.f_frsize * (buf.f_blocks - buf.f_bfree) / (1 << 30);
    *avail_p = (double) buf.f_frsize * buf.f_bavail / (1 << 30);
    return 0;
}

/**
 * @brief Calculates CPU usage percentage.
 * @return CPU usage percentage
 */
static unsigned cpu_usage()
{
    int                ret   = 0;
    unsigned long long user1 = 0, nice1 = 0, sys1 = 0, idle1 = 0, iowait1 = 0,
                       irq1 = 0, softirq1 = 0;
    unsigned long long user2 = 0, nice2 = 0, sys2 = 0, idle2 = 0, iowait2 = 0,
                       irq2 = 0, softirq2 = 0;
    unsigned long long work = 0, total = 0;
    struct timespec    tv = {
        .tv_sec  = 0,
        .tv_nsec = 50000000,
    };
    FILE *f = NULL;

    f = fopen("/proc/stat", "r");
    if (NULL == f) {
        nlog_error("open /proc/stat fail");
        return 0;
    }

    ret = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &user1, &nice1,
                 &sys1, &idle1, &iowait1, &irq1, &softirq1);
    if (7 != ret) {
        fclose(f);
        return 0;
    }

    nanosleep(&tv, NULL);

    rewind(f);
    ret = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &user2, &nice2,
                 &sys2, &idle2, &iowait2, &irq2, &softirq2);
    if (7 != ret) {
        fclose(f);
        return 0;
    }
    fclose(f);

    work  = (user2 - user1) + (nice2 - nice1) + (sys2 - sys1);
    total = work + (idle2 - idle1) + (iowait2 - iowait1) + (irq2 - irq1) +
        (softirq2 - softirq1);

    return (double) work / total * 100.0 * sysconf(_SC_NPROCESSORS_CONF);
}

/**
 * @brief Checks if there are any core dumps present.
 * @return True if core dumps are present, false otherwise
 */
static bool has_core_dumps()
{
    DIR *dp = opendir("core");
    if (NULL == dp) {
        return false;
    }

    bool found = false;
    for (struct dirent *de = NULL; NULL != (de = readdir(dp));) {
        if (0 == strncmp("core", de->d_name, 4)) {
            found = true;
            break;
        }
    }

    closedir(dp);
    return found;
}

/**
 * @brief Unregisters a metric entry by decrementing its value.
 * @param name Name of the metric entry
 */
static inline void metrics_unregister_entry(const char *name)
{
    neu_metric_entry_t *e = NULL;
    HASH_FIND_STR(g_metrics_.registered_metrics, name, e);
    if (0 == --e->value) {
        HASH_DEL(g_metrics_.registered_metrics, e);
        nlog_notice("del entry:%s", e->name);
        neu_metric_entry_free(e);
    }
}

/**
 * @brief Adds metric entries to the provided list.
 * @param entries Pointer to the list of metric entries
 * @param name Name of the metric
 * @param help Description of the metric
 * @param type Type of the metric
 * @param init Initial value for the metric
 * @return 0 on success, 1 if entry already exists, -1 on failure
 */
int neu_metric_entries_add(neu_metric_entry_t **entries, const char *name,
                           const char *help, neu_metric_type_e type,
                           uint64_t init)
{
    neu_metric_entry_t *entry = NULL;
    HASH_FIND_STR(*entries, name, entry);
    if (NULL != entry) {
        if (entry->type != type || (0 != strcmp(entry->help, help))) {
            nlog_error("metric entry %s(%d, %s) conflicts with (%d, %s)", name,
                       entry->type, entry->help, type, help);
            return -1;
        }
        return 1;
    }

    entry = calloc(1, sizeof(*entry));
    if (NULL == entry) {
        return -1;
    }

    if (NEU_METRIC_TYPE_ROLLING_COUNTER == type) {
        // only allocate rolling counter for nonzero time span
        if (init > 0 && NULL == (entry->rcnt = neu_rolling_counter_new(init))) {
            free(entry);
            return -1;
        }
    } else {
        entry->value = init;
    }

    entry->name = name;
    entry->type = type;
    entry->help = help;
    HASH_ADD_STR(*entries, name, entry);
    return 0;
}

/**
 * @brief Initializes Neuron metrics.
 *
 * This function initializes Neuron metrics, including OS information,
 * memory total bytes, and start timestamp.
 */
void neu_metrics_init()
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    if (0 == g_start_ts_) {
        g_start_ts_ = neu_time_ms();
        find_os_info();
        g_metrics_.mem_total_bytes = memory_total();
    }
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

/**
 * @brief Adds a node's metrics to the global metrics.
 *
 * @param adapter The adapter containing the node's metrics.
 *
 * This function adds a node's metrics to the global metrics hash table.
 */
void neu_metrics_add_node(const neu_adapter_t *adapter)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    HASH_ADD_STR(g_metrics_.node_metrics, name, adapter->metrics);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

/**
 * @brief Removes a node's metrics from the global metrics.
 *
 * @param adapter The adapter containing the node's metrics.
 *
 * This function removes a node's metrics from the global metrics hash table.
 */
void neu_metrics_del_node(const neu_adapter_t *adapter)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    HASH_DEL(g_metrics_.node_metrics, adapter->metrics);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

/**
 * @brief Registers a new metric entry.
 *
 * @param name The name of the metric entry.
 * @param help The help string for the metric entry.
 * @param type The type of the metric entry.
 *
 * @return 0 on success, -1 on failure.
 *
 * This function registers a new metric entry with the provided name, help,
 * and type.
 */
int neu_metrics_register_entry(const char *name, const char *help,
                               neu_metric_type_e type)
{
    int                 rv = 0;
    neu_metric_entry_t *e  = NULL;

    pthread_rwlock_wrlock(&g_metrics_mtx_);
    // use `value` field as reference counter, initialize to zero
    // and we don't need to allocate rolling counter for register entries
    rv = neu_metric_entries_add(&g_metrics_.registered_metrics, name, help,
                                type, 0);
    if (-1 != rv) {
        HASH_FIND_STR(g_metrics_.registered_metrics, name, e);
        ++e->value;
        rv = 0;
    }
    pthread_rwlock_unlock(&g_metrics_mtx_);
    return rv;
}

/**
 * @brief Unregisters a metric entry.
 *
 * @param name The name of the metric entry.
 *
 * This function unregisters a metric entry with the provided name.
 */
void neu_metrics_unregister_entry(const char *name)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    metrics_unregister_entry(name);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}


/**
 * @brief Visits and updates metrics using the provided callback function.
 *
 * @param cb The callback function to visit and update metrics.
 * @param data Additional data to pass to the callback function.
 *
 * This function visits and updates various metrics using the provided callback
 * function and additional data.
 */
void neu_metrics_visist(neu_metrics_cb_t cb, void *data)
{
    unsigned cpu       = cpu_usage();
    size_t   mem_used  = neuron_memory_used();
    size_t   mem_cache = memory_cache();
    size_t   disk_size = 0, disk_used = 0, disk_avail = 0;
    disk_usage(&disk_size, &disk_used, &disk_avail);
    bool     core_dumped    = has_core_dumps();
    uint64_t uptime_seconds = (neu_time_ms() - g_start_ts_) / 1000;
    pthread_rwlock_rdlock(&g_metrics_mtx_);
    g_metrics_.cpu_percent          = cpu;
    g_metrics_.cpu_cores            = get_nprocs();
    g_metrics_.mem_used_bytes       = mem_used;
    g_metrics_.mem_cache_bytes      = mem_cache;
    g_metrics_.disk_size_gibibytes  = disk_size;
    g_metrics_.disk_used_gibibytes  = disk_used;
    g_metrics_.disk_avail_gibibytes = disk_avail;
    g_metrics_.core_dumped          = core_dumped;
    g_metrics_.uptime_seconds       = uptime_seconds;

    g_metrics_.north_nodes              = 0;
    g_metrics_.north_running_nodes      = 0;
    g_metrics_.north_disconnected_nodes = 0;
    g_metrics_.south_nodes              = 0;
    g_metrics_.south_running_nodes      = 0;
    g_metrics_.south_disconnected_nodes = 0;

    neu_node_metrics_t *n;
    HASH_LOOP(hh, g_metrics_.node_metrics, n)
    {
        neu_plugin_common_t *common =
            neu_plugin_to_plugin_common(n->adapter->plugin);

        n->adapter->cb_funs.update_metric(n->adapter, NEU_METRIC_RUNNING_STATE,
                                          n->adapter->state, NULL);
        n->adapter->cb_funs.update_metric(n->adapter, NEU_METRIC_LINK_STATE,
                                          common->link_state, NULL);

        if (NEU_NA_TYPE_DRIVER == n->adapter->module->type) {
            ++g_metrics_.south_nodes;
            if (NEU_NODE_RUNNING_STATE_RUNNING == n->adapter->state) {
                ++g_metrics_.south_running_nodes;
            }
            if (NEU_NODE_LINK_STATE_DISCONNECTED == common->link_state) {
                ++g_metrics_.south_disconnected_nodes;
            }
        } else if (NEU_NA_TYPE_APP == n->adapter->module->type) {
            ++g_metrics_.north_nodes;
            if (NEU_NODE_RUNNING_STATE_RUNNING == n->adapter->state) {
                ++g_metrics_.north_running_nodes;
            }
            if (NEU_NODE_LINK_STATE_DISCONNECTED == common->link_state) {
                ++g_metrics_.north_disconnected_nodes;
            }
        }
    }

    cb(&g_metrics_, data);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}
