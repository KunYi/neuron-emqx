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

#include <stdio.h>

#include "define.h"
#include "metrics.h"
#include "utils/asprintf.h"
#include "utils/log.h"

#include "metric_handle.h"
#include "monitor.h"
#include "restful/handle.h"
#include "restful/http.h"

// clang-format off
#define METRIC_GLOBAL_TMPL                                                       \
    "# HELP core_dumped Whether there is any core dump\n"                        \
    "# TYPE core_dumped gauge\n"                                                 \
    "core_dumped %d\n"                                                           \
    "# HELP uptime_seconds Uptime in seconds\n"                                  \
    "# TYPE uptime_seconds counter\n"                                            \
    "uptime_seconds %" PRIu64 "\n"                                               \
    "# HELP north_nodes_total Number of north nodes\n"                           \
    "# TYPE north_nodes_total gauge\n"                                           \
    "north_nodes_total %zu\n"                                                    \
    "# HELP north_running_nodes_total Number of north nodes in running state\n"  \
    "# TYPE north_running_nodes_total gauge\n"                                   \
    "north_running_nodes_total %zu\n"                                            \
    "# HELP north_disconnected_nodes_total Number of north nodes disconnected\n" \
    "# TYPE north_disconnected_nodes_total gauge\n"                              \
    "north_disconnected_nodes_total %zu\n"                                       \
    "# HELP south_nodes_total Number of south nodes\n"                           \
    "# TYPE south_nodes_total gauge\n"                                           \
    "south_nodes_total %zu\n"                                                    \
    "# HELP south_running_nodes_total Number of south nodes in running state\n"  \
    "# TYPE south_running_nodes_total gauge\n"                                   \
    "south_running_nodes_total %zu\n"                                            \
    "# HELP south_disconnected_nodes_total Number of south nodes disconnected\n" \
    "# TYPE south_disconnected_nodes_total gauge\n"                              \
    "south_disconnected_nodes_total %zu\n"
// clang-format on

static int response(nng_aio *aio, char *content, enum nng_http_status status)
{
    nng_http_res *res = NULL;

    nng_http_res_alloc(&res);

    nng_http_res_set_header(res, "Content-Type", "text/plain");
    nng_http_res_set_header(res, "Access-Control-Allow-Origin", "*");
    nng_http_res_set_header(res, "Access-Control-Allow-Methods",
                            "POST,GET,PUT,DELETE,OPTIONS");
    nng_http_res_set_header(res, "Access-Control-Allow-Headers", "*");

    if (content != NULL && strlen(content) > 0) {
        nng_http_res_copy_data(res, content, strlen(content));
    } else {
        nng_http_res_set_data(res, NULL, 0);
    }

    nng_http_res_set_status(res, status);

    nng_http_req *nng_req = nng_aio_get_input(aio, 0);
    nlog_notice("%s %s [%d]", nng_http_req_get_method(nng_req),
                nng_http_req_get_uri(nng_req), status);

    nng_aio_set_output(aio, 0, res);
    nng_aio_finish(aio, 0);

    return 0;
}

static inline bool parse_metrics_catgory(const char *s, size_t len,
                                         neu_metrics_category_e *cat)
{
    char *domain[] = {
        [NEU_METRICS_CATEGORY_GLOBAL] = "global",
        [NEU_METRICS_CATEGORY_DRIVER] = "driver",
        [NEU_METRICS_CATEGORY_APP]    = "app",
        [NEU_METRICS_CATEGORY_ALL]    = NULL,
    };

    if (NULL == s || NULL == cat) {
        return false;
    }

    for (int i = 0; i < NEU_METRICS_CATEGORY_ALL; ++i) {
        if (strlen(domain[i]) == len && 0 == strncmp(s, domain[i], len)) {
            *cat = i;
            return true;
        }
    }

    return false;
}

static inline void gen_global_metrics(const neu_metrics_t *metrics,
                                      FILE *               stream)
{
    fprintf(stream, METRIC_GLOBAL_TMPL, metrics->core_dumped,
            metrics->uptime_seconds, metrics->north_nodes,
            metrics->north_running_nodes, metrics->north_disconnected_nodes,
            metrics->south_nodes, metrics->south_running_nodes,
            metrics->south_disconnected_nodes);
}

