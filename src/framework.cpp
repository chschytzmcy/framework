//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    framework.cpp
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-1-1

#include "framework.h"
#include "ccdk_test.h"

DEFINE_string(corelist, "", "binding core list");
DEFINE_int32(server_core, 0, "server core");
DEFINE_int32(server_thread, 1, "server thread number");
DEFINE_int32(req_num, 1, "request number");
DEFINE_int32(debug, 0, "debug level, 0:debug, 1:info, 2:error");
DEFINE_string(log, "", "log file name");

DEFINE_string(role, "client", "role");

DEFINE_int32(proc_num, 1, "process number");
DEFINE_int32(thread_num, 1, "thread number");

DEFINE_int32(server_action_delay, 0, "server action delay (us)");
DEFINE_int32(interval, 0, "request interval (us)");

DEFINE_int32(operation, 0, "operation mode and number");
DEFINE_int32(monitor_session_id, -1, "monitor session id");

uint32_t g_req_num = 1;
uint32_t g_debug_level = 0;
uint32_t g_proc_num = 1;
uint32_t g_thread_num = 1;
uint32_t g_interval = 0;

struct rte_ring *g_intr_ring;

void dma_h2c_async_cb(struct framework_test_msg *msg)
{
    if (FLAGS_debug < 2) {
        printf("%s(), %d, msg = %p, msg->cmd = 0x%x\n", __func__, __LINE__, msg, msg->cmd);
    }

    switch (msg->cmd) {
    case FRAMEWORK_TEST_MSG_SA_CMD:
        rte_atomic32_set(&msg->result, FRAMEWORK_TEST_MSG_SA_RESULT);
        break;

    case FRAMEWORK_TEST_MSG_AA_CMD:
        rte_atomic32_set(&msg->result, FRAMEWORK_TEST_MSG_AA_RESULT);
        break;

    default:
        break;
    };

    int ret = ccdk_framework_set_msg_done(msg, FRAMEWORK_TEST_RESULT);

    if (FLAGS_debug < 2) {
        printf("%s(), %d, ret = %d, msg = %p\n", __func__, __LINE__, ret, msg);
    }
}

static void *dma_h2c_intr_async_task(void *arg)
{
    struct framework_test_msg *msg = NULL;
    printf("%s(), %d, dma_h2c_intr_async_task running\n", __func__, __LINE__);

    while (1) {
        if (rte_ring_dequeue(g_intr_ring, (void **)&msg) < 0) {
            usleep(100);
            continue;
        }

        dma_h2c_async_cb(msg);
    }
}


int dma_h2c_cb(void *arg, void *message)
{
    int ret = 0;
    struct framework_test_msg *msg = (struct framework_test_msg *)message;

    if (FLAGS_server_action_delay) {
        usleep(FLAGS_server_action_delay);
    }

    if (msg) {
        if (FLAGS_debug < 2) {
            printf("%s(), %d, msg = %p, msg->cmd = 0x%x\n", __func__, __LINE__, msg, msg->cmd);
        }

        switch (msg->cmd) {
        case FRAMEWORK_TEST_MSG_SS_CMD:
            rte_atomic32_set(&msg->result, FRAMEWORK_TEST_MSG_SS_RESULT);
            break;

        case FRAMEWORK_TEST_MSG_AS_CMD:
            rte_atomic32_set(&msg->result, FRAMEWORK_TEST_MSG_AS_RESULT);
            break;

        case FRAMEWORK_TEST_MSG_SA_CMD:
            ret = rte_ring_enqueue(g_intr_ring, (void *)msg);
            return 1;
            break;

        case FRAMEWORK_TEST_MSG_AA_CMD:
            ret = rte_ring_enqueue(g_intr_ring, (void *)msg);
            return 1;
            break;

        default:
            break;
        };
    }

    if (FLAGS_debug < 2) {
        printf("%s(), %d\n", __func__, __LINE__);
    }

    return FRAMEWORK_TEST_RESULT;
}


