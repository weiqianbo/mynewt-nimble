/*
   wqueue.h
   Worker thread queue based on the Standard C++ library list
   template class.
   ------------------------------------------
   Copyright (c) 2013 Vic Hargrave
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

// https://vichargrave.github.io/articles/2013-01/multithreaded-work-queue-in-cpp
// https://github.com/vichargrave/wqueue/blob/master/wqueue.h


#ifndef __wqueue_h__
#define __wqueue_h__

#include <pthread.h>
#include <list>

template <typename T> class wqueue
{
    std::list<T>         m_queue;
    pthread_mutex_t      m_mutex;
    pthread_mutexattr_t  m_mutex_attr;
    pthread_cond_t       m_condv;

public:
    wqueue()
    {
        pthread_mutexattr_init(&m_mutex_attr);
        /*
         * Use PTHREAD_MUTEX_NORMAL (not RECURSIVE) because pthread_cond_wait
         * with a recursive mutex has UNDEFINED behavior per POSIX.
         * With a recursive mutex, pthread_cond_wait may not properly release
         * the mutex, causing a deadlock where:
         *   1. Thread A holds the mutex (recursive lock count > 0)
         *   2. Thread A calls pthread_cond_wait which may NOT release the mutex
         *   3. Thread B calls put() -> pthread_mutex_lock() and blocks forever
         *   4. Thread A can never be signaled because Thread B holds the signal
         *
         * A normal mutex is sufficient because callbacks run AFTER get()
         * returns (mutex already released), so there is no reentrant locking.
         */
        pthread_mutexattr_settype(&m_mutex_attr, PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init(&m_mutex, &m_mutex_attr);
        pthread_cond_init(&m_condv, NULL);
    }

    ~wqueue() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_condv);
    }

    void put(T item) {
        pthread_mutex_lock(&m_mutex);
        m_queue.push_back(item);
        pthread_cond_signal(&m_condv);
        pthread_mutex_unlock(&m_mutex);
    }

    T get(uint32_t tmo) {
        pthread_mutex_lock(&m_mutex);
        if (tmo) {
            while (m_queue.size() == 0) {
                pthread_cond_wait(&m_condv, &m_mutex);
            }
        }

        T item = NULL;

        if (m_queue.size() != 0) {
            item = m_queue.front();
            m_queue.pop_front();
        }

        pthread_mutex_unlock(&m_mutex);
        return item;
    }

    void remove(T item) {
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

#endif
