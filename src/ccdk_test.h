//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    ccdk_test.h
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-4-2


#ifndef	_CCDK_TEST_H_
#define	_CCDK_TEST_H_

#include "ccdk_framework_intf.h"

#define CCDK_TEST_RESULT	-6
#define CCDK_TEST_SERVER_ID	0x11112222
#define CCDK_TEST_MSG_RESULT	200


#define CCDK_MSG_MAX_ARGC	10
struct ccdk_msg_desc {
    /// @brief session
    ccdk_session_req_action_t action;
    void *arg;

    /// @brief server
    int pfidx;
    int argc;
    void *argv[CCDK_MSG_MAX_ARGC];

    int is_async;

    int resp_chn = -1; //the eventfd for response
    rte_atomic32_t resp_done;
    uint32_t server_id;
    int is_block; 	//根据这个标记,表示当前req执行完成前后续req是否继续下发到primary执行
    int timeout;
    void *pcbfunc;
};

int ccdk_server_cb(void *arg, void *message);
int ccdk_handle_msg(struct ccdk_msg_desc *msg_desc);
void ccdk_ut_ss(void);

#endif