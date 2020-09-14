//
/// Copyright (c) 2019 Alibaba-inc.
///
/// All rights reserved.
/// @file    framework_st.cpp
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2019-2-25

#include "framework.h"


static void *framework_st_s_s_task(void *arg)
{
    framework_s_x(0);
}

static void *framework_st_s_a_task(void *arg)
{
    framework_s_x(1);
}

void framework_st_s_x(int is_sa)
{
    //struct timeval start, end;
    //unsigned long long time_cost;
    //srand(start.tv_sec);
    //gettimeofday(&start, NULL);
    pthread_t thread[1024] = {};
    int64_t start_ts = rte_rdtsc_precise();

    for (int i = 0; i < g_thread_num; ++i) {
        char namebuf[64] = {0};

        if (is_sa) {
            snprintf(namebuf, sizeof(namebuf), "st_s_a-%d", i);
            //rte_ctrl_thread_create(&thread[i], namebuf, NULL, framework_st_s_a_task, NULL);
            pthread_create(&thread[i], NULL, framework_st_s_a_task, NULL);
        } else {
            snprintf(namebuf, sizeof(namebuf), "st_s_s-%d", i);
            //rte_ctrl_thread_create(&thread[i], namebuf, NULL, framework_st_s_s_task, NULL);
            pthread_create(&thread[i], NULL, framework_st_s_s_task, NULL);
        }

        //pthread_create(&thread[i], NULL, thread_multi_task, arg);
    }

    for (int j = 0; j < g_thread_num; j++) {
        if (thread[j] > 0) {
            pthread_join(thread[j], NULL);
        }
    }

    int64_t end_ts  = rte_rdtsc_precise();
    int64_t req_latency = end_ts - start_ts;
    double req_latency_ms = (double)(req_latency * 1000) / rte_get_tsc_hz();
    double qps = double(g_thread_num * g_req_num * 1000) / req_latency_ms;
    //gettimeofday(&end, NULL);
    //time_cost = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
    //double tc = time_cost;
    //double qps = double(g_thread_num * g_req_num * 1000) / tc;
    printf("over, thread_num = %d, req_num = %d, time_cost = %f ms, qps = %f\n", g_thread_num, g_req_num,
           req_latency_ms, qps);
}

static void *framework_st_a_s_task(void *arg)
{
    framework_a_x(0);
}

static void *framework_st_a_a_task(void *arg)
{
    framework_a_x(1);
}

void framework_st_a_x(int is_aa)
{
    pthread_t thread[1024] = {};
    int64_t start_ts = rte_rdtsc_precise();

    for (int i = 0; i < g_thread_num; ++i) {
        char namebuf[64] = {0};

        if (is_aa) {
            snprintf(namebuf, sizeof(namebuf), "st_a_a-%d", i);
            //rte_ctrl_thread_create(&thread[i], namebuf, NULL, framework_st_a_a_task, NULL);
            pthread_create(&thread[i], NULL, framework_st_a_a_task, NULL);
        } else {
            snprintf(namebuf, sizeof(namebuf), "st_a_s-%d", i);
            //rte_ctrl_thread_create(&thread[i], namebuf, NULL, framework_st_a_s_task, NULL);
            pthread_create(&thread[i], NULL, framework_st_a_s_task, NULL);
        }
    }

    for (int j = 0; j < g_thread_num; j++) {
        if (thread[j] > 0) {
            pthread_join(thread[j], NULL);
        }
    }

    int64_t end_ts  = rte_rdtsc_precise();
    int64_t req_latency = end_ts - start_ts;
    double req_latency_ms = (double)(req_latency * 1000) / rte_get_tsc_hz();
    double qps = double(g_thread_num * g_req_num * 1000) / req_latency_ms;
    printf("over, thread_num = %d, req_num = %d, time_cost = %f ms, qps = %f\n", g_thread_num, g_req_num,
           req_latency_ms, qps);
}