//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    framework_ut.cpp
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-2-25




#include "framework.h"


int64_t g_req_end;


static void request_a_x_cb(void *arg, void *message, int result)
{
    rte_atomic32_t *response_a_s_done = (rte_atomic32_t *)arg;
    struct framework_test_msg *msg = (struct framework_test_msg *)message;
    ASSERT_EQ(result, FRAMEWORK_TEST_RESULT);

    if (g_debug_level < 2) {
        printf("%s(), %d, message = %p\n", __func__, __LINE__, message);
    }

    if (msg->cmd == FRAMEWORK_TEST_MSG_AA_CMD) {
        ASSERT_EQ(rte_atomic32_read(&msg->result), FRAMEWORK_TEST_MSG_AA_RESULT);
    } else {
        ASSERT_EQ(rte_atomic32_read(&msg->result), FRAMEWORK_TEST_MSG_AS_RESULT);
    }

    int ret = ccdk_framework_mempool_put_msg(msg);
    ASSERT_EQ(ret, EC_CCDK_OK);
    rte_atomic32_inc(response_a_s_done);
    g_req_end = rte_rdtsc_precise();

    if (g_debug_level < 2) {
        printf("%s(), %d, arg = %p, result = %d\n", __func__, __LINE__, arg, result);
    }
}


void framework_a_x(int is_aa)
{
    rte_atomic32_t request_a_s_done;
    rte_atomic32_t response_a_s_done;
    int ret = ccdk_framework_open_session(request_a_x_cb, &response_a_s_done);
    ASSERT_TRUE(ret >= 0);
    uint32_t session_id = (uint32_t)ret;
    rte_atomic32_init(&request_a_s_done);
    rte_atomic32_init(&response_a_s_done);
    int64_t start_ts = rte_rdtsc_precise();

    for (int i = 0; i < g_req_num; i++) {
        int ret = 0;
        struct framework_test_msg *msg = (struct framework_test_msg *)ccdk_framework_mempool_get_msg(
                                             FRAMEWORK_TEST_MSG_SIZE);

        if (!msg) {
            printf("no enough msg, wait for a moment.\n");
            sleep(5);
            continue;
        }

        memset(msg, 0, sizeof(struct framework_test_msg));

        if (is_aa) {
            msg->cmd = FRAMEWORK_TEST_MSG_AA_CMD;
        } else {
            msg->cmd = FRAMEWORK_TEST_MSG_AS_CMD;
        }

        ret = ccdk_framework_handle_async_msg(DMA_H2C_SERVER_ID, msg);
        ASSERT_EQ(ret, EC_CCDK_OK);
        rte_atomic32_inc(&request_a_s_done);

        if (g_interval) {
            usleep(g_interval);
        }
    }

    while (1) {
        usleep(10000);

        if (rte_atomic32_read(&request_a_s_done) == rte_atomic32_read(&response_a_s_done)) {
            printf("request_a_s_done = %d, response_a_s_done = %d\n", rte_atomic32_read(&request_a_s_done),
                   rte_atomic32_read(&response_a_s_done));
            break;
        }
    }

    int64_t req_latency = g_req_end - start_ts;
    double req_latency_us = ((double)req_latency * 1000000) / rte_get_tsc_hz();
    double qps = (1000000 * g_req_num) / req_latency_us;
    printf("ax, session_id = %d, g_req_num = %d, start_ts = 0x%lu, end_ts = 0x%lu, req_latency = %ld  = %f us, qps = %f\n",
           session_id, g_req_num, start_ts, g_req_end, req_latency, req_latency_us, qps);
    print_session_latency(session_id);
    ccdk_framework_close_session();
}


void framework_s_x(int is_sa)
{
    int ret = ccdk_framework_open_session(NULL, NULL);
    ASSERT_TRUE(ret >= 0);
    uint32_t session_id = (uint32_t)ret;
    int64_t start_ts = rte_rdtsc_precise();

    for (int i = 0; i < g_req_num; i++) {
        int ret = 0;
        struct framework_test_msg *msg = (struct framework_test_msg *)ccdk_framework_mempool_get_msg(
                                             FRAMEWORK_TEST_MSG_SIZE);

        if (!msg) {
            printf("no enough msg, wait for a moment.\n");
            sleep(5);
            continue;
        }

        memset(msg, 0, sizeof(struct framework_test_msg));

        if (is_sa) {
            msg->cmd = FRAMEWORK_TEST_MSG_SA_CMD;
        } else {
            msg->cmd = FRAMEWORK_TEST_MSG_SS_CMD;
        }

        ret = ccdk_framework_handle_sync_msg(DMA_H2C_SERVER_ID, msg, 3000);

        if (ret != FRAMEWORK_TEST_RESULT) {
            print_session_latency(session_id);
        }

        ASSERT_EQ(ret, FRAMEWORK_TEST_RESULT);

        if (is_sa) {
            ASSERT_EQ(rte_atomic32_read(&msg->result), FRAMEWORK_TEST_MSG_SA_RESULT);
        } else {
            ASSERT_EQ(rte_atomic32_read(&msg->result), FRAMEWORK_TEST_MSG_SS_RESULT);
        }

        ret = ccdk_framework_mempool_put_msg(msg);

        if (ret < EC_CCDK_OK) {
            printf("ccdk_framework_mempool_put_msg. ret = %d\n", ret);
        }
    }

    int64_t end_ts  = rte_rdtsc_precise();
    int64_t req_latency = end_ts - start_ts;
    //double req_latency_us = ((double)req_latency * 1000000) / rte_get_tsc_hz();
    //double qps = double(1000000 * g_req_num)  / req_latency_us;
	//double qqps = double(1 * g_req_num * 1000000) / req_latency_us;
    //printf("sx, session_id = %d, g_req_num = %d, start_ts = 0x%lu, end_ts = 0x%lu, req_latency = %ld  = %f us, qps = %f, qqps = %f\n",
    //       session_id, g_req_num, start_ts, end_ts, req_latency, req_latency_us, qps, qqps);

	double req_latency_ms = ((double)req_latency * 1000) / rte_get_tsc_hz();
    double qps = double(1000 * g_req_num)  / req_latency_ms;
    printf("sx, session_id = %d, g_req_num = %d, start_ts = 0x%lu, end_ts = 0x%lu, req_latency = %ld  = %f ms, qps = %f\n",
           session_id, g_req_num, start_ts, end_ts, req_latency, req_latency_ms, qps);
    print_session_latency(session_id);
    ccdk_framework_close_session();
}



