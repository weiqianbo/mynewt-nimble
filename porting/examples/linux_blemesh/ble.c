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
#include <stdlib.h>
#include "mesh/mesh.h"
#include "console/console.h"

/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "mesh/glue.h"
#include "mesh/health_srv.h"
#include "mesh/cfg.h"

#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

/* Company ID */
#define CID_VENDOR 0x05C3
#define STANDARD_TEST_ID 0x00
#define TEST_ID 0x01
static int recent_test_id = STANDARD_TEST_ID;

/* Mesh network parameters - must match provisioner */
#define PROV_ADDR        0x0001
#define NET_IDX          0x0000
#define APP_IDX          0x0000
#define GROUP_ADDR       0xc123
#define DEFAULT_TTL      31

/* Application key - must match provisioner's app_key */
static const uint8_t app_key[16] = {
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
};

/* Test model configuration - shared by both sides */
#define TEST_MODEL_ID    0x0001
#define TEST_OPCODE      BT_MESH_MODEL_OP_3(0x02, CID_VENDOR)

#define FAULT_ARR_SIZE 2

/* Timer for periodic messages */
static struct ble_npl_callout msg_send_callout;

static bool has_reg_fault = true;

static int
fault_get_cur(struct bt_mesh_model *model,
              uint8_t *test_id,
              uint16_t *company_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE-1] = 0xff };

    console_printf("fault_get_cur() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;
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

    console_printf("fault_get_reg() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;

    if (has_reg_fault) {
        uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE-1] = 0xff };

        *fault_count = MIN(*fault_count, sizeof(reg_faults));
        memcpy(faults, reg_faults, *fault_count);
    } else {
        *fault_count = 0;
    }

    return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    has_reg_fault = false;

    return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    if (test_id != STANDARD_TEST_ID && test_id != TEST_ID) {
        return -BLE_HS_EINVAL;
    }

    recent_test_id = test_id;
    has_reg_fault = true;
    bt_mesh_fault_update(bt_mesh_model_elem(model));

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

static struct bt_mesh_model_pub health_pub;

static void
health_pub_init(void)
{
    health_pub.msg  = BT_MESH_HEALTH_FAULT_MSG(0);
}

static struct bt_mesh_model_pub gen_level_pub;
static struct bt_mesh_model_pub gen_onoff_pub;

static uint8_t gen_on_off_state;
static int16_t gen_level_state;

/* Test model - for bidirectional messaging with logging */
static struct bt_mesh_model_pub test_model_pub;
static uint8_t msg_counter;

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
    console_printf("[DEVICE] Received mesh message\n");
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

static int gen_onoff_status(struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    uint8_t *status;
    int rc;

    console_printf("#mesh-onoff STATUS\n");

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x04));
    status = net_buf_simple_add(msg, 1);
    *status = gen_on_off_state;

    rc = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
    if (rc) {
        console_printf("#mesh-onoff STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
    return rc;
}

static int gen_onoff_get(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-onoff GET\n");

    return gen_onoff_status(model, ctx);
}

static int gen_onoff_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-onoff SET\n");

    gen_on_off_state = buf->om_data[0];

    return gen_onoff_status(model, ctx);
}

static int gen_onoff_set_unack(struct bt_mesh_model *model,
                struct bt_mesh_msg_ctx *ctx,
                struct os_mbuf *buf)
{
    console_printf("#mesh-onoff SET-UNACK\n");

    gen_on_off_state = buf->om_data[0];
    return 0;
}

static const struct bt_mesh_model_op gen_onoff_op[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x01), 0, gen_onoff_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x02), 2, gen_onoff_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x03), 2, gen_onoff_set_unack },
    BT_MESH_MODEL_OP_END,
};

static void gen_level_status(struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(4);

    console_printf("#mesh-level STATUS\n");

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x08));
    net_buf_simple_add_le16(msg, gen_level_state);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        console_printf("#mesh-level STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
}

static int gen_level_get(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-level GET\n");

    gen_level_status(model, ctx);
    return 0;
}

