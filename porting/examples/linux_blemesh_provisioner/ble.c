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
 * language or limitations.
 */

#include <assert.h>
#include <string.h>
#include "mesh/mesh.h"
#include "console/console.h"

/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "mesh/glue.h"
#include "mesh/health_srv.h"

#include "mesh/cdb.h"
#include "mesh/cfg_cli.h"

#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

/* Company ID */
#define CID_VENDOR 0x05C3

/* Provisioner configuration */
#define PROV_ADDR        0x0001
#define NET_IDX          0x0000
#define APP_IDX          0x0000
#define GROUP_ADDR       0xc123
#define DEFAULT_TTL      31

/* Test model configuration - shared by both sides */
#define TEST_MODEL_ID    0x0001
#define TEST_OPCODE      BT_MESH_MODEL_OP_3(0x02, CID_VENDOR)

/* Fixed device key for the provisioner */
static const uint8_t prov_dev_key[16] = {
    0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
    0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd
};

/* Application key for distribution to nodes */
static const uint8_t app_key[16] = {
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
};

/* Flag to prevent duplicate provisioning attempts */
static bool provisioning_in_progress;

/* Timer for periodic messages from provisioner */
static struct ble_npl_callout msg_send_callout;
static uint16_t last_node_addr;

/* Forward declarations */
void mesh_initialized(void);
static void prov_node_added(uint16_t net_idx, uint8_t uuid[16], uint16_t addr,
                            uint8_t num_elem);
static void send_msg_to_node(struct bt_mesh_model *model, uint16_t dst_addr);
static void msg_send_work(struct ble_npl_event *ev);

/* Health server callbacks */
static int
fault_get_cur(struct bt_mesh_model *model,
              uint8_t *test_id,
              uint16_t *company_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    static uint8_t reg_faults[2] = { [0 ... 1] = 0xff };

    *test_id = 0;
    *company_id = CID_VENDOR;
    *fault_count = MIN(*fault_count, sizeof(reg_faults));
    memcpy(faults, reg_faults, *fault_count);
    return 0;
}

static int
fault_get_reg(struct bt_mesh_model *model,
              uint16_t company_id,
              uint8_t *test_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    *test_id = 0;
    *fault_count = 0;
    return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }
    return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }
    if (test_id != 0) {
        return -BLE_HS_EINVAL;
    }
    return 0;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .fault_get_cur = &fault_get_cur,
    .fault_get_reg = &fault_get_reg,
    .fault_clear = &fault_clear,
    .fault_test = &fault_test,
};

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

static struct os_mbuf *bt_mesh_pub_msg_health_pub;
static struct bt_mesh_model_pub health_pub;

/* Test model - for bidirectional messaging with logging */
static struct bt_mesh_model_pub test_model_pub;

static int
test_model_recv(struct bt_mesh_model *model,
                 struct bt_mesh_msg_ctx *ctx,
                 struct os_mbuf *buf)
{
    uint8_t opcode_buf[3] = { 0 };
    int i;

    /* Reconstruct opcode from the first bytes of the buffer */
    if (buf->om_len >= 1) {
        opcode_buf[0] = buf->om_data[0];
    }
    if (buf->om_len >= 2) {
        opcode_buf[1] = buf->om_data[1];
    }
    if (buf->om_len >= 3) {
        opcode_buf[2] = buf->om_data[2];
    }

    console_printf("\n========================================\n");
    console_printf("[PROVISIONER] Received mesh message\n");
    console_printf("========================================\n");
    console_printf("  Source address:   0x%04x\n", ctx->addr);
    console_printf("  Destination:      0x%04x\n", ctx->addr);
    console_printf("  AppKey index:     0x%04x\n", ctx->app_idx);
    console_printf("  NetKey index:     0x%04x\n", ctx->net_idx);
    console_printf("  Opcode:           0x%02x%02x%02x\n",
                   opcode_buf[0], opcode_buf[1], opcode_buf[2]);
    console_printf("  Payload length:   %u bytes\n", buf->om_len);
    console_printf("  Payload data:     ");
    for (i = 0; i < buf->om_len && i < 32; i++) {
        console_printf("%02x ", buf->om_data[i]);
    }
    if (buf->om_len > 32) {
        console_printf("...");
    }
    console_printf("\n");
    console_printf("========================================\n\n");