int task_cb(void *arg, void *request)
{
    printf("%s(), %d, arg = %s\n", __func__, __LINE__, (char *)arg);
    return 0;
}
/*
int operation_cb(void *arg, void *message)
{
    int ret = 0;
    int session_cnt = 0;
    struct framework_session_latency_msg *msg = (struct framework_session_latency_msg *)message;
    struct ccdk_session_latency latency = {};

    for (int i = 0; i < MAX_SESSION_NUM; i++) {
        ret = ccdk_framework_get_session_latency_info(i, &msg->latency[session_cnt]);

        if (ret == EC_CCDK_OK) {
            session_cnt++;
        }
    }

    msg->session_cnt = session_cnt;
    return EC_CCDK_OK;
}
*/
static void print_session_latency_info(struct ccdk_session_latency &latency)
{
    printf("\n--------");
    printf("session_id = %d, rte_get_tsc_hz() = %lld\n", latency.session_id, rte_get_tsc_hz());
    printf("min_start_2_action_latency = %lld, min_action_latency = %lld, min_action_2_intr_latency = %lld, min_intr_2_complete_latency = %lld, min_start_2_complete_latency = %lld\n",
           latency.min_start_2_action_latency,
           latency.min_action_latency,
           latency.min_action_2_intr_latency,
           latency.min_intr_2_complete_latency,
           latency.min_start_2_complete_latency);
    printf("max_start_2_action_latency = %lld, max_action_latency = %lld, max_action_2_intr_latency = %lld, max_intr_2_complete_latency = %lld, max_start_2_complete_latency = %lld\n",
           latency.max_start_2_action_latency,
           latency.max_action_latency,
           latency.max_action_2_intr_latency,
           latency.max_intr_2_complete_latency,
           latency.max_start_2_complete_latency);
    printf("total_start_2_action_cnt = %lld, total_action_cnt = %lld, total_action_2_intr_cnt = %lld, total_intr_2_complete_cnt = %lld, total_start_2_complete_cnt = %lld\n",
           latency.total_start_2_action_cnt,
           latency.total_action_cnt,
           latency.total_action_2_intr_cnt,
           latency.total_intr_2_complete_cnt,
           latency.total_start_2_complete_cnt);
    printf("total_start_2_action_latency = %lld, total_action_latency = %lld, total_action_2_intr_latency = %lld, total_intr_2_complete_latency = %lld, total_start_2_complete_latency = %lld\n",
           latency.total_start_2_action_latency,
           latency.total_action_latency,
           latency.total_action_2_intr_latency,
           latency.total_intr_2_complete_latency,
           latency.total_start_2_complete_latency);
    printf("avg_start_2_action_latency = %lld, avg_action_latency = %lld, avg_action_2_intr_latency = %lld, avg_intr_2_complete_latency = %lld, avg_start_2_complete_latency = %lld\n",
           latency.avg_start_2_action_latency,
           latency.avg_action_latency,
           latency.avg_action_2_intr_latency,
           latency.avg_intr_2_complete_latency,
           latency.avg_start_2_complete_latency);
    printf("-------------------------------\n\n");
}


void print_session_latency(uint32_t session_id)
{
    struct ccdk_session_latency latency = {};
    int ret = ccdk_framework_get_session_latency_info(session_id, &latency);

    if (ret == 0) {
        print_session_latency_info(latency);
    }
}

void print_session_latency_task(void)
{
    while (1) {
        sleep(10);

        for (int i = 0; i < 1000; i++) {
            print_session_latency(i);
        }
    }
}

static void set_debug_level(int level)
{
    if (level == 2) {
        rte_log_set_global_level(RTE_LOG_ERR);
        //rte_log_set_level_pattern("CCDK-PMD", RTE_LOG_ERR);
        rte_log_set_level_regexp("CCDK-PMD", RTE_LOG_ERR);
    } else if (level == 1) {
        rte_log_set_global_level(RTE_LOG_INFO);
        //rte_log_set_level_pattern("CCDK-PMD", RTE_LOG_INFO);
        rte_log_set_level_regexp("CCDK-PMD", RTE_LOG_INFO);
    } else {
        rte_log_set_global_level(RTE_LOG_DEBUG);
        //rte_log_set_level_pattern("CCDK-PMD", (RTE_LOG_DEBUG));
        rte_log_set_level_regexp("CCDK-PMD", RTE_LOG_DEBUG);
    }
}

TEST(ut, ccdk)
{
    ccdk_ut_ss();
    GTEST_LOG_(INFO) << "ut ccdk exit now" << std::endl;
}


TEST(ut, ss)
{
    framework_s_x(0);
    GTEST_LOG_(INFO) << "ut ss exit now" << std::endl;
}

TEST(ut, sa)
{
    framework_s_x(1);
    GTEST_LOG_(INFO) << "ut sa exit now" << std::endl;
}

TEST(ut, as)
{
    framework_a_x(0);
    GTEST_LOG_(INFO) << "ut as exit now" << std::endl;
}


TEST(ut, aa)
{
    framework_a_x(1);
    GTEST_LOG_(INFO) << "ut aa exit now" << std::endl;
}