static int gen_level_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level SET: level=%d\n", level);

    gen_level_status(model, ctx);

    gen_level_state = level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int gen_level_set_unack(struct bt_mesh_model *model,
                struct bt_mesh_msg_ctx *ctx,
                struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level SET-UNACK: level=%d\n", level);

    gen_level_state = level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int gen_delta_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_status(model, ctx);

    gen_level_state += delta_level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int gen_delta_set_unack(struct bt_mesh_model *model,
                struct bt_mesh_msg_ctx *ctx,
                struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_state += delta_level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int gen_move_set(struct bt_mesh_model *model,
             struct bt_mesh_msg_ctx *ctx,
             struct os_mbuf *buf)
{
    return 0;
}

static int gen_move_set_unack(struct bt_mesh_model *model,
                   struct bt_mesh_msg_ctx *ctx,
                   struct os_mbuf *buf)
{
    return 0;
}

static const struct bt_mesh_model_op gen_level_op[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x05), 0, gen_level_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x06), 3, gen_level_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x07), 3, gen_level_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x09), 5, gen_delta_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0a), 5, gen_delta_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x0b), 3, gen_move_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0c), 3, gen_move_set_unack },
    BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_op,
              &gen_onoff_pub, NULL),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, gen_level_op,
              &gen_level_pub, NULL),
};

static struct bt_mesh_model_pub vnd_model_pub;

static int vnd_model_recv(struct bt_mesh_model *model,
                           struct bt_mesh_msg_ctx *ctx,
                           struct os_mbuf *buf)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    int rc;

    console_printf("#vendor-model-recv\n");

    console_printf("data:%s len:%d\n", bt_hex(buf->om_data, buf->om_len),
                   buf->om_len);

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_3(0x01, CID_VENDOR));
    os_mbuf_append(msg, buf->om_data, buf->om_len);

    rc = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
    if (rc) {
        console_printf("#vendor-model-recv: send rsp failed\n");
    }

    os_mbuf_free_chain(msg);
    return rc;
}

static const struct bt_mesh_model_op vnd_model_op[] = {
        { BT_MESH_MODEL_OP_3(0x01, CID_VENDOR), 0, vnd_model_recv },
        BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_VENDOR, TEST_MODEL_ID, test_model_op,
              &test_model_pub, NULL),
    BT_MESH_MODEL_VND(CID_VENDOR, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, vnd_model_op,
              &vnd_model_pub, NULL),
};

#define TEST_VND_IDX  0

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

static void send_msg_to_provisioner(struct bt_mesh_model *model)
{
    int rc;

    if (!test_model_pub.msg) {
        console_printf("[DEVICE] Publication buffer not initialized\n");
        return;
    }

    /* Reset and prepare the publication message */
    net_buf_simple_reset(test_model_pub.msg);
    bt_mesh_model_msg_init(test_model_pub.msg, TEST_OPCODE);

    /* Add a simple payload: message counter + magic bytes */
    msg_counter++;
    net_buf_simple_add_u8(test_model_pub.msg, msg_counter);
    net_buf_simple_add_u8(test_model_pub.msg, 0xCA);
    net_buf_simple_add_u8(test_model_pub.msg, 0xFE);
    net_buf_simple_add_u8(test_model_pub.msg, 0xBA);
    net_buf_simple_add_u8(test_model_pub.msg, 0xBE);

    console_printf("[DEVICE] Publishing test message #%u to provisioner 0x%04x\n",
                   msg_counter, test_model_pub.addr);

    rc = bt_mesh_model_publish(model);
    if (rc) {
        console_printf("[DEVICE] Failed to publish message (err %d)\n", rc);
    } else {
        console_printf("[DEVICE] Message published successfully\n");
    }
}

static void msg_send_work(struct ble_npl_event *ev)
{
    uint32_t ticks;

    (void)ev;
    console_printf("[DEVICE] msg_send_work: is_provisioned=%d\n",
                   bt_mesh_is_provisioned());
    if (bt_mesh_is_provisioned()) {
        send_msg_to_provisioner(&vnd_models[TEST_VND_IDX]);
    }
    ble_npl_time_ms_to_ticks(3000, &ticks);
    ble_npl_callout_reset(&msg_send_callout, ticks);
}

