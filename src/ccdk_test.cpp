//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    ccdk_test.cpp
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-4-2


#include "ccdk_test.h"
#include "framework.h"

int ccdk_dma_h2c(void *argv1, void *argv2)
{
    printf("%s, argv2 = %s\n", __func__, argv2);
    return CCDK_TEST_RESULT;
}

int ccdk_dma_c2h(void *argv1, void *argv2)
{
    printf("%s, argv2 = %s\n", __func__, argv2);
    return CCDK_TEST_RESULT;
}

int ccdk_server_cb(void *arg, void *message)
{
    int ret = 0;
    struct ccdk_msg_desc *msg_desc = (struct ccdk_msg_desc *)message;
    rte_atomic32_set(&msg_desc->resp_done, CCDK_TEST_MSG_RESULT);

    if (g_debug_level < 2) {
        printf("%s(), %d\n", __func__, __LINE__);
    }

    switch (msg_desc->pfidx) {
    case 0:
        return ccdk_dma_h2c(msg_desc->argv[0], msg_desc->argv[1]);
        break;

    case 1:
        return ccdk_dma_c2h(msg_desc->argv[0], msg_desc->argv[1]);
        break;

    default:
        break;
    };

    return CCDK_TEST_RESULT;
}


int ccdk_handle_msg(struct ccdk_msg_desc *msg_desc)
{
    int ret = EC_CCDK_OK;
    uint32_t session_id;
    ret = ccdk_framework_open_session(msg_desc->action, msg_desc->arg);

    if (unlikely(ret < EC_CCDK_OK)) {
        printf("ccdk_framework_open_session failed, ret = %d\n", ret);
        return ret;
    }

    if (msg_desc->is_async) {
        ret = ccdk_framework_handle_async_msg(msg_desc->server_id, msg_desc);
    } else {
        ret = ccdk_framework_handle_sync_msg(msg_desc->server_id, msg_desc, msg_desc->timeout);
    }

    return ret;
}

void ccdk_ut_ss(void)
{
    uint32_t session_id = 0;
    int64_t start_ts = rte_rdtsc_precise();
    void *msg = rte_zmalloc("ccdk-test", 32, 0);
    strcpy(msg, "msg-test");

    for (int i = 0; i < 2; i++) {
        int ret = 0;
        struct ccdk_msg_desc *msg_desc = (struct ccdk_msg_desc *)ccdk_framework_mempool_get_msg(
                                             sizeof(struct ccdk_msg_desc));

        if (!msg_desc) {
            printf("no enough msg, wait for a moment.\n");
            sleep(5);
            continue;
        }

        memset(msg_desc, 0, sizeof(struct ccdk_msg_desc));
        msg_desc->action = NULL;
        msg_desc->arg = NULL;
        msg_desc->pfidx = i;
        msg_desc->argc = 2;
        msg_desc->argv[0] = NULL;
        msg_desc->argv[1] = msg;
        msg_desc->is_async = 0;
        msg_desc->server_id = CCDK_TEST_SERVER_ID;
        msg_desc->timeout = 3000;
        ret = ccdk_handle_msg(msg_desc);

        if (ret != CCDK_TEST_RESULT) {
            print_session_latency(session_id);
        }

        //printf("ret = %d, resp_done = %d\n", ret, rte_atomic32_read(&msg_desc->resp_done));
        ASSERT_EQ(ret, CCDK_TEST_RESULT);
        ASSERT_EQ(rte_atomic32_read(&msg_desc->resp_done), CCDK_TEST_MSG_RESULT);
        ret = ccdk_framework_mempool_put_msg(msg_desc);

        if (ret < EC_CCDK_OK) {
            printf("ccdk_framework_mempool_put_msg. ret = %d\n", ret);
        }
    }

    int64_t end_ts  = rte_rdtsc_precise();
    int64_t req_latency = end_ts - start_ts;
    double req_latency_us = ((double)req_latency * 1000000) / rte_get_tsc_hz();
    double qps = (1000000 * g_req_num)  / req_latency_us;
    printf("sx, session_id = %d, g_req_num = %d, start_ts = 0x%lu, end_ts = 0x%lu, req_latency = %ld  = %f us, qps = %f\n",
           session_id, g_req_num, start_ts, end_ts, req_latency, req_latency_us, qps);
    print_session_latency(session_id);
    ccdk_framework_close_session();
    rte_free(msg);
}