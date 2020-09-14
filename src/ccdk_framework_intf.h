//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    ccdk_framework_intf.h
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-2-3


#ifndef _CCDK_FRAMEWORK_INTF__H_
#define _CCDK_FRAMEWORK_INTF__H_

#include <rte_eal.h>
#include "rte_cycles.h"

typedef void *(*ccdk_intr_action_t)(void *arg);

typedef void (*ccdk_session_req_action_t)(void *arg, void *request, int result);

typedef int (*ccdk_server_action_t)(void *arg, void *request);

struct ccdk_session_latency {
    uint32_t session_id;
    uint64_t min_start_2_action_latency;
    uint64_t min_action_latency;
    uint64_t min_action_2_intr_latency;
    uint64_t min_intr_2_complete_latency;
    uint64_t min_start_2_complete_latency;

    uint64_t max_start_2_action_latency;
    uint64_t max_action_latency;
    uint64_t max_action_2_intr_latency;
    uint64_t max_intr_2_complete_latency;
    uint64_t max_start_2_complete_latency;


    uint64_t total_start_2_action_cnt;
    uint64_t total_start_2_action_latency;
    uint64_t avg_start_2_action_latency;

    uint64_t total_action_cnt;
    uint64_t total_action_latency;
    uint64_t avg_action_latency;

    uint64_t total_action_2_intr_cnt;
    uint64_t total_action_2_intr_latency;
    uint64_t avg_action_2_intr_latency;

    uint64_t total_intr_2_complete_cnt;
    uint64_t total_intr_2_complete_latency;
    uint64_t avg_intr_2_complete_latency;

    uint64_t total_start_2_complete_cnt;
    uint64_t total_start_2_complete_latency;
    uint64_t avg_start_2_complete_latency;
};


#define CCDK_MAX_SESSION_NUM	256
#define CCDK_MGNT_CMD_GET_LATENCY	0x00000001
struct ccdk_framework_mgnt_session_latency_msg {
    int session_cnt;
    struct ccdk_session_latency latency[CCDK_MAX_SESSION_NUM];
};

#define MGNT_SERVER_ID	0xffffffff
struct ccdk_framework_mgnt_msg {
    uint32_t cmd;
    uint32_t data_len;
    uint8_t data[0];
};
#define CCDK_SIEZOF_MGNT_MSG sizeof(struct ccdk_framework_mgnt_msg)


/// @brief intr
int ccdk_framework_intr_action_register(int vector, ccdk_intr_action_t action, void *arg);
int ccdk_framework_intr_init(struct rte_intr_handle *intr_handle);

/// @brief session
int ccdk_framework_open_session(ccdk_session_req_action_t action, void *arg);
int ccdk_framework_close_session(void);
int ccdk_framework_get_session_latency_info(uint32_t session_id, struct ccdk_session_latency *latency);

/// @brief server
int ccdk_framework_register_server(const char *name, const uint32_t server_id, int thread_num,
                                   ccdk_server_action_t action, void *arg, int core_id);

/// @brief request
int ccdk_framework_set_msg_done(void *msg, int result);
int ccdk_framework_handle_sync_msg(uint32_t server_id, void *msg, int timeout);
int ccdk_framework_handle_async_msg(uint32_t server_id, void *msg);

void *ccdk_framework_mempool_get_msg(int size);
int ccdk_framework_mempool_put_msg(void *msg);
void *ccdk_framework_malloc_msg(int size);
void ccdk_framework_free_msg(void *msg);

/// @brief init
int ccdk_framework_init(void);
void ccdk_framework_cleanup(void);


#endif