static void prov_complete(uint16_t net_idx, uint16_t addr)
{
    int i;
    uint32_t ticks;
    struct bt_mesh_model *m;

    console_printf("\n========================================\n");
    console_printf("[DEVICE] Provisioning completed!\n");
    console_printf("========================================\n");
    console_printf("  NetKey index:  0x%04x\n", net_idx);
    console_printf("  Node address:  0x%04x\n", addr);
    console_printf("========================================\n\n");

    /* Self-configure: add AppKey and bind to models locally */
    {
        // /* Delete the settings file to clear any stale AppKeys from previous runs */
        // system("rm -f /tmp/bt_mesh_settings.conf");
        // console_printf("[DEVICE] Cleared persisted settings\n");

        int err = bt_mesh_app_key_add(APP_IDX, net_idx, app_key);
        if (err) {
            console_printf("[DEVICE] Failed to add AppKey (err %d)\n", err);
        } else {
            console_printf("[DEVICE] AppKey added (app_idx=0x%04x, net_idx=0x%04x)\n",
                           APP_IDX, net_idx);
        }

        /* Bind AppKey to Health Server */
        m = &root_models[1];
        for (i = 0; i < ARRAY_SIZE(m->keys); i++) {
            if (m->keys[i] == BT_MESH_KEY_UNUSED) {
                m->keys[i] = APP_IDX;
                console_printf("[DEVICE] Bound AppKey to Health Server (slot %d)\n", i);
                break;
            }
        }

        /* Bind AppKey to Test Model */
        m = &vnd_models[TEST_VND_IDX];
        for (i = 0; i < ARRAY_SIZE(m->keys); i++) {
            if (m->keys[i] == BT_MESH_KEY_UNUSED) {
                m->keys[i] = APP_IDX;
                console_printf("[DEVICE] Bound AppKey to Test Model (slot %d)\n", i);
                break;
            }
        }

        /* Configure Test Model publication parameters */
        test_model_pub.addr = PROV_ADDR;
        test_model_pub.key = APP_IDX;
        test_model_pub.ttl = DEFAULT_TTL;
        if (!test_model_pub.msg) {
            test_model_pub.msg = NET_BUF_SIMPLE(BT_MESH_TX_SDU_MAX);
        }
        console_printf("[DEVICE] Test Model publication configured (addr=0x%04x)\n", PROV_ADDR);

        /* Subscribe Health Server to group */
        m = &root_models[1];
        for (i = 0; i < ARRAY_SIZE(m->groups); i++) {
            if (m->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
                m->groups[i] = GROUP_ADDR;
                console_printf("[DEVICE] Health Server subscribed to group 0x%04x\n",
                               GROUP_ADDR);
                break;
            }
        }

        /* Subscribe Test Model to group */
        m = &vnd_models[TEST_VND_IDX];
        for (i = 0; i < ARRAY_SIZE(m->groups); i++) {
            if (m->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
                m->groups[i] = GROUP_ADDR;
                console_printf("[DEVICE] Test Model subscribed to group 0x%04x\n",
                               GROUP_ADDR);
                break;
            }
        }

        console_printf("[DEVICE] Self-configuration complete!\n\n");
    }

    /* Start periodic messages to provisioner */
    console_printf("[DEVICE] Starting periodic messages (3s interval)...\n");
    ble_npl_time_ms_to_ticks(3000, &ticks);
    ble_npl_callout_reset(&msg_send_callout, ticks);
}

static const uint8_t dev_uuid[16] = MYNEWT_VAL(BLE_MESH_DEV_UUID);

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
    .output_number = output_number,
    .complete = prov_complete,
};

static void
blemesh_on_reset(int reason)
{
    BLE_HS_LOG(ERROR, "Resetting state; reason=%d\n", reason);
}

void mesh_initialized(void);

static void
blemesh_on_sync(void)
{
    int err;
    uint32_t ticks;

    console_printf("Bluetooth initialized\n");

    err = bt_mesh_init(0, &prov, &comp);
    if (err) {
        console_printf("Initializing mesh failed (err %d)\n", err);
        return;
    }

#if (MYNEWT_VAL(BLE_MESH_SHELL))
    shell_register_default_module("mesh");
#endif

    console_printf("Mesh initialized\n");

    mesh_initialized();

    // if (IS_ENABLED(CONFIG_SETTINGS)) {
    //     settings_load();
    // }

    if (bt_mesh_is_provisioned()) {
        printk("Mesh network restored from flash\n");
    }

    bt_mesh_prov_enable(BT_MESH_PROV_GATT | BT_MESH_PROV_ADV);

    /* Initialize message send callout */
    ble_npl_callout_init(&msg_send_callout, nimble_port_get_dflt_eventq(),
                         msg_send_work, NULL);

    /* If already provisioned, start messaging immediately */
    if (bt_mesh_is_provisioned()) {
        console_printf("Already provisioned, starting periodic messages...\n");
        ble_npl_time_ms_to_ticks(3000, &ticks);
        ble_npl_callout_reset(&msg_send_callout, ticks);
    }
}

void
nimble_host_task(void *param)
{
    health_pub_init();

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = blemesh_on_reset;
    ble_hs_cfg.sync_cb = blemesh_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_run();
}