    return 0;
}

static const struct bt_mesh_model_op test_model_op[] = {
    { TEST_OPCODE, 0, test_model_recv },
    BT_MESH_MODEL_OP_END,
};

#if MYNEWT_VAL(BLE_MESH_CFG_CLI)
static struct bt_mesh_cfg_cli cfg_cli;
#endif

static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
#if MYNEWT_VAL(BLE_MESH_CFG_CLI)
    BT_MESH_MODEL_CFG_CLI(&cfg_cli),
#endif
};

#define HEALTH_MODEL_IDX  1
#define CFG_CLI_MODEL_IDX 2

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_VENDOR, TEST_MODEL_ID, test_model_op,
              &test_model_pub, NULL),
};

#define TEST_VND_IDX       0

static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
    .cid = CID_VENDOR,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
    console_printf("OOB Number: %u\n", number);
    return 0;
}

static void prov_complete(uint16_t net_idx, uint16_t addr)
{
    console_printf("Local node provisioned, primary address 0x%04x\n", addr);
}

static void unprovisioned_beacon(uint8_t uuid[16],
                                 bt_mesh_prov_oob_info_t oob_info,
                                 uint32_t *uri_hash)
{
    int err;
    static uint16_t prov_addr = 0x0100;

    if (provisioning_in_progress) {
        console_printf("Provisioning already in progress, skipping beacon\n");
        return;
    }

    console_printf("Unprovisioned beacon received\n");
    console_printf("  UUID: %s\n", bt_hex(uuid, 16));
    console_printf("  OOB Info: 0x%02x\n", oob_info);

    provisioning_in_progress = true;

    /* Provision the device using PB-ADV */
    err = bt_mesh_provision_adv(uuid, NET_IDX, prov_addr, 0);
    if (err) {
        console_printf("Failed to start provisioning (err %d)\n", err);
        provisioning_in_progress = false;
        return;
    }

    console_printf("Provisioning started for address 0x%04x\n", prov_addr);
    prov_addr++;
}

static const uint8_t dev_uuid[16] = MYNEWT_VAL(BLE_MESH_DEV_UUID);

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
    .output_number = output_number,
    .complete = prov_complete,
    .node_added = prov_node_added,
    .unprovisioned_beacon = unprovisioned_beacon,
};

static void msg_send_work(struct ble_npl_event *ev)
{
    uint32_t ticks;

    (void)ev;
    if (last_node_addr != BT_MESH_ADDR_UNASSIGNED) {
        send_msg_to_node(&vnd_models[TEST_VND_IDX], last_node_addr);
    }
    ble_npl_time_ms_to_ticks(3000, &ticks);
    ble_npl_callout_reset(&msg_send_callout, ticks);
}

static void send_msg_to_node(struct bt_mesh_model *model, uint16_t dst_addr)
{
    struct os_mbuf *msg;
    struct bt_mesh_msg_ctx ctx = {
        .addr = dst_addr,
        .app_idx = APP_IDX,
        .net_idx = NET_IDX,
        .send_ttl = DEFAULT_TTL,
    };
    int rc;
    static uint32_t msg_counter;

    msg = NET_BUF_SIMPLE(BT_MESH_TX_SDU_MAX);
    if (!msg) {
        console_printf("Failed to allocate message buffer\n");
        return;
    }

    bt_mesh_model_msg_init(msg, TEST_OPCODE);

    /* Add a simple payload: message counter + timestamp placeholder */
    msg_counter++;
    net_buf_simple_add_u8(msg, (uint8_t)(msg_counter & 0xFF));
    net_buf_simple_add_u8(msg, (uint8_t)((msg_counter >> 8) & 0xFF));
    net_buf_simple_add_u8(msg, 0xDE);
    net_buf_simple_add_u8(msg, 0xAD);
    net_buf_simple_add_u8(msg, 0xBE);
    net_buf_simple_add_u8(msg, 0xEF);

    console_printf("[PROVISIONER] Sending test message #%u to 0x%04x\n",
                   msg_counter, dst_addr);
    rc = bt_mesh_model_send(model, &ctx, msg, NULL, NULL);
    if (rc) {
        console_printf("[PROVISIONER] Failed to send message (err %d)\n", rc);
    } else {
        console_printf("[PROVISIONER] Message sent successfully\n");
    }

    os_mbuf_free_chain(msg);
}

