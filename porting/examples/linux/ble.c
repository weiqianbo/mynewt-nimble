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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char gap_name[] = "nimble-central";

/* Target address to connect to: 11:22:33:44:55:77 */
static const uint8_t target_addr[6] = {0x7a, 0xe1, 0x03, 0x73, 0x00, 0x52};

static uint8_t own_addr_type;

static void start_scan(void);
static int gap_event_cb(struct ble_gap_event *event, void *arg);

/* Print address in hex format */
static char *
addr_str(const void *addr)
{
    static char str[32];
    const uint8_t *u8p;

    u8p = addr;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

    return str;
}

/* Check if the scanned address matches the target address */
static int
should_connect(const struct ble_gap_disc_desc *disc)
{
    /* Check if the address matches the target */
    if (memcmp(disc->addr.val, target_addr, 6) == 0) {
        printf("Found target device: %s\n", addr_str(disc->addr.val));
        return 1;
    }

    return 0;
}

/* Connect to the target device */
static void
connect_if_target(const struct ble_gap_disc_desc *disc)
{
    uint8_t own_addr_type;
    int rc;

    /* Don't do anything if this is not the target device */
    if (!should_connect(disc)) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        printf("Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("Error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect to the advertiser. Allow 30 seconds (30000 ms) for timeout */
    printf("Connecting to device: %s\n", addr_str(disc->addr.val));
    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         gap_event_cb, NULL);
    if (rc != 0) {
        printf("Error: Failed to connect to device; addr_type=%d addr=%s rc=%d\n",
               disc->addr.type, addr_str(disc->addr.val), rc);
        /* Resume scanning if connection fails */
        start_scan();
    }
}

static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        /* An advertisement report was received during GAP discovery */
        printf("Scanned device: %s, RSSI: %d dBm\n",
               addr_str(event->disc.addr.val), event->disc.rssi);

        /* Try to connect if this is the target device */
        connect_if_target(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed */
        if (event->connect.status == 0) {
            /* Connection successfully established */
            printf("Connection established\n");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                printf("Connected to: %s\n", addr_str(desc.peer_id_addr.val));
            }
        } else {
            /* Connection attempt failed; resume scanning */
            printf("Connection failed; status=%d\n", event->connect.status);
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated */
        printf("Disconnected; reason=%d\n", event->disconnect.reason);

        /* Resume scanning */
        start_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        printf("Discovery complete; reason=%d\n", event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication */
        printf("Received %s; conn_handle=%d attr_handle=%d\n",
               event->notify_rx.indication ? "indication" : "notification",
               event->notify_rx.conn_handle,
               event->notify_rx.attr_handle);
        return 0;

    case BLE_GAP_EVENT_MTU:
        printf("MTU update event; conn_handle=%d mtu=%d\n",
               event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link. Just throw away the old bond and
         * accept the new link.
         */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

static void
start_scan(void)
{
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while scanning (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("Error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates */
    disc_params.filter_duplicates = 1;

    /* Perform a passive scan (don't send follow-up scan requests) */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    printf("Starting BLE scan...\n");
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      gap_event_cb, NULL);
    if (rc != 0) {
        printf("Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

static void
app_ble_sync_cb(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for target device */
    start_scan();
}

void
nimble_host_task(void *param)
{
    ble_hs_cfg.sync_cb = app_ble_sync_cb;
    ble_hs_cfg.reset_cb = NULL;

    ble_svc_gap_device_name_set(gap_name);

    nimble_port_run();
}