TEST(st, ss)
{
    framework_st_s_x(0);
    GTEST_LOG_(INFO) << "st ss exit now" << std::endl;
}

TEST(st, sa)
{
    framework_st_s_x(1);
    GTEST_LOG_(INFO) << "st sa exit now" << std::endl;
}

TEST(st, as)
{
    framework_st_a_x(0);
    GTEST_LOG_(INFO) << "st as exit now" << std::endl;
}

TEST(st, aa)
{
    framework_st_a_x(1);
    GTEST_LOG_(INFO) << "st aa exit now" << std::endl;
}

void operation_session(void)
{
    uint32_t session_id = 0;
    int ret = ccdk_framework_open_session(NULL, NULL);

    if (ret < EC_CCDK_OK) {
        return ret;
    }

    session_id = (uint32_t)ret;
    int msg_size = CCDK_SIEZOF_MGNT_MSG + sizeof(struct ccdk_framework_mgnt_session_latency_msg);
    struct ccdk_framework_mgnt_msg *mgnt_msg = (struct ccdk_framework_mgnt_msg *)ccdk_framework_malloc_msg(
                msg_size);

    if (unlikely(!mgnt_msg)) {
        printf("ccdk_framework_malloc_msg fail.\n");
        return;
    }

    mgnt_msg->cmd = CCDK_MGNT_CMD_GET_LATENCY;
    mgnt_msg->data_len = sizeof(struct ccdk_framework_mgnt_session_latency_msg);

    if (mgnt_msg) {
        for (int i = 0; i < FLAGS_operation; i++) {
            struct ccdk_framework_mgnt_session_latency_msg *session_latency = (struct
                    ccdk_framework_mgnt_session_latency_msg *)mgnt_msg->data;
            memset(session_latency, 0, sizeof(struct ccdk_framework_mgnt_session_latency_msg));
            ret = ccdk_framework_handle_sync_msg(MGNT_SERVER_ID, mgnt_msg, 3000);

            if (ret < EC_CCDK_OK) {
                printf("%s(), %d\n", __func__, __LINE__);
                return ret;
            }

            if (FLAGS_monitor_session_id >= 0 && FLAGS_monitor_session_id < session_latency->session_cnt) {
                print_session_latency_info(session_latency->latency[FLAGS_monitor_session_id]);
            } else {
                for (int j = 0; j < session_latency->session_cnt; j++) {
                    if (session_id == session_latency->latency[j].session_id) {
                        continue;
                    }

                    print_session_latency_info(session_latency->latency[j]);
                }
            }

            if (i != (FLAGS_operation - 1)) {
                sleep(10);
            }
        }
    }

    ccdk_framework_free_msg(mgnt_msg);
    ccdk_framework_close_session();
}

/*
static void *framework_test_task(void *arg)
{
    printf("%s(), %d, thread_id = %d\n", __func__, __LINE__, rte_sys_gettid());

    while (1) {
        //printf("%s(), %d, thread_id = %d\n", __func__, __LINE__, rte_sys_gettid());
        usleep(10);
    }
}
*/

