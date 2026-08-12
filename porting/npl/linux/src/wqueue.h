/*
 * wqueue.h
 * Worker thread queue for ble_npl_event pointers.
 * Non-template C++ class (simplified from original template version).
 *
 * Uses PTHREAD_MUTEX_NORMAL (not RECURSIVE) because pthread_cond_wait
 * with a recursive mutex has undefined behavior per POSIX.
 *
 * Includes a magic number to detect memory corruption.
 */

#ifndef __wqueue_h__
#define __wqueue_h__

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <list>

#include "nimble/nimble_npl.h"

#define WQUEUE_DEBUG 1

#if WQUEUE_DEBUG
#define WQUEUE_LOG(fmt, ...)  fprintf(stderr, "wqueue: " fmt "\n", ##__VA_ARGS__)
#else
#define WQUEUE_LOG(fmt, ...)  do {} while (0)
#endif

#define WQUEUE_MAGIC_INIT  0x57415545455555AAULL
#define WQUEUE_MAGIC_DEAD  0x5741554545555555ULL

class wqueue {
    uint64_t                   m_magic;
    std::list<ble_npl_event *> m_queue;
    pthread_mutex_t            m_mutex;
    pthread_cond_t             m_condv;

public:
    wqueue() : m_magic(WQUEUE_MAGIC_INIT)
    {
        pthread_mutex_init(&m_mutex, NULL);
        pthread_cond_init(&m_condv, NULL);
        WQUEUE_LOG("init %p magic=0x%llx", this, (unsigned long long)m_magic);
    }

    ~wqueue()
    {
        WQUEUE_LOG("destroy %p magic=0x%llx count=%zu",
                   this, (unsigned long long)m_magic, m_queue.size());
        m_magic = WQUEUE_MAGIC_DEAD;
        pthread_cond_destroy(&m_condv);
        pthread_mutex_destroy(&m_mutex);
    }

    bool is_valid() const
    {
        return (m_magic == WQUEUE_MAGIC_INIT);
    }

    void put(ble_npl_event *item)
    {
        if (!item) {
            WQUEUE_LOG("put %p: NULL item, ignored", this);
            return;
        }

        if (!is_valid()) {
            WQUEUE_LOG("put %p: *** CORRUPTED *** magic=0x%llx item=%p cb=%p",
                       this, (unsigned long long)m_magic, item,
                       item ? item->ev_cb : NULL);
            return;
        }

        pthread_mutex_lock(&m_mutex);
        m_queue.push_back(item);
        // WQUEUE_LOG("put %p: item=%p cb=%p count=%zu",
        //            this, item, item->ev_cb, m_queue.size());
        pthread_cond_signal(&m_condv);
        pthread_mutex_unlock(&m_mutex);
    }

    ble_npl_event *get(uint32_t tmo)
    {
        ble_npl_event *item = NULL;

        if (!is_valid()) {
            WQUEUE_LOG("get %p: *** CORRUPTED *** magic=0x%llx",
                       this, (unsigned long long)m_magic);
            return NULL;
        }

        pthread_mutex_lock(&m_mutex);

        if (tmo) {
            while (m_queue.size() == 0) {
                // WQUEUE_LOG("get %p: waiting (tmo=%u)", this, tmo);
                pthread_cond_wait(&m_condv, &m_mutex);
            }
        }

        if (m_queue.size() != 0) {
            item = m_queue.front();
            m_queue.pop_front();
            // WQUEUE_LOG("get %p: item=%p cb=%p remaining=%zu",
            //            this, item, item ? item->ev_cb : NULL,
            //            m_queue.size());
        } else {
            WQUEUE_LOG("get %p: empty (tmo=0), returning NULL", this);
        }

        pthread_mutex_unlock(&m_mutex);
        return item;
    }

    void remove(ble_npl_event *item)
    {
        if (!item) {
            return;
        }

        if (!is_valid()) {
            WQUEUE_LOG("remove %p: *** CORRUPTED *** magic=0x%llx item=%p",
                       this, (unsigned long long)m_magic, item);
            return;
        }

        pthread_mutex_lock(&m_mutex);
        m_queue.remove(item);
        WQUEUE_LOG("remove %p: item=%p count=%zu",
                   this, item, m_queue.size());
        pthread_mutex_unlock(&m_mutex);
    }

    int size()
    {
        int sz;
        if (!is_valid()) {
            WQUEUE_LOG("size %p: *** CORRUPTED *** magic=0x%llx",
                       this, (unsigned long long)m_magic);
            return 0;
        }
        pthread_mutex_lock(&m_mutex);
        sz = m_queue.size();
        pthread_mutex_unlock(&m_mutex);
        return sz;
    }
};

#endif