static inline void
gen_single_node_metrics(const neu_node_metrics_t *node_metrics, FILE *stream)
{
    neu_metric_entry_t *e = NULL;
    HASH_LOOP(hh, node_metrics->entries, e)
    {
        fprintf(stream,
                "# HELP %s %s\n# TYPE %s %s\n%s{node=\"%s\"} %" PRIu64 "\n",
                e->name, e->help, e->name, neu_metric_type_str(e->type),
                e->name, node_metrics->name, e->value);
    }
}

static void gen_all_node_metrics(const neu_metrics_t *metrics, int type_filter,
                                 FILE *stream)
{
    neu_metric_entry_t *e = NULL, *r = NULL;
    neu_node_metrics_t *n = NULL;

    HASH_LOOP(hh, metrics->registered_metrics, r)
    {
        bool commented = false;
        HASH_LOOP(hh, metrics->node_metrics, n)
        {
            if (!(type_filter & n->type)) {
                continue;
            }

            if (!commented) {
                commented = true;
                fprintf(stream, "# HELP %s %s\n# TYPE %s %s\n", r->name,
                        r->help, r->name, neu_metric_type_str(r->type));
            }

            HASH_FIND_STR(n->entries, r->name, e);
            if (e) {
                fprintf(stream, "%s{node=\"%s\"} %" PRIu64 "\n", e->name,
                        n->name, e->value);
            }
        }
    }
}

struct context {
    int         filter;
    int *       status;
    FILE *      stream;
    const char *node;
};

static void gen_node_metrics(const neu_metrics_t *metrics, struct context *ctx)
{
    if (ctx->node[0]) {
        neu_node_metrics_t *n = NULL;
        HASH_FIND_STR(metrics->node_metrics, ctx->node, n);
        if (NULL == n || 0 == (ctx->filter & n->type)) {
            *ctx->status = NNG_HTTP_STATUS_NOT_FOUND;
            return;
        }
        gen_single_node_metrics(n, ctx->stream);
    } else {
        gen_all_node_metrics(metrics, ctx->filter, ctx->stream);
    }
}

void handle_get_metric(nng_aio *aio)
{
    int           status = NNG_HTTP_STATUS_OK;
    char *        result = NULL;
    size_t        len    = 0;
    FILE *        stream = NULL;
    neu_plugin_t *plugin = neu_monitor_get_plugin();

    neu_metrics_category_e cat           = NEU_METRICS_CATEGORY_ALL;
    size_t                 cat_param_len = 0;
    const char *cat_param = http_get_param(aio, "category", &cat_param_len);
    if (NULL != cat_param &&
        !parse_metrics_catgory(cat_param, cat_param_len, &cat)) {
        plog_info(plugin, "invalid metrics category: %.*s", (int) cat_param_len,
                  cat_param);
        status = NNG_HTTP_STATUS_BAD_REQUEST;
        goto end;
    }

    char    node_name[NEU_NODE_NAME_LEN] = { 0 };
    ssize_t rv = http_get_param_str(aio, "node", node_name, sizeof(node_name));
    if (-1 == rv || rv >= NEU_NODE_NAME_LEN ||
        (0 < rv && NEU_METRICS_CATEGORY_GLOBAL == cat)) {
        status = NNG_HTTP_STATUS_BAD_REQUEST;
        goto end;
    }

    stream = open_memstream(&result, &len);
    if (NULL == stream) {
        status = NNG_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        goto end;
    }

    struct context ctx = {
        .status = &status,
        .stream = stream,
        .node   = node_name,
    };

    switch (cat) {
    case NEU_METRICS_CATEGORY_GLOBAL:
        neu_metrics_visist((neu_metrics_cb_t) gen_global_metrics, stream);
        break;
    case NEU_METRICS_CATEGORY_DRIVER:
        ctx.filter = NEU_NA_TYPE_DRIVER;
        neu_metrics_visist((neu_metrics_cb_t) gen_node_metrics, &ctx);
        break;
    case NEU_METRICS_CATEGORY_APP:
        ctx.filter = NEU_NA_TYPE_APP;
        neu_metrics_visist((neu_metrics_cb_t) gen_node_metrics, &ctx);
        break;
    case NEU_METRICS_CATEGORY_ALL:
        ctx.filter = NEU_NA_TYPE_DRIVER | NEU_NA_TYPE_APP;
        neu_metrics_visist((neu_metrics_cb_t) gen_global_metrics, stream);
        neu_metrics_visist((neu_metrics_cb_t) gen_node_metrics, &ctx);
        break;
    }

end:
    if (NULL != stream) {
        fclose(stream);
    }
    response(aio, NNG_HTTP_STATUS_OK == status ? result : NULL, status);
    free(result);
}