static void *framework_test_task1(void *arg)
{
    printf("%s(), %d, thread_id = %d\n", __func__, __LINE__, rte_sys_gettid());

    while (1) {
        //printf("%s(), %d, thread_id = %d\n", __func__, __LINE__, rte_sys_gettid());
        sleep(1);

        if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
            if (0 == access("/home/cy187263/framewark/ccdk-pmd/app/framework/debug", F_OK)) {
                rte_log_set_global_level(RTE_LOG_DEBUG);
                //rte_log_set_level_pattern("CCDK-PMD", (RTE_LOG_DEBUG));
                rte_log_set_level_regexp("CCDK-PMD", RTE_LOG_DEBUG);
            }
        }
    }
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, false);
    int ret;

    if (FLAGS_log.length() > 0) {
        FILE *f = fopen(FLAGS_log.c_str(), "ab+");
        rte_openlog_stream(f);
    }

    rte_log_set_global_level(RTE_LOG_DEBUG);
    //rte_log_set_level_pattern("CCDK-PMD", (RTE_LOG_DEBUG));
    rte_log_set_level_regexp("CCDK-PMD", RTE_LOG_DEBUG);
    printf("%s(), %d, FLAGS_corelist = %s\n", __func__, __LINE__, FLAGS_corelist.data());

    if (FLAGS_corelist.length() == 0) {
        int dpdk_argc = 2;
        char *dpdk_argv[dpdk_argc] = {argv[0], "--proc-type=auto"};
        //char *dpdk_argv[dpdk_argc] = {argv[0], "--proc-type=auto", "-l 62"};
        ret = rte_eal_init(dpdk_argc, dpdk_argv);
    } else {
        int dpdk_argc = 3;
        char lcore[128] = {0};
        //snprintf(lcore, sizeof(lcore), "--master-lcore=%d", FLAGS_lcore);
        snprintf(lcore, sizeof(lcore), "-l %s", FLAGS_corelist.data());
        //char *dpdk_argv[dpdk_argc] = {argv[0], "--proc-type=auto", lcore, "-l 60,61"};
        char *dpdk_argv[dpdk_argc] = {argv[0], "--proc-type=auto", lcore};
        ret = rte_eal_init(dpdk_argc, dpdk_argv);
    }

    if (ret < 0) {
        return -1;
    }

    ret = ccdk_framework_init();

    if (ret < 0) {
        printf("%s(), %d\n", __func__, __LINE__);
        return ret;
    }

    struct framework_test_msg *msg = NULL;

    while (1) {
        msg = (struct framework_test_msg *)ccdk_framework_mempool_get_msg(
                  FRAMEWORK_TEST_MSG_SIZE);

        if (msg) {
            ccdk_framework_mempool_put_msg(msg);
            break;
        }

        sleep(1);
    }

    //pthread_t thread_id;
    //pthread_create(&thread_id, NULL, framework_test_task1, NULL);
    /*
        pthread_t thread[1024] = {};

        for (int i = 0; i < 3; ++i) {
            char namebuf[64] = {0};
            snprintf(namebuf, sizeof(namebuf), "st_s_a-%d", i);
            rte_ctrl_thread_create(&thread[i], namebuf, NULL, framework_test_task, NULL);
            //pthread_create(&thread[i], NULL, framework_test_task, NULL);
            //rte_thread_setname(thread[i], namebuf);
            //printf("id  = %x\n", thread[i]);
        }

        //ret = rte_eal_remote_launch(framework_test_task1, NULL, 61);

        while (1);
    */
    const char *name = "DMA-H2C";
    ret = ccdk_framework_register_server(name, DMA_H2C_SERVER_ID, FLAGS_server_thread, dma_h2c_cb, NULL,
                                         FLAGS_server_core);

    if (ret < 0) {
        printf("%s(), %d, ret = %d\n", __func__, __LINE__, ret);
        //return ret;
    }

    const char *name1 = "CCDK-TEST";
    ret = ccdk_framework_register_server(name1, CCDK_TEST_SERVER_ID, 1, ccdk_server_cb, NULL, 0);

    if (ret < 0) {
        printf("%s(), %d, ret = %d\n", __func__, __LINE__, ret);
        //return ret;
    }

    /*

        const char *name2 = "TASK1";
        ret = ccdk_framework_register_server(name2, TASK_SERVER_ID, 1, task_cb, NULL);

        if (ret < 0) {
            printf("%s(), %d\n", __func__, __LINE__);
            return ret;
        }

    const char *name3 = "Operation";
    ret = ccdk_framework_register_server(name3, OPERATION_SERVER_ID, 1, operation_cb, NULL, 0);

    if (ret < 0) {
        printf("%s(), %d, ret = %d\n", __func__, __LINE__, ret);
        //return ret;
    }
    */
    g_req_num = FLAGS_req_num;
    g_debug_level = FLAGS_debug;
    g_proc_num = FLAGS_proc_num;
    g_thread_num = FLAGS_thread_num;
    g_interval = FLAGS_interval;
    set_debug_level(FLAGS_debug);

    if (FLAGS_operation) {
        operation_session();
        goto end;
    }

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        pthread_t tid;
        unsigned ring_size = 4096;
        sleep(1);
        g_intr_ring = rte_ring_create("intr_ring", ring_size, SOCKET_ID_ANY, RING_F_SC_DEQ);
        pthread_create(&tid, NULL, dma_h2c_intr_async_task, NULL);
        //rte_ctrl_thread_create(&tid, "intr_async_task", NULL, dma_h2c_intr_async_task, NULL);
    }

    sleep(3);

    /*
        if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
            pthread_t slt_thread;
            pthread_create(&slt_thread, NULL, print_session_latency_task, NULL);
        }
    */

    if (FLAGS_role == "server") {
        printf("--------------as a server-----------------\n");

        while (1) {
            sleep(1);
        }
    } else {
        RUN_ALL_TESTS();
    }

end:
    printf("--------------exit-----------------\n");
    ccdk_framework_cleanup();
    return 0;
}