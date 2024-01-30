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
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "event/event.h"
#include "utils/log.h"

#ifdef NEU_PLATFORM_LINUX
#include <sys/epoll.h>
#include <sys/timerfd.h>

/**
 * @struct neu_event_timer
 * @brief Structure representing a timer event.
 *
 * The `neu_event_timer` structure is used to store information about a timer event,
 * including its unique identifier (`fd`), associated event data (`event_data`),
 * timer specifications (`value`), type of timer (`type`), synchronization mutex (`mtx`),
 * and a flag to stop the timer (`stop`).
 */
struct neu_event_timer {
    int                    fd;          ///< File descriptor for the timer.
    struct event_data *    event_data;  ///< Pointer to the associated event data.
    struct itimerspec      value;       ///< Timer specifications.
    neu_event_timer_type_e type;        ///< Type of timer event.
    pthread_mutex_t        mtx;         ///< Mutex for synchronization.
    bool                   stop;        ///< Flag to stop the timer.
};

/**
 * @struct neu_event_io
 * @brief Structure representing an I/O event.
 *
 * The `neu_event_io` structure stores information about an I/O event, including its
 * file descriptor (`fd`) and associated event data (`event_data`).
 */
struct neu_event_io {
    int                fd;              ///< File descriptor for the I/O event.
    struct event_data *event_data;      ///< Pointer to the associated event data.
};

/**
 * @struct event_data
 * @brief Structure representing common data for both timer and I/O events.
 *
 * The `event_data` structure stores information shared between timer and I/O events,
 * including the event type (`type`), callback functions (`callback`), context data
 * (`ctx`), user data (`usr_data`), file descriptor (`fd`), index in the event array (`index`),
 * and a flag indicating if the event is in use (`use`).
 */
struct event_data {
    enum {
        TIMER = 0,
        IO    = 1,
    } type;                             ///< Type of the event (timer or I/O).
    union {
        neu_event_io_callback    io;    ///< Callback function for I/O events.
        neu_event_timer_callback timer; ///< Callback function for timer events.
    } callback;
    union {
        neu_event_io_t    io;           ///< I/O event context.
        neu_event_timer_t timer;        ///< Timer event context.
    } ctx;

    void *usr_data;                     ///< User data associated with the event.
    int   fd;                           ///< File descriptor associated with the event.
    int   index;                        ///< Index of the event data in the event array.
    bool  use;                          ///< Flag indicating if the event data is in use.
};

#define EVENT_SIZE 1400

/**
 * @struct neu_events
 * @brief Structure representing the event manager.
 *
 * The `neu_events` structure is used to manage events, including the epoll file descriptor (`epoll_fd`),
 * the event handling thread (`thread`), a flag to stop the event manager (`stop`), synchronization mutex (`mtx`),
 * the number of events (`n_event`), and an array to store event data (`event_datas`).
 */
struct neu_events {
    int       epoll_fd;                 ///< File descriptor for the epoll instance.
    pthread_t thread;                   ///< Thread for event handling.
    bool      stop;                     ///< Flag to stop the event manager.

    pthread_mutex_t   mtx;              ///< Mutex for synchronization.
    int               n_event;          ///< Number of events in the event array.
    struct event_data event_datas[EVENT_SIZE];  ///< Array to store event data.
};

/**
 * @brief Get a free event from the event manager.
 *
 * This function retrieves a free event from the event manager's event array.
 * The event array has a maximum size defined by EVENT_SIZE. The function
 * searches for the first available (unused) slot in the array and marks it as used.
 *
 * @param events Pointer to the `neu_events_t` structure.
 * @return The index of the free event, or -1 if no free event is available.
 */
static int get_free_event(neu_events_t *events)
{
    int ret = -1;
    pthread_mutex_lock(&events->mtx);
    for (int i = 0; i < EVENT_SIZE; i++) {
        if (events->event_datas[i].use == false) {
            events->event_datas[i].use   = true;
            events->event_datas[i].index = i;
            ret                          = i;
            break;
        }
    }

    pthread_mutex_unlock(&events->mtx);
    return ret;
}

/**
 * @brief Free an event in the event manager.
 *
 * This function releases a previously acquired event in the event manager's event array.
 * It marks the specified event as unused, allowing it to be acquired again in the future.
 *
 * @param events Pointer to the `neu_events_t` structure.
 * @param index Index of the event to be freed.
 */
static void free_event(neu_events_t *events, int index)
{
    pthread_mutex_lock(&events->mtx);
    events->event_datas[index].use   = false;
    events->event_datas[index].index = 0;
    pthread_mutex_unlock(&events->mtx);
}

/**
 * @brief Event loop for handling events in a separate thread.
 *
 * This function represents the main loop of the event manager's thread. It continuously
 * waits for events using epoll_wait and processes them based on their types.
 * For timer events, it reads from the associated timer file descriptor and triggers
 * the timer callback. For I/O events, it handles EPOLLHUP, EPOLLRDHUP, and EPOLLIN
 * events, calling the appropriate I/O callback functions.
 *
 * @param arg Pointer to the `neu_events_t` structure.
 * @return NULL.
 */
