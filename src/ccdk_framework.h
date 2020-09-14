//
/// Copyright (c) 2018 Alibaba-inc.
///
/// All rights reserved.
/// @file    ccdk_framework.h
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2018-12-30

#ifndef _CCDK_FRAMEWORK__H_
#define _CCDK_FRAMEWORK__H_

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_atomic.h>
#include <rte_vfio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "ccdk_pmd.h"
#include "ccdk_framework_intf.h"
#include <rte_log.h>

#define FRAMEWORK_LOG(level, fmt, args...) {\
        if (unlikely(RTE_LOG_ ## level <= rte_log_get_global_level())) {\
            struct timeval tv;	\
            struct tm *ptm;	\
            char time_string[40];	\
            gettimeofday(&tv, NULL);	\
            ptm = localtime(&tv.tv_sec);	\
            strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);	\
            rte_log(RTE_LOG_ ## level, g_ccdk_accdev_logtype, "%s - framework[ %s ]: %s(), %d, [%d] " fmt "\n", time_string, #level,  __func__ , __LINE__ , g_thread_local.session_id, ##args); \
        }	\
    }while (0)

#define FRAMEWORK_DEBUG(fmt, args...) \
    FRAMEWORK_LOG(DEBUG, fmt, ## args)

#define FRAMEWORK_INFO(fmt, args...) \
    FRAMEWORK_LOG(INFO, fmt, ## args)
#define FRAMEWORK_WARN(fmt, args...) \
    FRAMEWORK_LOG(WARNING, fmt, ## args)
#define FRAMEWORK_ERR(fmt, args...) \
    FRAMEWORK_LOG(ERR, fmt, ## args)
#define FRAMEWORK_FATAL(fmt, args...) \
    FRAMEWORK_LOG(FATAL, fmt, ## args)

/// @brief	网络不可达
#define EC_CCDK_MP_SOCKET_ERR (EC_CCDK_GENERAL_BASE - 100)
/// @brief 不支持
#define EC_CCDK_NOT_SUPPORT (EC_CCDK_GENERAL_BASE - 101)
/// @brief session 不可用
#define EC_CCDK_SESSION_UNUSED	(EC_CCDK_GENERAL_BASE - 102)

#define CCDK_FRAMEWORK_VERSION "1.52"

//#define EVENTFD_MODE
#define INTR_ONLY_POLL_MODE


#define AVG(v1, v2)	((v2) > 0 ? (v1) / (v2) : 0)

enum ccdk_framework_session_mode {
    CCDK_SESSION_OPEN = 0x01,
    CCDK_SESSION_MP = 0x02,
    CCDK_SESSION_MAX = 0x03,
};

#define CCDK_REQ_MAGIC	0x94129394

struct ccdk_framework_session_latency_req_log {
    rte_atomic32_t session_id;
    rte_atomic32_t server_id;
    rte_atomic32_t req_id;
    rte_atomic64_t req_addr;
    rte_atomic64_t start;
    rte_atomic64_t action_before;
    rte_atomic64_t action_after;
    rte_atomic64_t intr_handle;
    rte_atomic64_t complete;
};

struct ccdk_framework_session_latency {
    rte_atomic64_t min_start_2_action_latency;
    rte_atomic64_t min_action_latency;
    rte_atomic64_t min_action_2_intr_latency;
    rte_atomic64_t min_intr_2_complete_latency;
    rte_atomic64_t min_start_2_complete_latency;

    rte_atomic64_t max_start_2_action_latency;
    rte_atomic64_t max_action_latency;
    rte_atomic64_t max_action_2_intr_latency;
    rte_atomic64_t max_intr_2_complete_latency;
    rte_atomic64_t max_start_2_complete_latency;


    rte_atomic64_t total_start_2_action_cnt;
    rte_atomic64_t total_start_2_action_latency;

    rte_atomic64_t total_action_cnt;
    rte_atomic64_t total_action_latency;

    rte_atomic64_t total_action_2_intr_cnt;
    rte_atomic64_t total_action_2_intr_latency;

    rte_atomic64_t total_intr_2_complete_cnt;
    rte_atomic64_t total_intr_2_complete_latency;

    rte_atomic64_t total_start_2_complete_cnt;
    rte_atomic64_t total_start_2_complete_latency;

#define CCDK_FRAMEWORK_REQ_ARRAY_SIZE	32
    struct ccdk_framework_session_latency_req_log req_log_array[CCDK_FRAMEWORK_REQ_ARRAY_SIZE];
    rte_atomic32_t req_log_idx;
};

struct ccdk_framework_req {
    rte_atomic32_t magic;
    uint32_t size;
    uint32_t session_id;
    uint32_t server_id;
    uint32_t req_id;
    uint32_t front_is_async;
    uint32_t behind_is_async;

    //ccdk_session_req_action_t action;
    //void *arg;

    /// @brief latency
    /// start          action before          action after          intr_handle          complete
    ///   |    eventfd       |        action        |       intr           |     eventfd     |
    ///
    ///    ---start_2_action---------action------------action_2_intr-------intr_2_complete--
    ///    -----------------------------start_2_complete------------------------------------------
    int64_t start_ts;
    int64_t start_2_action_latency;
    int64_t action_before_ts;
    int64_t action_latency;
    int64_t action_after_ts;
    int64_t action_2_intr_latency;
    int64_t intr_handle_ts;
    int64_t intr_2_complete_latency;
    int64_t complete_ts;
    int64_t start_2_complete_latency;

    TAILQ_ENTRY(ccdk_framework_req) next; /**< Pointer entries for a tailq list */

#define CCDK_REQ_UNDONE	100
    rte_atomic32_t result;

    /// @brief user msg
    uint8_t msg[0];
};


#define CCDK_MAX_INTR_VECTOR_NUM RTE_MAX_RXTX_INTR_VEC_ID

TAILQ_HEAD(ccdk_framework_req_list_head, ccdk_framework_req);

struct ccdk_framework_intr {
    struct ccdk_framework_req_list_head head;
    rte_spinlock_t lock;
    uint32_t vector;
    ccdk_intr_action_t action;
    void *arg;
};


#define CCDK_CFG_MP "eal_ccdk_mp_sync"
#define SOCKET_CCDK_REGISTER 0x100
#define SOCKET_CCDK_UNREGISTER 0x200
#define SOCKET_CCDK_HEARTBEAT 0x300
#define SOCKET_CCDK_REQ_CLIENT_EVENT_FD 0x400
#define SOCKET_CCDK_REQ_SERVER_EVENT_FD 0x500
#define SOCKET_CCDK_REQ_SESSION_EVENT_FD 0x600


struct ccdk_mp_param {
    int req;
    int result;
};

struct ccdk_mp_register_param {
    struct ccdk_mp_param param;
    uint32_t session_id;
    struct ccdk_framework_session_latency *latency;
    struct rte_mempool_cache *request_cache;
};

struct ccdk_mp_get_server_event_fd_param {
    struct ccdk_mp_param param;
    uint32_t server_id;
};

struct ccdk_mp_get_session_event_fd_param {
    struct ccdk_mp_param param;
    uint32_t session_id;
};

struct ccdk_framework_init_desc {
    /// @brief 0: binding master's core; >0: no binding or binding the other core id, depend on register server parameters
    uint8_t server_binding_policy;
    uint8_t response_binding_policy;
};

#define CCDK_MAX_MEMPOOL_NUM	8
struct ccdk_framework_mempool {
    int size;
    struct rte_mempool *pool;
};

enum {
    CCDK_UNUSED = 0,
    CCDK_USED = 1,
};

struct ccdk_framework_condition {
#define CCDK_SESSION_SIGNALLED 1000
    pthread_mutex_t cv_mutex;
    pthread_cond_t  cv;
    rte_atomic32_t cv_signalled;
};

#define CCDK_INVALID_SESSION_ID	0xffffffff
struct ccdk_framework_session {
    uint32_t session_id;
    rte_atomic32_t status;
    int is_local;
    rte_atomic32_t heartbeat;

    struct ccdk_framework_req_list_head head;
    uint32_t req_id;
    rte_spinlock_t lock;
#define CCDK_MEMPOOL_CACHE_SIZE	64
    struct rte_mempool_cache *request_cache;

    ccdk_session_req_action_t action;
    void *arg;

    /// @brief 0: sync event fd, 1: async event fd
#define CCDK_SESSION_MAX_EVENTFD	2
#define CCDK_SESSION_SYNC_EVENTFD	0
#define CCDK_SESSION_ASYNC_EVENTFD	1
    int event_fd[CCDK_SESSION_MAX_EVENTFD];
    struct rte_epoll_event event[CCDK_SESSION_MAX_EVENTFD];
    int epfd;

    /// @brief pthread_cond mode
    struct ccdk_framework_condition cond;

    /// @brief latency
    struct ccdk_framework_session_latency *latency;
};


#define CCDK_MAX_SERVER_NUM	64
#define CCDK_MAX_SERVER_THREAD_NUM	32

struct ccdk_framework_server {
    const char *name;
    uint32_t server_id;
    int status;

    /// @brief eventfd mode
    int event_fd;

    /// @brief pthread_cond mode
    struct ccdk_framework_condition cond;

    /// @brief for request
    //struct rte_ring *high_ring;
    struct rte_ring *ring;

    /// @brief primary only
    pthread_t thread[CCDK_MAX_SERVER_THREAD_NUM];
    int thread_running;
    int epfd;
    ccdk_server_action_t action;
    void *arg;
    struct ccdk_framework_req_list_head head;
    rte_spinlock_t lock;
    /// @brief server action 是否可重入是业务要考虑的事情，这个不建议对每个server action调用加锁
    pthread_mutex_t mutex;
};

struct ccdk_framework {
    struct ccdk_framework_init_desc desc;
    struct ccdk_framework_mempool mem_pool[CCDK_MAX_MEMPOOL_NUM];

    /// @brief primary only
    struct rte_intr_handle *intr_handle;
    pthread_t thread;
    int thread_running;

    int intr_epfd;
    pthread_t intr_thread;
    int intr_thread_running;

    struct ccdk_framework_intr intr[CCDK_MAX_INTR_VECTOR_NUM];

    struct ccdk_framework_server server[CCDK_MAX_SERVER_NUM];

    struct ccdk_framework_session session[CCDK_MAX_SESSION_NUM];
    pthread_mutex_t mp_mutex;
    pthread_mutex_t mempool_mutex;

    int session_epfd;
    struct ccdk_framework_condition cond;
    pthread_t resp_thread;
    int resp_thread_running;

    rte_atomic32_t already_init;
};


struct ccdk_framework_thread_local {
    uint32_t session_id;
};



#define CCDK_MGNT_CMD_HEARTBEAT		0x10000001

struct ccdk_framework_mgnt_heartbeat_msg {
    uint32_t session_id[CCDK_MAX_SESSION_NUM];
};


#endif