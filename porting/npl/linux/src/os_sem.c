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
#include <errno.h>
#include <semaphore.h>
#include <time.h>

#include "os/os.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

/*
 * When a shell command (e.g., app-key-add) calls bt_mesh_cfg_* which blocks on
 * k_sem_take, the entire event loop is blocked because ble_npl_sem_pend is
 * called from the event loop thread. This prevents incoming mesh responses and
 * timer callbacks from being processed, causing a deadlock-like state.
 *
 * Fix: While waiting for the semaphore, periodically process events from the
 * default event queue (nimble_port_get_dflt_eventq) to keep the event loop
 * running. A __thread guard prevents nested event processing when a callback
 * itself calls k_sem_take.
 */
static __thread int in_sem_pend = 0;

static void
sem_pend_pump_events(void)
{
    struct ble_npl_eventq *evq;
    struct ble_npl_event *ev;

    if (in_sem_pend) {
        return;
    }

    evq = nimble_port_get_dflt_eventq();
    if (!evq) {
        return;
    }

    in_sem_pend = 1;

    ev = ble_npl_eventq_get(evq, 0);
    if (ev) {
        ble_npl_event_run(ev);
    }

    in_sem_pend = 0;
}

ble_npl_error_t
ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    sem_init(&sem->lock, 0, tokens);

    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_sem_release(struct ble_npl_sem *sem)
{
    int err;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    err = sem_post(&sem->lock);

    return (err) ? BLE_NPL_ERROR : BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_sem_pend(struct ble_npl_sem *sem, uint32_t timeout)
{
    int err = 0;
    struct timespec wait;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (timeout == BLE_NPL_TIME_FOREVER) {
        /* Use 1ms timed wait loop to allow event processing between waits.
         * This prevents the event loop from freezing when a shell command
         * (e.g., app-key-add) blocks on k_sem_take. */
        while (1) {
            err = clock_gettime(CLOCK_REALTIME, &wait);
            if (err) {
                return BLE_NPL_ERROR;
            }
            wait.tv_nsec += 1000000; /* +1ms */
            if (wait.tv_nsec >= 1000000000) {
                wait.tv_sec++;
                wait.tv_nsec -= 1000000000;
            }

            err = sem_timedwait(&sem->lock, &wait);
            if (err == 0) {
                return BLE_NPL_OK;
            }
            if (errno == ETIMEDOUT) {
                sem_pend_pump_events();
                continue;
            }
            if (errno != EINTR) {
                break;
            }
        }
    } else {
        err = clock_gettime(CLOCK_REALTIME, &wait);
        if (err) {
            return BLE_NPL_ERROR;
        }

        wait.tv_sec  += timeout / 1000;
        wait.tv_nsec += (timeout % 1000) * 1000000;
        if (wait.tv_nsec >= 1000000000) {
            wait.tv_sec  += wait.tv_nsec / 1000000000;
            wait.tv_nsec %= 1000000000;
        }

        while ((err = sem_timedwait(&sem->lock, &wait)) != 0) {
            switch (errno) {
            case EINTR:
                sem_pend_pump_events();
                continue;
            case ETIMEDOUT:
                sem_pend_pump_events();
                return BLE_NPL_TIMEOUT;
            }
            break;
        }
    }

    return (err) ? BLE_NPL_ERROR : BLE_NPL_OK;
}

uint16_t
ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    int count;

    assert(sem);
    assert(&sem->lock);
    sem_getvalue(&sem->lock, &count);

    return count;
}