static void *event_loop(void *arg)
{
    neu_events_t *events   = (neu_events_t *) arg;
    int           epoll_fd = events->epoll_fd;

    while (true) {
        struct epoll_event event = { 0 };
        struct event_data *data  = NULL;

        // Wait for events using epoll_wait with a timeout of 1000 milliseconds
        int ret = epoll_wait(epoll_fd, &event, 1, 1000);
        if (ret == 0) {
            continue; // No events, continue waiting
        }

        if (ret == -1 && errno == EINTR) {
            continue; // Interrupted system call, continue waiting
        }

        if (ret == -1 || events->stop) {
            // Error in epoll_wait or the event manager is stopping, exit the loop
            zlog_warn(neuron, "event loop exit, errno: %s(%d), stop: %d",
                      strerror(errno), errno, events->stop);
            break;
        }

        // Get the associated data from the triggered event
        data = (struct event_data *) event.data.ptr;

        switch (data->type) {
        case TIMER:
            pthread_mutex_lock(&data->ctx.timer.mtx);
            if ((event.events & EPOLLIN) == EPOLLIN) {
                uint64_t t;
                // Read from the timer file descriptor to clear the event
                ssize_t size = read(data->fd, &t, sizeof(t));
                (void) size;

                // Check if the timer is not stopped
                if (!data->ctx.timer.stop) {
                    if (data->ctx.timer.type == NEU_EVENT_TIMER_BLOCK) {
                        // If the timer type is BLOCK, temporarily remove it from epoll,
                        // trigger the callback, reset the timer, and add it back to epoll
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data->fd, NULL);
                        ret = data->callback.timer(data->usr_data);
                        timerfd_settime(data->fd, 0, &data->ctx.timer.value,
                                        NULL);
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, data->fd, &event);
                    } else {
                        // If the timer type is not BLOCK, simply trigger the callback
                        ret = data->callback.timer(data->usr_data);
                    }
                }
            }

            pthread_mutex_unlock(&data->ctx.timer.mtx);
            break;
        case IO:
            if ((event.events & EPOLLHUP) == EPOLLHUP) {
                // Handle hang-up event
                data->callback.io(NEU_EVENT_IO_HUP, data->fd, data->usr_data);
                break;
            }

            if ((event.events & EPOLLRDHUP) == EPOLLRDHUP) {
                // Handle read hang-up event
                data->callback.io(NEU_EVENT_IO_CLOSED, data->fd,
                                  data->usr_data);
                break;
            }

            if ((event.events & EPOLLIN) == EPOLLIN) {
                // Handle read event
                data->callback.io(NEU_EVENT_IO_READ, data->fd, data->usr_data);
                break;
            }

            break;
        }
    }

    return NULL;
};

/**
 * @brief Creates a new neu_events_t structure for handling events.
 *
 * This function allocates and initializes a new neu_events_t structure. It creates an epoll
 * instance, initializes necessary fields, and starts the event loop in a separate thread.
 *
 * @return A pointer to the newly created neu_events_t structure.
 */
neu_events_t *neu_event_new(void)
{
    neu_events_t *events = calloc(1, sizeof(struct neu_events));

    events->epoll_fd = epoll_create(1);

    nlog_notice("create epoll: %d(%d)", events->epoll_fd, errno);
    assert(events->epoll_fd > 0);

    events->stop    = false;
    events->n_event = 0;
    pthread_mutex_init(&events->mtx, NULL);

    pthread_create(&events->thread, NULL, event_loop, events);

    return events;
};

/**
 * @brief Closes and deallocates resources associated with the neu_events_t structure.
 *
 * This function stops the event loop, closes the epoll instance, and frees the memory
 * allocated for the neu_events_t structure.
 *
 * @param events Pointer to the neu_events_t structure to be closed and freed.
 * @return Always returns 0.
 */
int neu_event_close(neu_events_t *events)
{
    events->stop = true;
    close(events->epoll_fd);

    pthread_join(events->thread, NULL); // Wait for the event loop thread to finish
    pthread_mutex_destroy(&events->mtx);

    free(events);
    return 0;
}

/**
 * @brief Adds a timer event to the event manager.
 *
 * This function creates a timer using timerfd_create, sets its properties using timerfd_settime,
 * and adds it to the epoll instance managed by the event manager. The timer callback will be
 * triggered when the timer expires.
 *
 * @param events Pointer to the neu_events_t structure managing events.
 * @param timer Timer parameters, including callback, duration, and user data.
 * @return Pointer to the neu_event_timer_t structure representing the added timer.
 */
