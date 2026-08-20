/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nimble/nimble_npl.h"
#include <pthread.h>
#include <list>
// #include "wqueue.h"

class wqueue
{
    std::list< ble_npl_event* >         m_queue;
    pthread_mutex_t      m_mutex;
    pthread_mutexattr_t  m_mutex_attr;
    pthread_cond_t       m_condv;

public:
    wqueue()
    {
        pthread_mutexattr_init(&m_mutex_attr);
        pthread_mutexattr_settype(&m_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&m_mutex, &m_mutex_attr);
        pthread_cond_init(&m_condv, NULL);
    }

    ~wqueue() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_condv);
    }

    void put(ble_npl_event * item) {
        pthread_mutex_lock(&m_mutex);
        if (item->ev_queued) {
            pthread_mutex_unlock(&m_mutex);
            return;
        }
        item->ev_queued = 1;
        m_queue.push_back(item);
        pthread_cond_signal(&m_condv);
        pthread_mutex_unlock(&m_mutex);
    }

    ble_npl_event * get(uint32_t tmo) {
        pthread_mutex_lock(&m_mutex);
        if (tmo) {
            while (m_queue.size() == 0) {
                pthread_cond_wait(&m_condv, &m_mutex);
            }
        }

        ble_npl_event * item = NULL;

        if (m_queue.size() != 0) {
            item = m_queue.front();
            m_queue.pop_front();
        }

        pthread_mutex_unlock(&m_mutex);
        return item;
    }

    void remove(ble_npl_event * item) {
        pthread_mutex_lock(&m_mutex);
        m_queue.remove(item);
        pthread_mutex_unlock(&m_mutex);
    }

    int size() {
        pthread_mutex_lock(&m_mutex);
        int size = m_queue.size();
        pthread_mutex_unlock(&m_mutex);
        return size;
    }
};

extern "C" {

typedef wqueue wqueue_t;

static struct ble_npl_eventq dflt_evq;

struct ble_npl_eventq *
ble_npl_eventq_dflt_get(void)
{
    if (!dflt_evq.q) {
        dflt_evq.q = new wqueue_t();
    }

    return &dflt_evq;
}

void
ble_npl_eventq_init(struct ble_npl_eventq *evq)
{
    evq->q = new wqueue_t();
}

bool
ble_npl_eventq_is_empty(struct ble_npl_eventq *evq)
{
    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    if (q->size()) {
        return 1;
    } else {
        return 0;
    }
}

int
ble_npl_eventq_inited(const struct ble_npl_eventq *evq)
{
    return (evq->q != NULL);
}

void
ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    if (evq == NULL) {
        printf("ble_npl_eventq_put: evq is NULL, ev=%p\n", (void *)ev);
        return;
    }

    if (evq->q == NULL) {
        printf("ble_npl_eventq_put: evq->q is NULL, evq=%p ev=%p\n", (void *)evq, (void *)ev);
        return;
    }

    if (ev == NULL) {
        printf("ble_npl_eventq_put: ev is NULL\n");
        return;
    }

    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    

    // ev->ev_queued = 1;
    q->put(ev);
}

struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *evq,
                                         ble_npl_time_t tmo)
{
    struct ble_npl_event *ev;
    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    ev = q->get(tmo);

    if (ev) {
        ev->ev_queued = 0;
    }

    return ev;
}

void
ble_npl_eventq_run(struct ble_npl_eventq *evq)
{
    struct ble_npl_event *ev;

    ev = ble_npl_eventq_get(evq, BLE_NPL_TIME_FOREVER);
    ble_npl_event_run(ev);
}


// ========================================================================
//                         Event Implementation
// ========================================================================

void
ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn,
                   void *arg)
{
    memset(ev, 0, sizeof(struct ble_npl_event));
    ev->ev_cb = fn;
    ev->ev_arg = arg;
}

bool
ble_npl_event_is_queued(struct ble_npl_event *ev)
{
    return ev->ev_queued;
}

void *
ble_npl_event_get_arg(struct ble_npl_event *ev)
{
    return ev->ev_arg;
}

void
ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg)
{
    ev->ev_arg = arg;
}

void
ble_npl_event_run(struct ble_npl_event *ev)
{
    assert(ev->ev_cb != NULL);

    ev->ev_cb(ev);
}

void
ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    if (!ev->ev_queued) {
        return;
    }

    ev->ev_queued = 0;
    q->remove(ev);
}

}
