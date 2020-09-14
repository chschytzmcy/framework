//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    framework.h
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-2-23


#ifndef	_FRAMEWORK_H_
#define	_FRAMEWORK_H_
#include <thread>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include <rte_lcore.h>

#include "gtest/gtest.h"
#include "gflags/gflags.h"

#include "ccdk_framework_intf.h"
#include "ccdk_pmd_common.h"

//##define MUTLI_TEST

#define FRAMEWORK_TEST_RESULT	-3

#define DMA_H2C_SERVER_ID	0x10000001
#define DMA_C2H_SERVER_ID	0x10000002
#define TASK_SERVER_ID	0x20000001
#define OPERATION_SERVER_ID	0x30000001

#define FRAMEWORK_TEST_MSG_SS_CMD	0x00000001
#define FRAMEWORK_TEST_MSG_SA_CMD	0x00000002
#define FRAMEWORK_TEST_MSG_AS_CMD	0x00000003
#define FRAMEWORK_TEST_MSG_AA_CMD	0x00000004

#define FRAMEWORK_TEST_MSG_SS_RESULT	100
#define FRAMEWORK_TEST_MSG_SA_RESULT	101
#define FRAMEWORK_TEST_MSG_AS_RESULT	102
#define FRAMEWORK_TEST_MSG_AA_RESULT	103
struct framework_test_msg {
    uint32_t cmd;
    void *mempool_cache;
    rte_atomic32_t result;
};

#define FRAMEWORK_TEST_MSG_SIZE	sizeof(struct framework_test_msg)

extern uint32_t g_req_num;
extern uint32_t g_debug_level;
extern uint32_t g_proc_num;
extern uint32_t g_thread_num;
extern uint32_t g_interval;

void print_session_latency(uint32_t session_id);

void framework_s_x(int is_sa);
void framework_a_x(int is_aa);

void framework_st_s_x(int is_sa);
void framework_st_a_x(int is_aa);


#endif