neu_event_timer_t *neu_event_add_timer(neu_events_t *          events,
                                       neu_event_timer_param_t timer)
{
    int               ret      = 0;
    int               timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec value    = {
        .it_value.tv_sec     = timer.second,
        .it_value.tv_nsec    = timer.millisecond * 1000 * 1000,
        .it_interval.tv_sec  = timer.second,
        .it_interval.tv_nsec = timer.millisecond * 1000 * 1000,
    };
    int index = get_free_event(events);
    if (index < 0) {
        zlog_fatal(neuron, "no free event: %d", events->epoll_fd);
    }
    assert(index >= 0);

    neu_event_timer_t *timer_ctx = &events->event_datas[index].ctx.timer;
    timer_ctx->event_data        = &events->event_datas[index];

    struct epoll_event event = {
        .events   = EPOLLIN,
        .data.ptr = timer_ctx->event_data,
    };

    timerfd_settime(timer_fd, 0, &value, NULL);

    timer_ctx->event_data->type           = TIMER;
    timer_ctx->event_data->fd             = timer_fd;
    timer_ctx->event_data->usr_data       = timer.usr_data;
    timer_ctx->event_data->callback.timer = timer.cb;
    timer_ctx->event_data->ctx.timer = events->event_datas[index].ctx.timer;
    timer_ctx->event_data->index     = index;

    timer_ctx->value = value;
    timer_ctx->fd    = timer_fd;
    timer_ctx->type  = timer.type;
    timer_ctx->stop  = false;
    pthread_mutex_init(&timer_ctx->mtx, NULL);

    ret = epoll_ctl(events->epoll_fd, EPOLL_CTL_ADD, timer_fd, &event);

    zlog_notice(neuron,
                "add timer, second: %" PRId64 ", millisecond: %" PRId64
                ", timer: %d in epoll %d, "
                "ret: %d, index: %d",
                timer.second, timer.millisecond, timer_fd, events->epoll_fd,
                ret, index);

    return timer_ctx;
}

/**
 * @brief Removes a timer event from the event manager.
 *
 * This function stops and removes a timer event from the epoll instance managed by the event manager.
 * It closes the timer file descriptor and frees associated resources.
 *
 * @param events Pointer to the neu_events_t structure managing events.
 * @param timer Pointer to the neu_event_timer_t structure representing the timer to be removed.
 * @return Always returns 0.
 */
int neu_event_del_timer(neu_events_t *events, neu_event_timer_t *timer)
{
    zlog_notice(neuron, "del timer: %d from epoll: %d, index: %d", timer->fd,
                events->epoll_fd, timer->event_data->index);

    // Stop the timer and delete
    timer->stop = true;
    epoll_ctl(events->epoll_fd, EPOLL_CTL_DEL, timer->fd, NULL);

    // Close the timer file descriptor
    pthread_mutex_lock(&timer->mtx);
    close(timer->fd);
    pthread_mutex_unlock(&timer->mtx);

    pthread_mutex_destroy(&timer->mtx);
    free_event(events, timer->event_data->index);
    return 0;
}

/**
 * @brief Adds an I/O event to the event manager.
 *
 * This function adds an I/O event to the epoll instance managed by the event manager.
 * The specified file descriptor (`io.fd`) will be monitored for EPOLLIN, EPOLLERR,
 * EPOLLHUP, and EPOLLRDHUP events. When these events occur, the associated I/O callback
 * will be triggered.
 *
 * @param events Pointer to the neu_events_t structure managing events.
 * @param io I/O parameters, including file descriptor, callback, and user data.
 * @return Pointer to the neu_event_io_t structure representing the added I/O event.
 */
neu_event_io_t *neu_event_add_io(neu_events_t *events, neu_event_io_param_t io)
{
    int ret   = 0;
    int index = get_free_event(events);

    nlog_notice("add io, fd: %d, epoll: %d, index: %d", io.fd, events->epoll_fd,
                index);
    assert(index >= 0);

    neu_event_io_t *io_ctx   = &events->event_datas[index].ctx.io;
    io_ctx->event_data       = &events->event_datas[index];
    struct epoll_event event = {
        .events   = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP,
        .data.ptr = io_ctx->event_data,
    };

    io_ctx->event_data->type        = IO;
    io_ctx->event_data->fd          = io.fd;
    io_ctx->event_data->usr_data    = io.usr_data;
    io_ctx->event_data->callback.io = io.cb;
    io_ctx->event_data->ctx.io      = events->event_datas[index].ctx.io;
    io_ctx->event_data->index       = index;

    io_ctx->fd = io.fd;

    ret = epoll_ctl(events->epoll_fd, EPOLL_CTL_ADD, io.fd, &event);

    nlog_notice("add io, fd: %d, epoll: %d, ret: %d(%d), index: %d", io.fd,
                events->epoll_fd, ret, errno, index);
    assert(ret == 0);

    return io_ctx;
}

/**
 * @brief Removes an I/O event from the event manager.
 *
 * This function stops and removes an I/O event from the epoll instance managed by the event manager.
 * It frees the associated resources and releases the event slot.
 *
 * @param events Pointer to the neu_events_t structure managing events.
 * @param io Pointer to the neu_event_io_t structure representing the I/O event to be removed.
 * @return Always returns 0.
 */
int neu_event_del_io(neu_events_t *events, neu_event_io_t *io)
{
    if (io == NULL) {
        return 0;
    }

    zlog_notice(neuron, "del io: %d from epoll: %d, index: %d", io->fd,
                events->epoll_fd, io->event_data->index);

    epoll_ctl(events->epoll_fd, EPOLL_CTL_DEL, io->fd, NULL);
    free_event(events, io->event_data->index);

    return 0;
}

#endif