static void
blemesh_on_reset(int reason)
{
    BLE_HS_LOG(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
blemesh_on_sync(void)
{
    int err;
    uint8_t net_key[16];
    uint32_t ticks;
    int i;

    console_printf("Bluetooth initialized\n");

    err = bt_mesh_init(0, &prov, &comp);
    if (err) {
        console_printf("Initializing mesh failed (err %d)\n", err);
        return;
    }

    console_printf("Mesh initialized\n");

    mesh_initialized();

    /* Create CDB with a random NetKey for the provisioner */
    bt_rand(net_key, sizeof(net_key));
    err = bt_mesh_cdb_create(net_key);
    if (err) {
        console_printf("Failed to create CDB (err %d)\n", err);
        return;
    }

    console_printf("CDB created with NetKey: %s\n", bt_hex(net_key, 16));

    /* Provision the local node (provisioner) so it can provision other nodes */
    err = bt_mesh_provision(net_key, NET_IDX, 0, 0, PROV_ADDR, prov_dev_key);
    if (err) {
        console_printf("Failed to provision local node (err %d)\n", err);
        return;
    }

    console_printf("Provisioner node provisioned at address 0x%04x\n", PROV_ADDR);

    /* Allocate and store app key in CDB */
    struct bt_mesh_cdb_app_key *key;

    key = bt_mesh_cdb_app_key_alloc(NET_IDX, APP_IDX);
    if (!key) {
        console_printf("Failed to allocate app key in CDB\n");
        return;
    }
    memcpy(key->keys[0].app_key, app_key, 16);

    /* === Self-configuration via direct API (synchronous, no mesh transport) === */

    /* Add app key to mesh protocol stack */
    err = bt_mesh_app_key_add(APP_IDX, NET_IDX, app_key);
    if (err) {
        console_printf("Failed to add AppKey to mesh stack (err %d)\n", err);
    } else {
        console_printf("AppKey added to mesh stack (app_idx=0x%04x, net_idx=0x%04x)\n",
                       APP_IDX, NET_IDX);
    }

    /* Bind AppKey to Health Server model */
    {
        struct bt_mesh_model *m = &root_models[HEALTH_MODEL_IDX];
        for (i = 0; i < ARRAY_SIZE(m->keys); i++) {
            if (m->keys[i] == BT_MESH_KEY_UNUSED) {
                m->keys[i] = APP_IDX;
                console_printf("Bound AppKey 0x%04x to Health Server model (slot %d)\n",
                               APP_IDX, i);
                break;
            }
        }
        for (i = 0; i < ARRAY_SIZE(m->groups); i++) {
            if (m->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
                m->groups[i] = GROUP_ADDR;
                console_printf("Health Server subscribed to group 0x%04x (slot %d)\n",
                               GROUP_ADDR, i);
                break;
            }
        }
    }

    /* Bind AppKey to Test model */
    {
        struct bt_mesh_model *m = &vnd_models[TEST_VND_IDX];
        for (i = 0; i < ARRAY_SIZE(m->keys); i++) {
            if (m->keys[i] == BT_MESH_KEY_UNUSED) {
                m->keys[i] = APP_IDX;
                console_printf("Bound AppKey 0x%04x to Test model (slot %d)\n",
                               APP_IDX, i);
                break;
            }
        }
        for (i = 0; i < ARRAY_SIZE(m->groups); i++) {
            if (m->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
                m->groups[i] = GROUP_ADDR;
                console_printf("Test model subscribed to group 0x%04x (slot %d)\n",
                               GROUP_ADDR, i);
                break;
            }
        }
    }

    console_printf("Local self-configuration complete!\n");

    /* Set no authentication method for provisioning */
    bt_mesh_auth_method_set_none();

    /* Initialize callouts */
    last_node_addr = BT_MESH_ADDR_UNASSIGNED;
    ble_npl_callout_init(&msg_send_callout, nimble_port_get_dflt_eventq(),
                         msg_send_work, NULL);

    console_printf("Provisioner ready, waiting for unprovisioned nodes...\n");
}

static void prov_node_added(uint16_t net_idx, uint8_t uuid[16], uint16_t addr,
                            uint8_t num_elem)
{
    int i;
    uint32_t ticks;
    struct bt_mesh_cdb_node *node;

    console_printf("PROV_COMPLETE: Node provisioned, net_idx 0x%04x, addr 0x%04x, elem_count %u\n",
                   net_idx, addr, num_elem);
    console_printf("  UUID: %s\n", bt_hex(uuid, 16));

    provisioning_in_progress = false;

    /* Save node address for periodic messages */
    last_node_addr = addr;

    /* Start periodic messages to the node (1s delay to allow mesh stack to settle) */
    console_printf("Starting periodic messages to node 0x%04x\n", addr);
    ble_npl_time_ms_to_ticks(1000, &ticks);
    ble_npl_callout_reset(&msg_send_callout, ticks);

    /* Print CDB contents */
    if (atomic_test_bit(bt_mesh_cdb.flags, BT_MESH_CDB_VALID)) {
        console_printf("CDB nodes after provisioning:\n");
        for (i = 0; i < MYNEWT_VAL(BLE_MESH_CDB_NODE_COUNT); i++) {
            node = &bt_mesh_cdb.nodes[i];
            if (node->addr != BT_MESH_ADDR_UNASSIGNED) {
                console_printf("  Node[%d]: addr=0x%04x, net_idx=0x%04x, elem=%u\n",
                               i, node->addr, node->net_idx, node->num_elem);
                console_printf("    UUID: %s\n", bt_hex(node->uuid, 16));
                console_printf("    DevKey: %s\n", bt_hex(node->dev_key, 16));
            }
        }

        console_printf("CDB subnets:\n");
        for (i = 0; i < MYNEWT_VAL(BLE_MESH_CDB_SUBNET_COUNT); i++) {
            if (bt_mesh_cdb.subnets[i].net_idx != BT_MESH_KEY_UNUSED) {
                console_printf("  Subnet[%d]: net_idx=0x%04x, NetKey=%s\n",
                               i, bt_mesh_cdb.subnets[i].net_idx,
                               bt_hex(bt_mesh_cdb.subnets[i].keys[0].net_key, 16));
            }
        }

        console_printf("CDB app keys:\n");
        for (i = 0; i < MYNEWT_VAL(BLE_MESH_CDB_APP_KEY_COUNT); i++) {
            if (bt_mesh_cdb.app_keys[i].net_idx != BT_MESH_KEY_UNUSED) {
                console_printf("  AppKey[%d]: net_idx=0x%04x, app_idx=0x%04x, key=%s\n",
                               i, bt_mesh_cdb.app_keys[i].net_idx,
                               bt_mesh_cdb.app_keys[i].app_idx,
                               bt_hex(bt_mesh_cdb.app_keys[i].keys[0].app_key, 16));
            }
        }
    }
}

void
nimble_host_task(void *param)
{
    bt_mesh_pub_msg_health_pub = NET_BUF_SIMPLE(0);
    health_pub.msg = bt_mesh_pub_msg_health_pub;

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = blemesh_on_reset;
    ble_hs_cfg.sync_cb = blemesh_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_run();
}