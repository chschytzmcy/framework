//
/// Copyright (c) 2018 Alibaba-inc.
///
/// All rights reserved.
/// @file    ccdk_framework.cpp
/// @brief
/// @author  chenyan
/// @version 1.0
/// @date    2018-12-30

/// v1.1
/// 增加primary 直通处理request
/// 支持mutex和ring+eventfd
/// mutex(250w)性能远大于ring+eventfd(16w)(注：线程级evnetfd触发相对于进程级触发eventfd，相差不多，稍微高效一点点)

/// v1.2
/// client 增加session_id和thread_id绑定

/// v1.3
/// 实现同一个session同时支持同步和异步
/// primary 独占的同步只支持mutex、异步只支持ring+eventfd

/// v1.4
/// 增加magt server和session

/// v1.5
/// 增加req和session latency

/// v1.51
/// 错误码修正

/// v1.52
/// 优化

/// v1.6
/// 优化使用thread 局部存储实现 和 session 的绑定关系

#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

#include "ccdk_framework.h"

struct ccdk_framework g_framework = {};
static __thread struct ccdk_framework_thread_local g_thread_local = {
    .session_id = CCDK_INVALID_SESSION_ID,
};

#define CV

#ifdef CV
std::atomic<bool> server_dataReady{false};
std::atomic<bool> session_dataReady{false};
#endif


static void
ccdk_framework_cale_start_2_action_latency(int64_t action_before_ts, struct ccdk_framework_req *req,
        struct ccdk_framework_session_latency *latency)
{
    if (!action_before_ts) {
        req->action_before_ts = rte_rdtsc_precise();
    } else {
        req->action_before_ts = action_before_ts;
    }

    FRAMEWORK_DEBUG("session_id = %d, server_id = 0x%x\n", req->session_id, req->server_id);
    req->start_2_action_latency = req->action_before_ts - req->start_ts;

    if (req->start_2_action_latency > 0) {
        if (req->start_2_action_latency < rte_atomic64_read(&latency->min_start_2_action_latency)
            || rte_atomic64_read(&latency->min_start_2_action_latency) == 0) {
            rte_atomic64_set(&latency->min_start_2_action_latency,  req->start_2_action_latency);
        }

        if (req->start_2_action_latency > rte_atomic64_read(&latency->max_start_2_action_latency)) {
            rte_atomic64_set(&latency->max_start_2_action_latency, req->start_2_action_latency);
        }

        rte_atomic64_inc(&latency->total_start_2_action_cnt);
        rte_atomic64_add(&latency->total_start_2_action_latency, req->start_2_action_latency);
    }

    FRAMEWORK_DEBUG("start_2_action latency :: start_ts = 0x%lu, action_before_ts = 0x%lu, start_2_action_latency = 0x%lu, latency->total_start_2_action_cnt = %d, latency->total_start_2_action_latency = 0x%lu\n",
                    req->start_ts, req->action_before_ts, req->start_2_action_latency,
                    rte_atomic64_read(&latency->total_start_2_action_cnt),
                    rte_atomic64_read(&latency->total_start_2_action_latency));
}


static void
ccdk_framework_cale_action_latency(int64_t action_after_ts, struct ccdk_framework_req *req,
                                   struct ccdk_framework_session_latency *latency)
{
    if (!action_after_ts) {
        req->action_after_ts = rte_rdtsc_precise();
    } else {
        req->action_after_ts = action_after_ts;
    }

    FRAMEWORK_DEBUG("session_id = %d, server_id = 0x%x\n", req->session_id, req->server_id);
    req->action_latency = req->action_after_ts - req->action_before_ts;

    if (req->action_latency > 0) {
        if (req->action_latency < rte_atomic64_read(&latency->min_action_latency)
            || rte_atomic64_read(&latency->min_action_latency) == 0) {
            rte_atomic64_set(&latency->min_action_latency, req->action_latency);
        }

        if (req->action_latency > rte_atomic64_read(&latency->max_action_latency)) {
            rte_atomic64_set(&latency->max_action_latency, req->action_latency);
        }

        rte_atomic64_inc(&latency->total_action_cnt);
        rte_atomic64_add(&latency->total_action_latency, req->action_latency);
    }

    FRAMEWORK_DEBUG("action latency :: action_before_ts = 0x%lu, action_after_ts = 0x%lu, action_latency = 0x%lu, latency->total_action_cnt = %d, latency->total_action_latency = 0x%lu\n",
                    req->action_before_ts, req->action_after_ts, req->action_latency,
                    rte_atomic64_read(&latency->total_action_cnt),
                    rte_atomic64_read(&latency->total_action_latency));
}

static void
ccdk_framework_cale_action_2_intr_latency(int64_t intr_handle_ts, struct ccdk_framework_req *req,
        struct ccdk_framework_session_latency *latency)
{
    if (!intr_handle_ts) {
        req->intr_handle_ts = rte_rdtsc_precise();
    } else {
        req->intr_handle_ts = intr_handle_ts;
    }

    FRAMEWORK_DEBUG("session_id = %d, server_id = 0x%x\n", req->session_id, req->server_id);
    req->action_2_intr_latency = req->intr_handle_ts - req->action_after_ts;

    if (req->action_2_intr_latency > 0) {
        if (req->action_2_intr_latency < rte_atomic64_read(&latency->min_action_2_intr_latency)
            || rte_atomic64_read(&latency->min_action_2_intr_latency) == 0) {
            rte_atomic64_set(&latency->min_action_2_intr_latency, req->action_2_intr_latency);
        }

        if (req->action_2_intr_latency > rte_atomic64_read(&latency->max_action_2_intr_latency)) {
            rte_atomic64_set(&latency->max_action_2_intr_latency, req->action_2_intr_latency);
        }

        rte_atomic64_inc(&latency->total_action_2_intr_cnt);
        rte_atomic64_add(&latency->total_action_2_intr_latency, req->action_2_intr_latency);
    }

    FRAMEWORK_DEBUG("action_2_intr latency :: action_after_ts = 0x%lu, intr_handle_ts = 0x%lu, action_2_intr_latency = 0x%lu, latency->total_action_2_intr_cnt = %d, latency->total_action_2_intr_latency = 0x%lu\n",
                    req->action_after_ts, req->intr_handle_ts, req->action_2_intr_latency,
                    rte_atomic64_read(&latency->total_action_2_intr_cnt),
                    rte_atomic64_read(&latency->total_action_2_intr_latency));
}


static void
ccdk_framework_cale_intr_2_complete_latency(int64_t complete_ts, struct ccdk_framework_req *req,
        struct ccdk_framework_session_latency *latency)
{
    if (unlikely(!latency)) {
        return;
    }

    FRAMEWORK_DEBUG("session_id = %d, server_id = 0x%x\n", req->session_id, req->server_id);

    if (!complete_ts) {
        req->complete_ts = rte_rdtsc_precise();
    } else {
        req->complete_ts = complete_ts;
    }

    req->intr_2_complete_latency = req->complete_ts - req->intr_handle_ts;

    if (req->intr_2_complete_latency > 0) {
        if (req->intr_2_complete_latency < rte_atomic64_read(&latency->min_intr_2_complete_latency)
            || rte_atomic64_read(&latency->min_intr_2_complete_latency) == 0) {
            rte_atomic64_set(&latency->min_intr_2_complete_latency, req->intr_2_complete_latency);
        }

        if (req->intr_2_complete_latency > rte_atomic64_read(&latency->max_intr_2_complete_latency)) {
            rte_atomic64_set(&latency->max_intr_2_complete_latency, req->intr_2_complete_latency);
        }

        rte_atomic64_inc(&latency->total_intr_2_complete_cnt);
        rte_atomic64_add(&latency->total_intr_2_complete_latency, req->intr_2_complete_latency);
    }

    FRAMEWORK_DEBUG("intr_2_complete latency :: intr_handle_ts = 0x%lu, complete_ts = 0x%lu, intr_2_complete_latency = 0x%lu, latency->total_intr_2_complete_cnt = %d, latency->total_intr_2_complete_latency = 0x%lu, latency->total_start_2_complete_cnt = %d, latency->total_start_2_complete_latency = 0x%lu\n",
                    req->intr_handle_ts, req->complete_ts, req->intr_2_complete_latency,
                    rte_atomic64_read(&latency->total_intr_2_complete_cnt),
                    rte_atomic64_read(&latency->total_intr_2_complete_latency));
    req->start_2_complete_latency = req->complete_ts - req->start_ts;

    if (req->start_2_complete_latency > 0) {
        if (req->start_2_complete_latency < rte_atomic64_read(&latency->min_start_2_complete_latency)
            || rte_atomic64_read(&latency->min_start_2_complete_latency) == 0) {
            rte_atomic64_set(&latency->min_start_2_complete_latency, req->start_2_complete_latency);
        }

        if (req->start_2_complete_latency > rte_atomic64_read(&latency->max_start_2_complete_latency)) {
            rte_atomic64_set(&latency->max_start_2_complete_latency, req->start_2_complete_latency);
        }

        rte_atomic64_inc(&latency->total_start_2_complete_cnt);
        rte_atomic64_add(&latency->total_start_2_complete_latency, req->start_2_complete_latency);
    }

    FRAMEWORK_DEBUG("start_2_complete latency :: start_ts = 0x%lu, complete_ts = 0x%lu, start_2_complete_latency = %ld, latency->total_start_2_complete_cnt = %d, latency->total_start_2_complete_latency = %ld\n",
                    req->start_ts, req->complete_ts, req->start_2_complete_latency,
                    rte_atomic64_read(&latency->total_start_2_complete_cnt),
                    rte_atomic64_read(&latency->total_start_2_complete_latency));
}


int
ccdk_framework_intr_action_register(int vector, ccdk_intr_action_t action, void *arg)
{
    struct ccdk_framework_intr *intr;

    if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
        return EC_CCDK_NOT_SUPPORT;
    }

    if (vector < 0 || vector >= CCDK_MAX_INTR_VECTOR_NUM) {
        return EC_CCDK_OUT_OF_RANGE;
    }

    intr = &g_framework.intr[vector];
    intr->vector = (uint32_t)vector;
    intr->action = action;
    intr->arg = arg;
    TAILQ_INIT(&intr->head);
    rte_spinlock_init(&intr->lock);
    return EC_CCDK_OK;
}


/// @brief ccdk_framework_intr_thread_main
/// 采用intr+poll的方式，接收到中断后
/// @param arg
static void *
ccdk_framework_intr_thread_main(void *arg)
{
    struct rte_epoll_event revent[CCDK_MAX_INTR_VECTOR_NUM] = {};

    while (g_framework.intr_thread_running) {
#ifdef INTR_ONLY_POLL_MODE

        /// @brief 独占一个CPU core，实时调用intr action，遍历intr寄存器
        for (int i = 0; i < CCDK_MAX_INTR_VECTOR_NUM; i++) {
            struct ccdk_framework_intr *intr = (struct ccdk_framework_intr *)revent[i].epdata.data;

            if (intr->action) {
                /// @brief call user's intr cb
                (void)intr->action(intr->arg);
            }
        }

#else
        int poll_cnt = 100;
        n = rte_epoll_wait(RTE_EPOLL_PER_THREAD, revent, CCDK_MAX_INTR_VECTOR_NUM, 3000);

        if (unlikely(n < 0)) {
            FRAMEWORK_ERR("rte_epoll_wait returned error %d", n);
            return NULL;
        }

        /// @brief intr+poll方案，支持intr触发后，poll 持续100次
        for (int cnt = 0; cnt < poll_cnt; cnt++) {
            for (int i = 0; i < n; i++) {
                struct ccdk_framework_intr *intr = (struct ccdk_framework_intr *)revent[i].epdata.data;

                if (intr->action) {
                    /// @brief call user's intr cb
                    (void)intr->action(intr->arg);
                }
            }

            /// @brief 后续全中断向量poll检测
            n = CCDK_MAX_INTR_VECTOR_NUM;
        }

#endif
    }

    return NULL;
}


/* irq set buffer length for queue interrupts and LSC interrupt */
#define MSIX_IRQ_SET_BUF_LEN (sizeof(struct vfio_irq_set) + \
                              sizeof(int) * (CCDK_MAX_INTR_VECTOR_NUM + 1))

/* enable MSI-X interrupts */
static int
ccdk_framework_enable_vfio_msix(const struct rte_intr_handle *intr_handle)
{
    int len, ret;
    char irq_set_buf[MSIX_IRQ_SET_BUF_LEN];
    struct vfio_irq_set *irq_set;
    int *fd_ptr;
    len = sizeof(irq_set_buf);
    irq_set = (struct vfio_irq_set *) irq_set_buf;
    irq_set->argsz = len;
    irq_set->count = CCDK_MAX_INTR_VECTOR_NUM;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq_set->start = 0;
    fd_ptr = (int *) &irq_set->data;
    rte_memcpy(&fd_ptr[RTE_INTR_VEC_ZERO_OFFSET], intr_handle->efds,
               sizeof(*intr_handle->efds) * intr_handle->nb_efd);
    ret = ioctl(intr_handle->vfio_dev_fd, VFIO_DEVICE_SET_IRQS, irq_set);

    if (ret) {
        FRAMEWORK_ERR("Error enabling MSI-X interrupts for fd %d\n",
                      intr_handle->fd);
        return EC_CCDK_RETRY;
    }

    return EC_CCDK_OK;
}

/* disable MSI-X interrupts */
static int
ccdk_framework_disable_vfio_msix(const struct rte_intr_handle *intr_handle)
{
    struct vfio_irq_set *irq_set;
    char irq_set_buf[MSIX_IRQ_SET_BUF_LEN];
    int len, ret;
    len = sizeof(struct vfio_irq_set);
    irq_set = (struct vfio_irq_set *) irq_set_buf;
    irq_set->argsz = len;
    irq_set->count = 0;
    irq_set->flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq_set->start = 0;
    ret = ioctl(intr_handle->vfio_dev_fd, VFIO_DEVICE_SET_IRQS, irq_set);

    if (ret) {
        FRAMEWORK_ERR("Error disabling MSI-X interrupts for fd %d\n",  intr_handle->fd);
        return EC_CCDK_RETRY;
    }

    return EC_CCDK_OK;
}


static void
ccdk_framework_intr_cleanup(void)
{
    int ret = ccdk_framework_disable_vfio_msix(g_framework.intr_handle);

    if (ret < 0) {
        FRAMEWORK_ERR("ccdk_framework_disable_vfio_msix fail, ret = %d\n", ret);
    }

    g_framework.intr_thread_running = 0;

    if (g_framework.intr_thread > 0) {
        pthread_join(g_framework.intr_thread, NULL);
    }

    rte_intr_efd_disable(g_framework.intr_handle);

    if (g_framework.intr_epfd > 0) {
        close(g_framework.intr_epfd);
        g_framework.intr_epfd = 0;
    }
}


int
ccdk_framework_intr_init(struct rte_intr_handle *intr_handle)
{
    int ret = 0;
    int vec = 0;
    int pfd = 0;

    if (!intr_handle) {
        return EC_CCDK_NULL_PTR;
    }

    /// @brief create eventfd
    ret = rte_intr_efd_enable(intr_handle, CCDK_MAX_INTR_VECTOR_NUM);

    if (ret < 0) {
        FRAMEWORK_ERR("rte_intr_efd_enable failed, ret = %d\n", ret);
        return EC_CCDK_INIT_FAIL;
    }

    /// @brief add eventfd to epfd
    if (g_framework.intr_epfd <= 0) {
        pfd = epoll_create(255);

        if (pfd < 0) {
            FRAMEWORK_ERR("Cannot create epoll instance, error %i (%s)\n", errno, strerror(errno));
            goto end;
        }

        g_framework.intr_epfd = pfd;
    }

    for (vec = 0; vec < RTE_MAX_RXTX_INTR_VEC_ID; vec++) {
        struct ccdk_framework_intr *intr = &g_framework.intr[vec];
        TAILQ_INIT(&intr->head);
        rte_spinlock_init(&intr->lock);
        ret = rte_intr_rx_ctl(intr_handle, g_framework.intr_epfd, RTE_INTR_EVENT_ADD, vec + 1, intr);

        if (ret < 0) {
            FRAMEWORK_ERR("failed to add event, ret = %d\n", ret);
            goto end;
        }
    }

    /// @brief create thread for epoll wait
    g_framework.intr_thread_running = 1;
    ret = rte_ctrl_thread_create(&g_framework.intr_thread, "ccdk-intr-thread", NULL,
                                 ccdk_framework_intr_thread_main, NULL);

    if (ret < 0) {
        FRAMEWORK_ERR("failed to create thread, ret = %d\n", ret);
        goto end;
    }

    //4 enable msix interrupt
    // com_event_fd + 24 vector event fd   跟  msix 的0 -24 号中断向量是一一对应；
    //rte_intr_enable(intr_handle);
    ret = ccdk_framework_enable_vfio_msix(intr_handle);

    if (ret < 0) {
        FRAMEWORK_ERR("failed to create thread, ret = %d\n", ret);
        goto end;
    }

    g_framework.intr_handle = intr_handle;
    return EC_CCDK_OK;
end:
    ccdk_framework_intr_cleanup();
    return EC_CCDK_INIT_FAIL;
}


static inline int
ccdk_framework_eventfd_signal(int event_fd)
{
    uint64_t data = 1;
    int count = write(event_fd, &data, sizeof(uint64_t));

    if (count > 0) {
        return EC_CCDK_OK;
    }

    FRAMEWORK_ERR("count = %d, error %i (%s)\n", count, errno, strerror(errno));
    return EC_CCDK_RETRY;
}
static inline int
ccdk_framework_condition_signal(struct ccdk_framework_server *server)
{
	std::unique_lock<std::mutex> lk(server->mtx);
	server_dataReady = true;
    server->cv_data.notify_one();
	FRAMEWORK_DEBUG("\n");
    return EC_CCDK_OK;
}

static void
ccdk_framework_epoll_read_cb(int event_fd, void *arg)
{
    char buf[128] = {};
    int bytes_read = 128;
    int nbytes;

    do {
        nbytes = read(event_fd, &buf, bytes_read);
        FRAMEWORK_DEBUG("event_fd = %d, nbytes = %d\n", event_fd, nbytes);

        if (nbytes < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK ||
                errno == EAGAIN) {
                continue;
            }

            FRAMEWORK_ERR("Error reading from fd %d: %s\n", event_fd, strerror(errno));
        } else if (nbytes == 0) {
            FRAMEWORK_ERR("Read nothing from fd %d\n", event_fd);
        }

        return;
    } while (1);
}


static inline int
ccdk_framework_get_server_epfd(void)
{
    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        int pfd = epoll_create(255);

        if (pfd < 0) {
            FRAMEWORK_ERR("Cannot create epoll instance, error %i (%s)\n", errno, strerror(errno));
            return EC_CCDK_CREATE_FAIL;
        }

        return pfd;
    }

    /*
     * if we're in a secondary process, do nothing
     */
    return EC_CCDK_OK;
}


static inline int
ccdk_framework_get_server_event_fd(const uint32_t server_id)
{
    int event_fd;
    struct rte_mp_msg mp_req, *mp_rep;
    struct rte_mp_reply mp_reply;
    struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
    struct ccdk_mp_get_server_event_fd_param *p = (struct ccdk_mp_get_server_event_fd_param *)mp_req.param;

    /* if we're in a primary process, try to open the event fd */
    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        if (event_fd < 0) {
            FRAMEWORK_ERR("can't setup eventfd, error %i (%s)\n", errno, strerror(errno));
            return EC_CCDK_CREATE_FAIL;
        }

        return event_fd;
    }

    /*
     * if we're in a secondary process, request thread event fd from the
     * primary process via mp channel
     */
    p->param.req = SOCKET_CCDK_REQ_SERVER_EVENT_FD;
    p->server_id = server_id;
    strcpy(mp_req.name, CCDK_CFG_MP);
    mp_req.len_param = sizeof(*p);
    mp_req.num_fds = 0;
    event_fd = -1;

    if (rte_mp_request_sync(&mp_req, &mp_reply, &ts) == 0 &&
        mp_reply.nb_received == 1) {
        mp_rep = &mp_reply.msgs[0];
        p = (struct ccdk_mp_get_server_event_fd_param *)mp_rep->param;

        if (p->param.result == 0 && mp_rep->num_fds == 1) {
            event_fd = mp_rep->fds[0];
            FRAMEWORK_DEBUG("server_id = 0x%x, event_fd = %d\n", server_id, event_fd);
            free(mp_reply.msgs);
            return event_fd;
        } else {
            FRAMEWORK_ERR("result = %d, num_fds = %d\n", p->param.result, mp_rep->num_fds);
        }

        free(mp_reply.msgs);
    }

    FRAMEWORK_ERR("cannot request server event fd\n");
    return EC_CCDK_MP_SOCKET_ERR;
}

static struct rte_ring *
ccdk_framework_get_server_ring(const char *name)
{
    struct rte_ring *ring = NULL;
    const unsigned ring_size = 4096;

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        /// @brief 不能是RING_F_SP_ENQ，因为可多session对一个server，建议填RING_F_SC_DEQ，强制为"single-consumer"，能提升效率
        ring = rte_ring_create(name, ring_size, /*rte_socket_id()*/SOCKET_ID_ANY, /*RING_F_SC_DEQ*/0);
    } else {
        ring = rte_ring_lookup(name);
    }

    return ring;
}


static inline int
ccdk_framework_get_session_event_fd(const uint32_t session_id)
{
    int event_fd;
    struct rte_mp_msg mp_req, *mp_rep;
    struct rte_mp_reply mp_reply;
    struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
    struct ccdk_mp_get_session_event_fd_param *p = (struct ccdk_mp_get_session_event_fd_param *)mp_req.param;
    struct ccdk_framework_session *session = &g_framework.session[session_id];

    /* if we're in a primary process, try to open the event fd */
    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        for (int i = 0; i < CCDK_SESSION_MAX_EVENTFD; i++) {
            if (session->event_fd[i] <= 0) {
                event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

                if (event_fd < 0) {
                    FRAMEWORK_ERR("can't setup sync_event_fd, error %i (%s)\n", errno, strerror(errno));
                    return EC_CCDK_CREATE_FAIL;
                }

                session->event_fd[i] = event_fd;
            }
        }

        return EC_CCDK_OK;
    }

    /*
     * if we're in a secondary process, request thread event fd from the
     * primary process via mp channel
     */
    p->param.req = SOCKET_CCDK_REQ_SESSION_EVENT_FD;
    p->session_id = session_id;
    strcpy(mp_req.name, CCDK_CFG_MP);
    mp_req.len_param = sizeof(*p);
    mp_req.num_fds = 0;

    if (rte_mp_request_sync(&mp_req, &mp_reply, &ts) == 0 &&
        mp_reply.nb_received == 1) {
        mp_rep = &mp_reply.msgs[0];
        p = (struct ccdk_mp_get_session_event_fd_param *)mp_rep->param;

        if (p->param.result == 0 && mp_rep->num_fds == 2) {
            session->event_fd[CCDK_SESSION_SYNC_EVENTFD] = mp_rep->fds[CCDK_SESSION_SYNC_EVENTFD];
            session->event_fd[CCDK_SESSION_ASYNC_EVENTFD] = mp_rep->fds[CCDK_SESSION_ASYNC_EVENTFD];
            free(mp_reply.msgs);
            FRAMEWORK_DEBUG("session_id = %d, sync_event_fd = %d, async_event_fd = %d\n",  session_id,
                            session->event_fd[CCDK_SESSION_SYNC_EVENTFD],
                            session->event_fd[CCDK_SESSION_ASYNC_EVENTFD]);
            return EC_CCDK_OK;
        } else {
            FRAMEWORK_ERR("result = %d, mp_rep->num_fds = %d\n", p->param.result, mp_rep->num_fds);
        }

        free(mp_reply.msgs);
    }

    FRAMEWORK_ERR("cannot request session event fd\n");
    return EC_CCDK_MP_SOCKET_ERR;
}


static inline int
ccdk_framework_get_session_epfd(int session_id)
{
    int epfd = 0;
    struct ccdk_framework_session *session = &g_framework.session[session_id];

    if (session->epfd <= 0) {
        epfd = epoll_create(255);

        if (epfd < 0) {
            FRAMEWORK_ERR("Cannot create epoll instance, error %i (%s)\n", errno, strerror(errno));
            return EC_CCDK_CREATE_FAIL;
        }

        session->epfd = epfd;
    }

    return EC_CCDK_OK;
}


static int
ccdk_framework_attach_session_event_fd_to_epoll(int epfd, int event_fd, struct rte_epoll_event *event, int op,
        void *data)
{
    int ret = 0;
    struct rte_epoll_data *epdata;
    epdata = &event->epdata;
    epdata->event  = EPOLLIN | EPOLLPRI | EPOLLET;// | EPOLLONESHOT;
    epdata->data   = data;
    epdata->cb_fun = (rte_intr_event_cb_t)ccdk_framework_epoll_read_cb;
    epdata->cb_arg = NULL;
    ret = rte_epoll_ctl(epfd, op, event_fd, event);

    if (unlikely(ret < 0)) {
        FRAMEWORK_ERR("rte_epoll_ctl failed, op = %d, error %i (%s)\n", op, errno, strerror(errno));
        return EC_CCDK_RETRY;
    }

    FRAMEWORK_INFO("event(%d) attach epfd(%d) success\n", event_fd, epfd);
    return EC_CCDK_OK;
}


static int
ccdk_framework_find_session_id(uint32_t *session_id)
{
    if (g_thread_local.session_id < CCDK_MAX_SESSION_NUM) {
        struct ccdk_framework_session *session = &g_framework.session[g_thread_local.session_id];

        if (rte_atomic32_read(&session->status) == CCDK_USED) {
            FRAMEWORK_DEBUG("find it, session_id = %d\n", g_thread_local.session_id);
            *session_id = g_thread_local.session_id;
            return EC_CCDK_OK;
        }

        g_thread_local.session_id = CCDK_INVALID_SESSION_ID;
    }

    FRAMEWORK_INFO("don't find it, session_id = %d\n", g_thread_local.session_id);
    return EC_CCDK_NOT_EXIST;
}


/// @brief ccdk_framework_register
///
/// @returns
static int
ccdk_framework_register(enum ccdk_framework_session_mode mode, uint32_t *session_id)
{
    struct rte_mp_msg mp_req, *mp_rep;
    struct rte_mp_reply mp_reply;
    struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
    struct ccdk_framework_session *session = NULL;
    struct ccdk_mp_register_param *p = (struct ccdk_mp_register_param *)mp_req.param;

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
            session = &g_framework.session[i];

            if (rte_atomic32_read(&session->status) == CCDK_UNUSED) {
                if (!session->request_cache) {
                    struct rte_mempool_cache *request_cache;
                    request_cache = rte_mempool_cache_create(CCDK_MEMPOOL_CACHE_SIZE, SOCKET_ID_ANY);

                    if (!request_cache) {
                        return EC_CCDK_OUT_OF_MEM;
                    }

                    session->request_cache = request_cache;
                }

                if (!session->latency) {
                    struct ccdk_framework_session_latency *latency = NULL;
                    latency = (struct ccdk_framework_session_latency *)rte_zmalloc("ccdk-framework",
                              sizeof(struct ccdk_framework_session_latency), 64);

                    if (!latency) {
                        return EC_CCDK_OUT_OF_MEM;
                    }

                    session->latency = latency;
                }

                /// @brief 针对高频率调用的memset/memcpy，可采用rte_memcpy代替优化
                memset(session->latency, 0, sizeof(struct ccdk_framework_session_latency));
                rte_atomic32_set(&session->status, CCDK_USED);
                *session_id = i;
                return EC_CCDK_OK;
            }
        }

        return EC_CCDK_FULL;
    }

    p->param.req = SOCKET_CCDK_REGISTER;
    strcpy(mp_req.name, CCDK_CFG_MP);
    mp_req.len_param = sizeof(*p);
    mp_req.num_fds = 0;

    if (rte_mp_request_sync(&mp_req, &mp_reply, &ts) == 0 &&
        mp_reply.nb_received == 1) {
        mp_rep = &mp_reply.msgs[0];
        p = (struct ccdk_mp_register_param *)mp_rep->param;

        if (p->param.result == 0) {
            FRAMEWORK_DEBUG("session_id = %d\n", p->session_id);
            free(mp_reply.msgs);

            if (p->session_id >= CCDK_MAX_SESSION_NUM) {
                return EC_CCDK_OUT_OF_RANGE;
            }

            session = &g_framework.session[p->session_id];
            rte_atomic32_set(&session->status, CCDK_USED);
            session->latency = p->latency;
            session->request_cache = p->request_cache;
            *session_id = p->session_id;
            return EC_CCDK_OK;
        } else {
            FRAMEWORK_ERR("result = %d\n",  p->param.result);
        }

        free(mp_reply.msgs);
    }

    FRAMEWORK_ERR("register fail\n");
    return EC_CCDK_MP_SOCKET_ERR;
}


/// @brief ccdk_framework_unregister
///
/// @returns
static int
ccdk_framework_unregister(uint32_t session_id)
{
    struct rte_mp_msg mp_req, *mp_rep;
    struct rte_mp_reply mp_reply;
    struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
    struct ccdk_mp_register_param *p = (struct ccdk_mp_register_param *)mp_req.param;
    struct ccdk_framework_session *session = &g_framework.session[session_id];

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        rte_atomic32_set(&session->status, CCDK_UNUSED);

        if (session->request_cache) {
            //rte_mempool_cache_flush(session->request_cache, mp);
            rte_mempool_cache_free(session->request_cache);
            session->request_cache = NULL;
        }

        if (session->latency) {
            rte_free(session->latency);
            session->latency = NULL;
        }

        return EC_CCDK_OK;
    }

    p->param.req = SOCKET_CCDK_UNREGISTER;
    strcpy(mp_req.name, CCDK_CFG_MP);
    mp_req.len_param = sizeof(*p);
    mp_req.num_fds = 0;
    p->session_id = session_id;

    if (rte_mp_request_sync(&mp_req, &mp_reply, &ts) == 0 &&
        mp_reply.nb_received == 1) {
        mp_rep = &mp_reply.msgs[0];
        p = (struct ccdk_mp_register_param *)mp_rep->param;

        if (p->param.result == 0) {
            free(mp_reply.msgs);
            rte_atomic32_set(&session->status, CCDK_UNUSED);
            session->latency = NULL;
            session->request_cache = NULL;
            return EC_CCDK_OK;
        } else {
            FRAMEWORK_ERR("result = %d\n",  p->param.result);
        }

        free(mp_reply.msgs);
    }

    FRAMEWORK_ERR("cannot unregister, session_id = %d\n", session_id);
    return EC_CCDK_MP_SOCKET_ERR;
}


static int
ccdk_framework_free_session(uint32_t session_id)
{
    int ret = 0;
    struct ccdk_framework_session *session = NULL;

    if (session_id >= CCDK_MAX_SESSION_NUM) {
        return EC_CCDK_OUT_OF_RANGE;
    }

    pthread_mutex_lock(&g_framework.mp_mutex);
    session = &g_framework.session[session_id];

    if (rte_atomic32_read(&session->status) != CCDK_USED) {
        pthread_mutex_unlock(&g_framework.mp_mutex);
        return EC_CCDK_OK;
    }

    if (session->epfd > 0) {
        close(session->epfd);
        session->epfd = 0;
    }

    rte_atomic32_set(&session->heartbeat, 0);

    for (int i = 0; i < CCDK_SESSION_MAX_EVENTFD; i++) {
        if (session->event_fd[i] > 0) {
            close(session->event_fd[i]);
            session->event_fd[i] = 0;
        }
    }

    ret = ccdk_framework_unregister(session_id);

    if (ret < EC_CCDK_OK) {
        goto fail;
    }

    FRAMEWORK_INFO("free session success, session_id = %d\n", session_id);
    pthread_mutex_unlock(&g_framework.mp_mutex);
    return EC_CCDK_OK;
fail:
    pthread_mutex_unlock(&g_framework.mp_mutex);
    return ret;
}


static int
ccdk_framework_get_session(enum ccdk_framework_session_mode mode, ccdk_session_req_action_t action, void *arg,
                           uint32_t *session_id)
{
    int ret = 0;
    uint32_t id = 0;
    struct ccdk_framework_session *session = NULL;
    pthread_mutex_lock(&g_framework.mp_mutex);
    ret = ccdk_framework_register(mode, &id);

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("ccdk_framework_register failed, ret = %d", ret);
        pthread_mutex_unlock(&g_framework.mp_mutex);
        return ret;
    }

    session = &g_framework.session[id];
    ret = ccdk_framework_get_session_event_fd(id);

    if (ret < EC_CCDK_OK) {
        goto fail;
    }

    ret = ccdk_framework_get_session_epfd(id);

    if (ret < EC_CCDK_OK) {
        goto fail;
    }

    session->req_id = 0;
    session->session_id = id;
    session->action = action;
    session->arg = arg;

    if (CCDK_SESSION_OPEN == mode) {
        if (session->event_fd[CCDK_SESSION_SYNC_EVENTFD] > 0) {
            ret = ccdk_framework_attach_session_event_fd_to_epoll(session->epfd,
                    session->event_fd[CCDK_SESSION_SYNC_EVENTFD], &session->event[CCDK_SESSION_SYNC_EVENTFD],
                    EPOLL_CTL_ADD, NULL);

            if (ret < EC_CCDK_OK) {
                FRAMEWORK_ERR("ccdk_framework_attach_session_event_fd_to_epoll failed, error %i (%s)\n", errno,
                              strerror(errno));
                goto fail;
            }
        }

        if (action) {
            if (session->event_fd[CCDK_SESSION_ASYNC_EVENTFD] > 0) {
                ret = ccdk_framework_attach_session_event_fd_to_epoll(g_framework.session_epfd,
                        session->event_fd[CCDK_SESSION_ASYNC_EVENTFD],
                        &session->event[CCDK_SESSION_ASYNC_EVENTFD], EPOLL_CTL_ADD, session);

                if (ret < EC_CCDK_OK) {
                    FRAMEWORK_ERR("ccdk_framework_attach_session_event_fd_to_epoll failed, error %i (%s)\n", errno,
                                  strerror(errno));
                    goto fail;
                }
            }
        }
    }

    TAILQ_INIT(&session->head);
    rte_spinlock_init(&session->lock);

    if (session_id) {
        *session_id = id;
    }

    FRAMEWORK_INFO("get session success, session_id = %d, sync event_fd = %d, async event_fd = %d, epfd = %d, latency = %p, request_cache = %p\n",
                   id, session->event_fd[CCDK_SESSION_SYNC_EVENTFD], session->event_fd[CCDK_SESSION_ASYNC_EVENTFD],
                   session->epfd, session->latency, session->request_cache);
    pthread_mutex_unlock(&g_framework.mp_mutex);
    return EC_CCDK_OK;
fail:
    pthread_mutex_unlock(&g_framework.mp_mutex);
    ret = ccdk_framework_free_session(id);

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("ccdk_framework_unregister failed, error %i (%s)\n", errno, strerror(errno));
    }

    return ret;
}

int
ccdk_framework_open_session(ccdk_session_req_action_t action, void *arg)
{
    int ret = 0;
    uint32_t session_id;
    ret = ccdk_framework_find_session_id(&session_id);

    if (likely(EC_CCDK_OK == ret)) {
        return (int)session_id;
    }

    ret = ccdk_framework_get_session(CCDK_SESSION_OPEN, action, arg, &session_id);

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("ccdk_framework_get_session failed, ret = %d\n", ret);
        return ret;
    }

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        g_framework.session[session_id].is_local = 1;
    } else {
        g_framework.session[session_id].is_local = 0;
    }

    g_thread_local.session_id = session_id;
    FRAMEWORK_INFO("open session success, session_id = %d\n",  g_thread_local.session_id);
    return (int)session_id;
}


int
ccdk_framework_close_session(void)
{
    int ret = 0;
    ret = ccdk_framework_free_session(g_thread_local.session_id);

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("ccdk_framework_free_session failed, ret = %d, session_id = %d", ret,
                      g_thread_local.session_id);
    }

    memset(&g_thread_local, 0, sizeof(struct ccdk_framework_thread_local));
    g_thread_local.session_id = CCDK_INVALID_SESSION_ID;
    return ret;
}

int
ccdk_framework_get_session_latency_info(uint32_t session_id, struct ccdk_session_latency *session_latency)
{
    struct ccdk_framework_session *session = NULL;
    struct ccdk_framework_session_latency *latency = NULL;
#define CALE_latency	1000000

    if (session_id >= CCDK_MAX_SESSION_NUM) {
        return EC_CCDK_OUT_OF_RANGE;
    }

    session = &g_framework.session[session_id];

    if (!session_latency || !session->latency) {
        return EC_CCDK_NULL_PTR;
    }

    latency = session->latency;
    session_latency->session_id = session_id;
    //FRAMEWORK_DEBUG("session_id = %d, latency = %p, rte_get_tsc_hz() = %lld\n\n",  session_id, latency, rte_get_tsc_hz());
    session_latency->min_start_2_action_latency = rte_atomic64_read(&latency->min_start_2_action_latency) *
            CALE_latency /
            rte_get_tsc_hz();
    session_latency->min_action_latency = rte_atomic64_read(&latency->min_action_latency) * CALE_latency /
                                          rte_get_tsc_hz();
    session_latency->min_action_2_intr_latency = rte_atomic64_read(&latency->min_action_2_intr_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->min_intr_2_complete_latency = rte_atomic64_read(&latency->min_intr_2_complete_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->min_start_2_complete_latency = rte_atomic64_read(&latency->min_start_2_complete_latency) *
            CALE_latency / rte_get_tsc_hz();
    /*FRAMEWORK_DEBUG("min_start_2_action_latency = %lld, min_action_latency = %lld, min_action_2_intr_latency = %lld, min_intr_2_complete_latency = %lld, min_start_2_complete_latency = %lld\n",
               rte_atomic64_read(&latency->min_start_2_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->min_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->min_action_2_intr_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->min_intr_2_complete_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->min_start_2_complete_latency) * CALE_latency / rte_get_tsc_hz());
               */
    session_latency->max_start_2_action_latency = rte_atomic64_read(&latency->max_start_2_action_latency) *
            CALE_latency /
            rte_get_tsc_hz();
    session_latency->max_action_latency = rte_atomic64_read(&latency->max_action_latency) * CALE_latency /
                                          rte_get_tsc_hz();
    session_latency->max_action_2_intr_latency = rte_atomic64_read(&latency->max_action_2_intr_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->max_intr_2_complete_latency = rte_atomic64_read(&latency->max_intr_2_complete_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->max_start_2_complete_latency = rte_atomic64_read(&latency->max_start_2_complete_latency) *
            CALE_latency / rte_get_tsc_hz();
    /*FRAMEWORK_DEBUG("max_start_2_action_latency = %lld, max_action_latency = %lld, max_action_2_intr_latency = %lld, max_intr_2_complete_latency = %lld, max_start_2_complete_latency = %lld\n",
               rte_atomic64_read(&latency->max_start_2_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->max_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->max_action_2_intr_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->max_intr_2_complete_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->max_start_2_complete_latency) * CALE_latency / rte_get_tsc_hz());
            */
    session_latency->total_start_2_action_cnt = rte_atomic64_read(&latency->total_start_2_action_cnt);
    session_latency->total_action_cnt = rte_atomic64_read(&latency->total_action_cnt);
    session_latency->total_action_2_intr_cnt = rte_atomic64_read(&latency->total_action_2_intr_cnt);
    session_latency->total_intr_2_complete_cnt = rte_atomic64_read(&latency->total_intr_2_complete_cnt);
    session_latency->total_start_2_complete_cnt = rte_atomic64_read(&latency->total_start_2_complete_cnt);
    /*FRAMEWORK_DEBUG("total_start_2_action_cnt = %lld, total_action_cnt = %lld, total_action_2_intr_cnt = %lld, total_intr_2_complete_cnt = %lld, total_start_2_complete_cnt = %lld\n",
               rte_atomic64_read(&latency->total_start_2_action_cnt),
               rte_atomic64_read(&latency->total_action_cnt),
               rte_atomic64_read(&latency->total_action_2_intr_cnt),
               rte_atomic64_read(&latency->total_intr_2_complete_cnt),
               rte_atomic64_read(&latency->total_start_2_complete_cnt));
               */
    session_latency->total_start_2_action_latency = rte_atomic64_read(&latency->total_start_2_action_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->total_action_latency = rte_atomic64_read(&latency->total_action_latency) * CALE_latency /
                                            rte_get_tsc_hz();
    session_latency->total_action_2_intr_latency = rte_atomic64_read(&latency->total_action_2_intr_latency) *
            CALE_latency / rte_get_tsc_hz();
    session_latency->total_intr_2_complete_latency = rte_atomic64_read(&latency->total_intr_2_complete_latency)
            * CALE_latency / rte_get_tsc_hz();
    session_latency->total_start_2_complete_latency = rte_atomic64_read(&latency->total_start_2_complete_latency)
            *
            CALE_latency / rte_get_tsc_hz();
    /*FRAMEWORK_DEBUG("total_start_2_action_latency = %lld, total_action_latency = %lld, total_action_2_intr_latency = %lld, total_intr_2_complete_latency = %lld, total_start_2_complete_latency = %lld\n",
               rte_atomic64_read(&latency->total_start_2_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->total_action_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->total_action_2_intr_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->total_intr_2_complete_latency) * CALE_latency / rte_get_tsc_hz(),
               rte_atomic64_read(&latency->total_start_2_complete_latency) * CALE_latency / rte_get_tsc_hz());
    FRAMEWORK_DEBUG("yanshi total_start_2_action_latency = %lld, total_action_latency = %lld, total_action_2_intr_latency = %lld, total_intr_2_complete_latency = %lld, total_start_2_complete_latency = %lld\n",
               rte_atomic64_read(&latency->total_start_2_action_latency),
               rte_atomic64_read(&latency->total_action_latency),
               rte_atomic64_read(&latency->total_action_2_intr_latency),
               rte_atomic64_read(&latency->total_intr_2_complete_latency),
               rte_atomic64_read(&latency->total_start_2_complete_latency));
    */
    session_latency->avg_start_2_action_latency = AVG(session_latency->total_start_2_action_latency,
            session_latency->total_start_2_action_cnt);
    session_latency->avg_action_latency = AVG(session_latency->total_action_latency,
                                          session_latency->total_action_cnt);
    session_latency->avg_action_2_intr_latency = AVG(session_latency->total_action_2_intr_latency,
            session_latency->total_action_2_intr_cnt);
    session_latency->avg_intr_2_complete_latency = AVG(session_latency->total_intr_2_complete_latency,
            session_latency->total_intr_2_complete_cnt);
    session_latency->avg_start_2_complete_latency = AVG(session_latency->total_start_2_complete_latency,
            session_latency->total_start_2_complete_cnt);
    /*FRAMEWORK_DEBUG("avg_start_2_action_latency = %lld, avg_action_latency = %lld, avg_action_2_intr_latency = %lld, avg_intr_2_complete_latency = %lld, avg_start_2_complete_latency = %lld\n",
               AVG(rte_atomic64_read(&latency->total_start_2_action_latency) * CALE_latency / rte_get_tsc_hz(),
                   rte_atomic64_read(&latency->total_start_2_action_cnt)),
               AVG(rte_atomic64_read(&latency->total_action_latency) * CALE_latency / rte_get_tsc_hz(),
                   rte_atomic64_read(&latency->total_action_cnt)),
               AVG(rte_atomic64_read(&latency->total_action_2_intr_latency) * CALE_latency / rte_get_tsc_hz(),
                   rte_atomic64_read(&latency->total_action_2_intr_cnt)),
               AVG(rte_atomic64_read(&latency->total_intr_2_complete_latency) * CALE_latency / rte_get_tsc_hz(),
                   rte_atomic64_read(&latency->total_intr_2_complete_cnt)),
               AVG(rte_atomic64_read(&latency->total_start_2_complete_latency) * CALE_latency / rte_get_tsc_hz(),
                   rte_atomic64_read(&latency->total_start_2_complete_cnt)));
    */
    return EC_CCDK_OK;
}

static void
ccdk_framework_free_all_session(void)
{
    FRAMEWORK_INFO("\n");

    for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
        int ret = ccdk_framework_free_session(i);

        if (ret < EC_CCDK_OK) {
            FRAMEWORK_ERR("ccdk_framework_free_session fail. ret = %d\n", ret);
        }
    }
}


static inline struct ccdk_framework_server *
ccdk_framework_find_server(const uint32_t server_id)
{
    for (int i = 0; i < CCDK_MAX_SERVER_NUM; i++) {
        struct ccdk_framework_server *server = &g_framework.server[i];

        if (server->server_id == server_id) {
            return server;
        }
    }

    FRAMEWORK_INFO("cannot find server,server_id = 0x%x\n", server_id);
    return NULL;
}

static struct ccdk_framework_server *
ccdk_framework_get_server(const char *name, const uint32_t server_id)
{
    struct ccdk_framework_server *server = ccdk_framework_find_server(server_id);

    if (server) {
        return server;
    }

    pthread_mutex_lock(&g_framework.mp_mutex);

    for (int i = 0; i < CCDK_MAX_SERVER_NUM; i++) {
        server = &g_framework.server[i];

        if (server->status == CCDK_UNUSED) {
            if (server->event_fd <= 0) {
                int ret = ccdk_framework_get_server_event_fd(server_id);

                if (ret < EC_CCDK_OK) {
                    FRAMEWORK_ERR("ccdk_framework_get_server_event_fd fail\n");
                    goto fail;
                }

                server->event_fd = ret;
            }

            if (server->epfd <= 0) {
                int ret = ccdk_framework_get_server_epfd();

                if (ret < EC_CCDK_OK) {
                    FRAMEWORK_ERR("ccdk_framework_get_server_epfd fail\n");
                    goto fail;
                }

                server->epfd = ret;
            }

            if (!server->ring) {
                struct rte_ring *ring;
                ring = ccdk_framework_get_server_ring(name);

                if (!ring) {
                    FRAMEWORK_ERR("ccdk_framework_get_server_ring fail\n");
                    goto fail;
                }

                server->ring = ring;
            }

            server->name = name;
            server->server_id = server_id;
            server->status = CCDK_USED;
            TAILQ_INIT(&server->head);
            rte_spinlock_init(&server->lock);
            FRAMEWORK_INFO("get server success, name = %s, server_id = 0x%x, event_fd = %d, epfd = %d, ring = %p\n",
                           server->name, server->server_id, server->event_fd, server->epfd, server->ring);
            pthread_mutex_unlock(&g_framework.mp_mutex);
            return server;
        }
    }

fail:
    pthread_mutex_unlock(&g_framework.mp_mutex);
    return NULL;
}


static void
ccdk_framework_free_all_server(void)
{
    for (int i = 0; i < CCDK_MAX_SERVER_NUM; i++) {
        struct ccdk_framework_server *server = &g_framework.server[i];
        server->thread_running = 0;

        for (int j = 0; j < CCDK_MAX_SERVER_THREAD_NUM; j++) {
            if (server->thread[j] > 0) {
                pthread_join(server->thread[j], NULL);
            }
        }

        if (server->epfd > 0) {
            close(server->epfd);
            server->epfd = 0;
        }

        if (server->event_fd > 0) {
            close(server->event_fd);
            server->event_fd = 0;
        }

        if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
            if (server->ring) {
                rte_ring_free(server->ring);
                server->ring = NULL;
            }
        }

        server->name = NULL;
        server->server_id = 0;
        server->action = NULL;
        server->arg = NULL;
        server->status = CCDK_UNUSED;
        FRAMEWORK_DEBUG("free server success, name = %s\n", server->name);
    }
}

/// @brief ccdk_framework_set_msg_done
/// msg和head处于同一个缓存，|___head____|______msg________|
/// 则通过container_of 可以找到head，从而直接设置req result
/// @returns
/// @param request
int
ccdk_framework_set_msg_done(void *msg, int result)
{
    int ret = 0;
    struct ccdk_framework_session *session;
    struct ccdk_framework_req *req = container_of(msg,
                                     struct ccdk_framework_req, msg);
    FRAMEWORK_DEBUG("req = %p, msg = %p\n", req, msg);

    if (unlikely(req->session_id > CCDK_MAX_SESSION_NUM)) {
        return EC_CCDK_OUT_OF_RANGE;
    }

    session = &g_framework.session[req->session_id];

    if (req->behind_is_async) {
        ccdk_framework_cale_action_2_intr_latency(0, req, session->latency);
    } else {
        ccdk_framework_cale_action_2_intr_latency(req->action_after_ts, req, session->latency);
    }

    rte_atomic32_set(&req->result, result);
#ifdef EVENTFD_MODE

    if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
        FRAMEWORK_ERR("req magic fail. req = %p, magic = 0x%x\n", req, rte_atomic32_read(&req->magic));
        return EC_CCDK_NOT_INIT;
    }

    FRAMEWORK_DEBUG("session_id = %d, front_is_async = %d, sync event_fd = %d, async event_fd = %d\n",
                    req->session_id,
                    req->front_is_async, session->event_fd[0], session->event_fd[1]);

    if (req->front_is_async) {
        ret = ccdk_framework_eventfd_signal(session->event_fd[CCDK_SESSION_ASYNC_EVENTFD]);
    } else {
        //FRAMEWORK_ERR("req = %p, eventfd = %d\n", req, session->event_fd[CCDK_SESSION_SYNC_EVENTFD]);
        #ifdef CV
        std::unique_lock<std::mutex> lk(session->mtx);
		session_dataReady = true;
    	session->cv_data.notify_one();
		#else
        ret = ccdk_framework_eventfd_signal(session->event_fd[CCDK_SESSION_SYNC_EVENTFD]);
		#endif
    }

    if (unlikely(ret < EC_CCDK_OK)) {
        FRAMEWORK_ERR("can't trigger eventfd, error %i (%s)\n", errno, strerror(errno));
        return ret;
    }

#endif
    return EC_CCDK_OK;
}

static int
ccdk_framework_server_eventfd_mode(struct ccdk_framework_server *server)
{
    int ret = 0;
    struct ccdk_framework_session *session = NULL;
    struct ccdk_framework_req *req = NULL;
    struct ccdk_framework_req *next_req = NULL;
    struct rte_epoll_event revent = {};
    
    #ifdef CV
	std::unique_lock<std::mutex> lk(server->mtx);
    //server->cv_data.wait(lk, []{ return server_dataReady.load(); });
    server->cv_data.wait(lk);
	#else
	int n = rte_epoll_wait(server->epfd, &revent, 1, 1000);
	
    FRAMEWORK_DEBUG("rte_epoll_wait, n = %d, pid = %d\n",  n, syscall(SYS_gettid));

    if (unlikely(n < 0)) {
        FRAMEWORK_ERR("rte_epoll_wait failed, error %i (%s)\n", errno, strerror(errno));
        return EC_CCDK_RETRY;
    }
	
	#endif
	
    if (rte_ring_dequeue(server->ring, (void **)&next_req) < 0) {
        return EC_CCDK_OK;
    }

    while (next_req) {
        req = next_req;

        if (rte_ring_dequeue(server->ring, (void **)&next_req) == 0) {
            /// @brief 预取到cache
            rte_prefetch0(next_req);
        } else {
            next_req = NULL;
        }

        FRAMEWORK_DEBUG("next_req = %p, req = %p, session_id = %d, req_id = %d, pid = %d\n",
                        next_req, req, req->session_id, req->req_id, syscall(SYS_gettid));

        if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
            FRAMEWORK_ERR("req magic fail. req = %p, magic = 0x%x\n", req, rte_atomic32_read(&req->magic));
            continue;
        }

        session = &g_framework.session[req->session_id];

        if (unlikely(rte_atomic32_read(&session->status) == CCDK_UNUSED)) {
            FRAMEWORK_ERR("session is invalid, because session->status = %d, session_id = %d\n",
                          rte_atomic32_read(&session->status),
                          req->session_id);
            rte_atomic32_set(&req->result, EC_CCDK_SESSION_UNUSED);
            continue;
        }

        rte_atomic32_set(&session->heartbeat, 0);
        ccdk_framework_cale_start_2_action_latency(0, req, session->latency);

        if (server->action) {
            //pthread_mutex_lock(&server->mutex);
            ret = server->action(server->arg, (void *)req->msg);
            //pthread_mutex_unlock(&server->mutex);
        }

        ccdk_framework_cale_action_latency(0, req, session->latency);

        if (ret <= 0) {
            req->behind_is_async = 0;
            ret = ccdk_framework_set_msg_done((void *)req->msg, ret);

            if (ret < EC_CCDK_OK) {
                FRAMEWORK_ERR("ccdk_framework_set_msg_done fail.\n");
            }
        } else { // > 0 is async mode
            req->behind_is_async = 1;
        }

        /// @brief 再次判断ring内是否有req
        if (!next_req) {
            if (rte_ring_dequeue(server->ring, (void **)&next_req) == 0) {
                /// @brief 预取到cache
                rte_prefetch0(next_req);
                FRAMEWORK_DEBUG("try to get next_req success, next_req = %p\n", next_req);
            }
        }
    }

    return EC_CCDK_OK;
}


static int
ccdk_framework_server_poll_mode(struct ccdk_framework_server *server)
{
    int ret = 0;
    struct ccdk_framework_session *session = NULL;
    struct ccdk_framework_req *req = NULL;

    if (rte_ring_dequeue(server->ring, (void **)&req) < 0) {
        usleep(100);
        return EC_CCDK_OK;
    }

    if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
        FRAMEWORK_ERR("req magic fail. req = %p, magic = 0x%x\n", req, rte_atomic32_read(&req->magic));
        return EC_CCDK_OK;
    }

    session = &g_framework.session[req->session_id];

    if (rte_atomic32_read(&session->status) == CCDK_UNUSED) {
        FRAMEWORK_DEBUG("session is invalid, because session->status = %d, session_id = %d\n",
                        rte_atomic32_read(&session->status),
                        req->session_id);
        rte_atomic32_set(&req->result, EC_CCDK_SESSION_UNUSED);
        return EC_CCDK_OK;
    }

    rte_atomic32_set(&session->heartbeat, 0);
    ccdk_framework_cale_start_2_action_latency(0, req, session->latency);

    if (server->action) {
        //pthread_mutex_lock(&server->mutex);
        ret = server->action(server->arg, (void *)req->msg);
        //pthread_mutex_unlock(&server->mutex);
    }

    ccdk_framework_cale_action_latency(0, req, session->latency);

    if (ret <= 0) {
        req->behind_is_async = 0;
        ret = ccdk_framework_set_msg_done((void *)req->msg, ret);

        if (ret < EC_CCDK_OK) {
            FRAMEWORK_ERR("ccdk_framework_set_msg_done fail.\n");
        }
    } else { // > 0 is async mode
        req->behind_is_async = 1;
    }

    return EC_CCDK_OK;
}


static void *
ccdk_framework_server_thread_main(void *arg)
{
    int ret = 0;
    struct ccdk_framework_server *server = (struct ccdk_framework_server *)arg;
    struct rte_epoll_event event;
    struct rte_epoll_data *epdata;
    epdata = &event.epdata;
    epdata->event  = EPOLLIN | EPOLLPRI | EPOLLET;
    epdata->data   = NULL;
    epdata->cb_fun = (rte_intr_event_cb_t)ccdk_framework_epoll_read_cb;
    epdata->cb_arg = (void *)NULL;
    ret = rte_epoll_ctl(server->epfd, EPOLL_CTL_ADD, server->event_fd, &event);

    if (ret < 0) {
        FRAMEWORK_ERR("can't add eventfd, error %i (%s)\n", errno, strerror(errno));
    }

    FRAMEWORK_INFO("server thread start running, name = %s, epfd = %d, event_fd = %d, ring = %p\n",
                   server->name,
                   server->epfd, server->event_fd, server->ring);

    while (server->thread_running) {
#ifdef EVENTFD_MODE

        if (unlikely(ccdk_framework_server_eventfd_mode(server)) < EC_CCDK_OK) {
            break;
        }

#else

        if (unlikely(ccdk_framework_server_poll_mode(server)) < EC_CCDK_OK) {
            break;
        }

#endif
    }

    return NULL;
}

static int ccdk_framework_register_server_primary(const char *name, const uint32_t server_id, int thread_num,
        ccdk_server_action_t action, void *arg, int core_id)
{
    int ret = 0;
    int lcore_id = core_id;
    int count = RTE_MIN(thread_num, CCDK_MAX_SERVER_THREAD_NUM);
    struct ccdk_framework_server *server = ccdk_framework_get_server(name, server_id);

    if (!server) {
        return EC_CCDK_CREATE_FAIL;
    }

    server->action = action;
    server->arg = arg;
    server->thread_running = 1;
    pthread_mutex_init(&server->mutex, NULL);

    for (int i = 0; i < count; i++) {
        char namebuf[64] = {0};

        if (!lcore_id) {
            if (!g_framework.desc.server_binding_policy) {
                snprintf(namebuf, sizeof(namebuf), "%s-%d-bm", name, i);
                ret = pthread_create(&server->thread[i], NULL, ccdk_framework_server_thread_main, server);
                (void)rte_thread_setname(server->thread[i], namebuf);
            } else {
                snprintf(namebuf, sizeof(namebuf), "%s-%d-nb", name, i);
                ret = rte_ctrl_thread_create(&server->thread[i], namebuf, NULL, ccdk_framework_server_thread_main, server);
            }
        } else {
            snprintf(namebuf, sizeof(namebuf), "%s-%d-b_%d", name, i, lcore_id);
            ret = rte_eal_remote_launch(ccdk_framework_server_thread_main, server, lcore_id);
            lcore_id++;
        }

        FRAMEWORK_INFO("create ccdk server thread. namebuf = %s\n", namebuf);

        if (ret < 0) {
            FRAMEWORK_ERR("ERROR: Cannot create ccdk server thread! ret = %d\n", ret);
            return EC_CCDK_CREATE_FAIL;
        }
    }

    return EC_CCDK_OK;
}


static int ccdk_framework_register_server_secondary(const char *name, const uint32_t server_id)
{
    struct ccdk_framework_server *server = ccdk_framework_get_server(name, server_id);

    if (server) {
        return EC_CCDK_OK;
    }

    return EC_CCDK_CREATE_FAIL;
}

int ccdk_framework_register_server(const char *name, const uint32_t server_id, int thread_num,
                                   ccdk_server_action_t action, void *arg, int core_id)
{
    int ret;

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        ret = ccdk_framework_register_server_primary(name, server_id, thread_num, action, arg, core_id);
    } else {
        ret = ccdk_framework_register_server_secondary(name, server_id);
    }

    return ret;
}

static int ccdk_framework_sync_wait_response(int epfd, int maxevents, int timeout)
{
    int n = 0;
    struct rte_epoll_event revent[CCDK_MAX_SESSION_NUM] = {};
    n = rte_epoll_wait(epfd, revent, maxevents, timeout);
    FRAMEWORK_DEBUG("rte_epoll_wait, n = %d\n", n);

    if (unlikely(!n)) {
        FRAMEWORK_DEBUG("rte_epoll_wait timeuot, epfd = %d\n", epfd);
        return EC_CCDK_TIMEOUT;
    } else if (unlikely(n < 0)) {
        FRAMEWORK_ERR("rte_epoll_wait returned error %d, epfd= %d", n, epfd);
        return EC_CCDK_RETRY;
    }

    return EC_CCDK_OK;
}


static int ccdk_framework_sync_wait_response_condition(struct ccdk_framework_session *session)
{
	FRAMEWORK_DEBUG("\n");
	std::unique_lock<std::mutex> lk(session->mtx);
    session->cv_data.wait(lk, []{ return session_dataReady.load(); });
    //session->cv_data.wait(lk);
	FRAMEWORK_DEBUG("\n");
    return EC_CCDK_OK;
}

static int ccdk_framework_async_wait_response(int epfd, int maxevents, int timeout)
{
    int ret = 0, n = 0;
    struct ccdk_framework_req *req, *next;
#ifdef EVENTFD_MODE
    struct rte_epoll_event revent[CCDK_MAX_SESSION_NUM] = {};
    n = rte_epoll_wait(epfd, revent, maxevents, timeout);
    FRAMEWORK_DEBUG("rte_epoll_wait, n = %d, pid = %d\n", n, syscall(SYS_gettid));

    if (unlikely(!n)) {
        FRAMEWORK_DEBUG("rte_epoll_wait timeuot, epfd = %d\n", epfd);
        return EC_CCDK_TIMEOUT;
    } else if (unlikely(n < 0)) {
        FRAMEWORK_ERR("rte_epoll_wait returned error %d, epfd= %d", n, epfd);
        return EC_CCDK_GENERAL_BASE;
    } else {
        ;
    }

    for (int i = 0; i < n; i++) {
        struct ccdk_framework_session *session = (struct ccdk_framework_session *)revent[i].epdata.data;

        /// @brief only async valid
        if (session) {
            int result = 0;

            if (rte_atomic32_read(&session->status) != CCDK_USED) {
                continue;
            }

again:
            rte_spinlock_lock(&session->lock);

            for (req = TAILQ_FIRST(&session->head); req != NULL; req = next) {
                next = TAILQ_NEXT(req, next);
                result = rte_atomic32_read(&req->result);
                FRAMEWORK_DEBUG("req = %p, session_id = %d, req_id = %d, result = %d, next = %p\n",  req,
                                req->session_id,
                                req->req_id,
                                result,
                                next);

                if (result != CCDK_REQ_UNDONE) {
                    ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
                    TAILQ_REMOVE(&session->head, req, next);
                    rte_spinlock_unlock(&session->lock);

                    if (session->action) {
                        session->action(session->arg, (void *)req->msg, result);
                    }

                    /// @brief 重新扫描所有req是否完成
                    goto again;
                }
            }

            rte_spinlock_unlock(&session->lock);
        }
    }

#else

    for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
        struct ccdk_framework_session *session = &g_framework.session[i];

        if (rte_atomic32_read(&session->status) == CCDK_USED) {
            int result = rte_atomic32_read(&req->result);
            rte_spinlock_lock(&session->lock);

            for (req = TAILQ_FIRST(&session->head); req != NULL; req = next) {
                next = TAILQ_NEXT(req, next);
                FRAMEWORK_DEBUG("session_id = %d, req_id = %d, result = %d, next = %p\n",  req->session_id,
                                req->req_id,
                                result,
                                next);

                if (result == CCDK_REQ_DONE) {
                    ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
                    TAILQ_REMOVE(&session->head, req, next);

                    if (session->action) {
                        session->action(session->arg, (void *)req->msg, result);
                    }
                }
            }

            rte_spinlock_unlock(&session->lock);
        }
    }

    usleep(100);
#endif
    return ret;
}

static int
ccdk_framework_local_sync_handle_request(uint32_t session_id, uint32_t server_id, void *msg, int timeout)
{
    int ret;
    struct ccdk_framework_req *req;
    struct ccdk_framework_session *session = NULL;
    struct ccdk_framework_server *server = NULL;

    if (unlikely(session_id >= CCDK_MAX_SESSION_NUM)) {
        FRAMEWORK_ERR("session_id error, session_id =  %d\n", session_id);
        return EC_CCDK_OUT_OF_RANGE;
    }

    session = &g_framework.session[session_id];
    server = ccdk_framework_find_server(server_id);

    if (unlikely(!server || !server->action)) {
        return EC_CCDK_NOT_EXIST;
    }

    req = container_of(msg, struct ccdk_framework_req, msg);

    if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
        FRAMEWORK_ERR("req magic fail. magic = 0x%x\n", rte_atomic32_read(&req->magic));
        return EC_CCDK_NOT_INIT;
    }

    req->session_id = session_id;
    req->server_id = server_id;
    req->req_id = session->req_id++;
    req->front_is_async = 0;
    rte_atomic32_set(&req->result, CCDK_REQ_UNDONE);
    req->start_ts = rte_rdtsc_precise();
    FRAMEWORK_DEBUG("start_ts = 0x%lu\n", req->start_ts);
    /// @brief start_2_action_latency
    ccdk_framework_cale_start_2_action_latency(req->start_ts, req, session->latency);
    //pthread_mutex_lock(&server->mutex);
    ret = server->action(server->arg, msg);
    //pthread_mutex_unlock(&server->mutex);
    /// @brief action_latency
    ccdk_framework_cale_action_latency(0, req, session->latency);

    /// @brief server action is async
    if (ret > 0) {
#ifdef EVENTFD_MODE
        /// @brief wait for async response
        ret = ccdk_framework_sync_wait_response(session->epfd, 1, timeout);

        if (unlikely(ret < EC_CCDK_OK)) {
            FRAMEWORK_ERR("wait response failed! req = %p, ret = %d, req->result = %d, count = %d\n", req, ret,
                          rte_atomic32_read(&req->result), rte_ring_count(server->ring));
            ccdk_framework_cale_action_2_intr_latency(0, req, session->latency);
            ccdk_framework_cale_intr_2_complete_latency(req->intr_handle_ts, req, session->latency);
            FRAMEWORK_ERR("start_2_action latency :: start_ts = 0x%lu, action_before_ts = 0x%lu, start_2_action_latency = 0x%lu\n",
                          req->start_ts, req->action_before_ts, req->start_2_action_latency);
            FRAMEWORK_ERR("action latency :: action_before_ts = 0x%lu, action_after_ts = 0x%lu, action_latency = 0x%lu\n",
                          req->action_before_ts, req->action_after_ts, req->action_latency);
            FRAMEWORK_ERR("action_2_intr latency :: action_after_ts = 0x%lu, intr_handle_ts = 0x%lu, action_2_intr_latency = 0x%lu\n",
                          req->action_after_ts, req->intr_handle_ts, req->action_2_intr_latency);
            FRAMEWORK_ERR("intr_2_complete latency :: intr_handle_ts = 0x%lu, complete_ts = 0x%lu, intr_2_complete_latency = 0x%lu\n",
                          req->intr_handle_ts, req->complete_ts, req->intr_2_complete_latency);
            FRAMEWORK_ERR("start_2_complete latency :: start_ts = 0x%lu, complete_ts = 0x%lu, start_2_complete_latency = %ld\n",
                          req->start_ts, req->complete_ts, req->start_2_complete_latency);
            return ret;
        }

        ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
        ret = rte_atomic32_read(&req->result);
#else
        int cnt = 0;

        while (rte_atomic32_read(&req->result) == CCDK_REQ_UNDONE) {
            usleep(100);

            if (++cnt * 1000 > timeout) {
                ret = EC_CCDK_TIMEOUT;
                FRAMEWORK_ERR("wait response failed! ret = %d\n", ret);
                ccdk_framework_cale_action_2_intr_latency(0, req, session->latency);
                ccdk_framework_cale_intr_2_complete_latency(req->intr_handle_ts, req, session->latency);
                return ret;
            }
        }

        ret = rte_atomic32_read(&req->result);
        ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
#endif
    } else {
        ccdk_framework_cale_action_2_intr_latency(req->action_after_ts, req, session->latency);
        ccdk_framework_cale_intr_2_complete_latency(req->intr_handle_ts, req, session->latency);
    }

    return ret;
}


static int
ccdk_framework_send_request(int is_async, uint32_t session_id, uint32_t server_id, void *msg, int timeout)
{
    int ret = EC_CCDK_FAIL;
    struct ccdk_framework_session *session = NULL;
    struct ccdk_framework_server *server = NULL;

    if (unlikely(session_id >= CCDK_MAX_SESSION_NUM)) {
        FRAMEWORK_ERR("session_id error, session_id =  %d\n", session_id);
        return EC_CCDK_OUT_OF_RANGE;
    }

    session = &g_framework.session[session_id];
    server = ccdk_framework_find_server(server_id);

    if (server) {
        struct ccdk_framework_req *req;
        req = container_of(msg, struct ccdk_framework_req, msg);

        if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
            FRAMEWORK_ERR("req magic fail. magic = 0x%x\n", rte_atomic32_read(&req->magic));
            return EC_CCDK_NOT_INIT;
        }

        req->session_id = session_id;
        req->server_id = server_id;
        req->req_id = session->req_id++;
        req->front_is_async = is_async;
        rte_atomic32_set(&req->result, CCDK_REQ_UNDONE);
        FRAMEWORK_DEBUG("session_id = %d, req = %p, req_id = %d, msg = %p\n", session_id, req, req->req_id, msg);
        req->start_ts = rte_rdtsc_precise();
        FRAMEWORK_DEBUG("start_ts = 0x%lu\n", req->start_ts);
        ret = rte_ring_enqueue(server->ring, (void *)req);

        if (unlikely(ret < 0)) {
            FRAMEWORK_ERR("rte_ring_enqueue error, ret %d\n", ret);
            return EC_CCDK_RETRY;
        }

#ifdef EVENTFD_MODE

        /// @brief wait for async response
        if (!is_async) {
            /// @brief trigger
            #ifdef CV
			ret = ccdk_framework_condition_signal(server);
			#else
            ret = ccdk_framework_eventfd_signal(server->event_fd);
			#endif

            if (unlikely(ret < EC_CCDK_OK)) {
                FRAMEWORK_ERR("event fd signal error, t->event_fd = %d, ret %d\n", server->event_fd, ret);
                return ret;
            }

            /// @brief wait for response
            #ifdef CV
			ret = ccdk_framework_sync_wait_response_condition(session);
			#else
            ret = ccdk_framework_sync_wait_response(session->epfd, 1, timeout);
			#endif
            if (unlikely(ret < EC_CCDK_OK)) {
                FRAMEWORK_ERR("wait response failed! req = %p, ret = %d, req->result = %d, count = %d\n", req, ret,
                              rte_atomic32_read(&req->result), rte_ring_count(server->ring));
                FRAMEWORK_ERR("start_2_action latency :: start_ts = 0x%lu, action_before_ts = 0x%lu, start_2_action_latency = 0x%lu\n",
                              req->start_ts, req->action_before_ts, req->start_2_action_latency);
                FRAMEWORK_ERR("action latency :: action_before_ts = 0x%lu, action_after_ts = 0x%lu, action_latency = 0x%lu\n",
                              req->action_before_ts, req->action_after_ts, req->action_latency);
                FRAMEWORK_ERR("action_2_intr latency :: action_after_ts = 0x%lu, intr_handle_ts = 0x%lu, action_2_intr_latency = 0x%lu\n",
                              req->action_after_ts, req->intr_handle_ts, req->action_2_intr_latency);
                FRAMEWORK_ERR("intr_2_complete latency :: intr_handle_ts = 0x%lu, complete_ts = 0x%lu, intr_2_complete_latency = 0x%lu\n",
                              req->intr_handle_ts, req->complete_ts, req->intr_2_complete_latency);
                FRAMEWORK_ERR("start_2_complete latency :: start_ts = 0x%lu, complete_ts = 0x%lu, start_2_complete_latency = %ld\n",
                              req->start_ts, req->complete_ts, req->start_2_complete_latency);
            } else {
                ret = rte_atomic32_read(&req->result);
            }

            ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
        } else {
            FRAMEWORK_DEBUG("add async list, session_id = %d, req = %p, req_id = %d\n",  session_id, req, req->req_id);
            rte_spinlock_lock(&session->lock);
            TAILQ_INSERT_TAIL(&session->head, req, next);
            rte_spinlock_unlock(&session->lock);
            /// @brief  add list first, and then trigger. 如果server响应够快，则在未插入list前就反触发到response，则在list找不到对应的req，导致req处理丢失
            ret = ccdk_framework_eventfd_signal(server->event_fd);

            if (unlikely(ret < EC_CCDK_OK)) {
                FRAMEWORK_ERR("event fd signal error, t->event_fd = %d, ret %d\n", server->event_fd, ret);
                return ret;
            }

            ret = 0;
        }

#else

        if (!is_async) {
            int cnt = 0;

            while (rte_atomic32_read(&req->result) == CCDK_REQ_UNDONE) {
                usleep(100);

                if (++cnt * 1000 > timeout) {
                    ret = EC_CCDK_TIMEOUT;
                    break;
                }
            }

            ret = rte_atomic32_read(&req->result);
            ccdk_framework_cale_intr_2_complete_latency(0, req, session->latency);
        } else {
            rte_spinlock_lock(&session->lock);
            FRAMEWORK_DEBUG("add async list, session_id = %d, req = %p, req_id = %d\n",  session_id, req, req->req_id);
            TAILQ_INSERT_TAIL(&session->head, req, next);
            rte_spinlock_unlock(&session->lock);
            ret = 0;
        }

#endif
    }

    return ret;
}


int
ccdk_framework_handle_sync_msg(uint32_t server_id, void *msg, int timeout)
{
    int ret = EC_CCDK_OK;
    uint32_t session_id;
    ret = ccdk_framework_find_session_id(&session_id);

    if (unlikely(ret < EC_CCDK_OK)) {
        FRAMEWORK_ERR("ccdk_framework_find_session_id failed, ret = %d\n", ret);
        return ret;
    }

    if (/*rte_eal_process_type() == RTE_PROC_PRIMARY*/0) {
        ret = ccdk_framework_local_sync_handle_request(session_id, server_id, msg, timeout);
    } else {
        ret = ccdk_framework_send_request(0, session_id, server_id, msg, timeout);
    }

    return ret;
}


int
ccdk_framework_handle_async_msg(uint32_t server_id, void *msg)
{
    int ret = EC_CCDK_OK;
    uint32_t session_id;
    ret = ccdk_framework_find_session_id(&session_id);

    if (unlikely(ret < EC_CCDK_OK)) {
        FRAMEWORK_ERR("ccdk_framework_find_session_id failed, ret = %d\n", ret);
        return ret;
    }

    return ccdk_framework_send_request(1, session_id, server_id, msg, -1);
}

static inline struct rte_mempool *
ccdk_framework_get_mempool(int size)
{
    int i = 0;

    for (i = 0; i < CCDK_MAX_MEMPOOL_NUM; i++) {
        if (g_framework.mem_pool[i].size == size) {
            return g_framework.mem_pool[i].pool;
        }
    }

    FRAMEWORK_ERR("mempool not exist. size = %d\n", size);
    return NULL;
}

static inline int
ccdk_framework_set_mempool(struct rte_mempool *pool, int size)
{
    int i = 0;

    for (i = 0; i < CCDK_MAX_MEMPOOL_NUM; i++) {
        if (g_framework.mem_pool[i].size == 0) {
            g_framework.mem_pool[i].size = size;
            g_framework.mem_pool[i].pool = pool;
            return EC_CCDK_OK;
        }
    }

    return EC_CCDK_FULL;
}

void *
ccdk_framework_mempool_get_msg(int size)
{
    int ret = 0;
    uint32_t session_id;
    struct ccdk_framework_session *session;
    struct rte_mempool_cache *request_cache;
    const unsigned flags = 0;
    const unsigned pool_cache = 32;
    const unsigned priv_data_sz = 0;
    struct ccdk_framework_req *req = NULL;
    uint32_t req_size = sizeof(struct ccdk_framework_req) + size;
    struct rte_mempool *request_pool = ccdk_framework_get_mempool(req_size);

    if (unlikely(!request_pool)) {
        int is_created = 0;
        char namebuf[32] = {0};
        /// @brief must be locked, when multiple theads to create together
        pthread_mutex_lock(&g_framework.mempool_mutex);
        FRAMEWORK_ERR("Want to create mempool\n");
        request_pool = ccdk_framework_get_mempool(req_size);

        if (!request_pool) {
            FRAMEWORK_ERR("Create mempool\n");
            snprintf(namebuf, sizeof(namebuf),
                     "%s-%d", "mempool", req_size);
            request_pool = rte_mempool_lookup(namebuf);
            FRAMEWORK_DEBUG("lookup request_pool = %p\n", request_pool);

            if (unlikely(!request_pool)) {
                request_pool = rte_mempool_create(namebuf, 2048,
                                                  req_size, pool_cache, priv_data_sz,
                                                  NULL, NULL, NULL, NULL,
                                                  SOCKET_ID_ANY, flags);

                if (!request_pool) {
                    FRAMEWORK_ERR("Failed to create mempool\n");
                    pthread_mutex_unlock(&g_framework.mempool_mutex);
                    return NULL;
                }

                FRAMEWORK_DEBUG("create request_pool = %p, avail_count = %d\n", request_pool,
                                rte_mempool_avail_count(request_pool));
                is_created = 1;
            }

            ret = ccdk_framework_set_mempool(request_pool, req_size);

            if (unlikely(ret < EC_CCDK_OK)) {
                FRAMEWORK_ERR("ccdk_framework_set_mempool error, ret %d\n", ret);

                if (is_created) {
                    rte_mempool_free(request_pool);
                }

                pthread_mutex_unlock(&g_framework.mempool_mutex);
                return NULL;
            }
        }

        pthread_mutex_unlock(&g_framework.mempool_mutex);
    }

    ret = ccdk_framework_find_session_id(&session_id);

    if (unlikely(ret < EC_CCDK_OK)) {
        ret = rte_mempool_get(request_pool, (void **)&req);
    } else {
        request_cache = g_framework.session[session_id].request_cache;

        if (unlikely(!request_cache)) {
            FRAMEWORK_ERR("request_cache is NULL.\n");
            return NULL;
        }

        ret = rte_mempool_get(request_pool, (void **)&req);
        //ret = rte_mempool_generic_get(request_pool, (void **)&req, 1, request_cache);
    }

    if (unlikely(ret < 0)) {
        FRAMEWORK_ERR("rte_mempool_get failed, ret %d, avail_count = %d\n", ret,
                      rte_mempool_avail_count(request_pool));
        return NULL;
    }

    memset(req, 0, sizeof(struct ccdk_framework_req));
    req->size = req_size;
    rte_atomic32_set(&req->magic, CCDK_REQ_MAGIC);
    FRAMEWORK_DEBUG("malloc success, session_id = %d, request_pool = %p, request_cache = %p, request = %p, msg = %p, avail_count = %d\n",
                    session_id, request_pool, request_cache, req, (void *)req->msg, rte_mempool_avail_count(request_pool));
    return (void *)req->msg;
}


int
ccdk_framework_mempool_put_msg(void *msg)
{
    int ret = 0;
    uint32_t session_id = 0;
    struct ccdk_framework_req *req = NULL;
    struct rte_mempool *request_pool = NULL;
    struct rte_mempool_cache *request_cache = NULL;

    if (unlikely(!msg)) {
        return EC_CCDK_NULL_PTR;
    }

    req = container_of(msg, struct ccdk_framework_req, msg);

    if (unlikely(rte_atomic32_read(&req->magic) != CCDK_REQ_MAGIC)) {
        FRAMEWORK_ERR("req magic fail. magic = 0x%x\n", rte_atomic32_read(&req->magic));
        return EC_CCDK_NOT_INIT;
    }

    request_pool = ccdk_framework_get_mempool(req->size);

    if (unlikely(!request_pool)) {
        return EC_CCDK_NOT_EXIST;
    }

    ret = ccdk_framework_find_session_id(&session_id);

    if (unlikely(ret < EC_CCDK_OK)) {
        /// @brief support other threads free msg
        rte_mempool_put(request_pool, req);
    } else {
        request_cache = g_framework.session[session_id].request_cache;

        if (unlikely(!request_cache)) {
            FRAMEWORK_ERR("request_cache is NULL\n");
            return NULL;
        }

        memset(req, 0, sizeof(struct ccdk_framework_req));
        rte_mempool_put(request_pool, req);
        //rte_mempool_generic_put(request_pool, &req, 1, request_cache);
        FRAMEWORK_DEBUG("session_id = %d\n", session_id);
    }

    FRAMEWORK_DEBUG("free msg, request_pool = %p, request_cache = %p, request = %p, msg = %p, avail_count = %d\n",
                    request_pool, request_cache, req, msg, rte_mempool_avail_count(request_pool));
    return EC_CCDK_OK;
}

void *
ccdk_framework_malloc_msg(int size)
{
    int req_len = sizeof(struct ccdk_framework_req) + size;
    struct ccdk_framework_req *req = NULL;
    req = (struct ccdk_framework_req *)rte_malloc("ccdk-framework", req_len, 0);

    if (unlikely(!req)) {
        FRAMEWORK_ERR("rte_malloc error\n");
        return NULL;
    }

    memset(req, 0, req_len);
    rte_atomic32_set(&req->magic, CCDK_REQ_MAGIC);
    FRAMEWORK_DEBUG("malloc success, request = %p, msg = %p\n", req, (void *)req->msg);
    return (void *)req->msg;
}

void
ccdk_framework_free_msg(void *msg)
{
    struct ccdk_framework_req *req = NULL;

    if (unlikely(!msg)) {
        return;
    }

    req = container_of(msg, struct ccdk_framework_req, msg);
    rte_free(req);
}

static void
ccdk_framework_free_mempool(void)
{
    for (int i = 0; i < CCDK_MAX_MEMPOOL_NUM; i++) {
        struct ccdk_framework_mempool *mem_pool = &g_framework.mem_pool[i];
        FRAMEWORK_DEBUG("free mempool, pool = %p, size = %d\n", mem_pool->pool, mem_pool->size);
        rte_mempool_free(mem_pool->pool);
        mem_pool->size = 0;
    }
}

static int
ccdk_framework_cfg_mp_primary(const struct rte_mp_msg *msg, const void *peer)
{
    int ret;
    struct rte_mp_msg reply;
    const struct ccdk_mp_param *m =
        (const struct ccdk_mp_param *)msg->param;
    memset(&reply, 0, sizeof(reply));

    switch (m->req) {
    case SOCKET_CCDK_REGISTER: {
        struct ccdk_mp_register_param *r = (struct ccdk_mp_register_param *)reply.param;
        const struct ccdk_mp_register_param *um =
            (const struct ccdk_mp_register_param *)msg->param;
        r->param.req = SOCKET_CCDK_REGISTER;
        r->param.result = ccdk_framework_get_session(CCDK_SESSION_MP, NULL, NULL,
                          &r->session_id);
        r->latency = g_framework.session[r->session_id].latency;
        r->request_cache = g_framework.session[r->session_id].request_cache;
    }
    break;

    case SOCKET_CCDK_UNREGISTER: {
        struct ccdk_mp_register_param *r = (struct ccdk_mp_register_param *)reply.param;
        const struct ccdk_mp_register_param *um =
            (const struct ccdk_mp_register_param *)msg->param;
        r->param.req = SOCKET_CCDK_UNREGISTER;
        r->session_id = um->session_id;
        r->param.result = ccdk_framework_free_session(r->session_id);
    }
    break;

    case SOCKET_CCDK_REQ_SERVER_EVENT_FD: {
        struct ccdk_framework_server *server = NULL;
        struct ccdk_mp_get_server_event_fd_param *r = (struct ccdk_mp_get_server_event_fd_param *)reply.param;
        const struct ccdk_mp_get_server_event_fd_param *server_m =
            (const struct ccdk_mp_get_server_event_fd_param *)msg->param;

        if (msg->len_param != sizeof(*server_m)) {
            FRAMEWORK_ERR("ccdk received invalid message, len_param is wrong\n");
            r->param.result = EC_CCDK_INV_ARG;
            break;
        }

        r->param.req = SOCKET_CCDK_REQ_SERVER_EVENT_FD;
        r->param.result = 0;
        r->server_id = server_m->server_id;
        server = ccdk_framework_find_server(r->server_id);

        if (!server) {
            r->param.result = EC_CCDK_NOT_EXIST;
            break;
        }

        reply.num_fds = 1;
        reply.fds[0] = server->event_fd;
        FRAMEWORK_DEBUG("server_id = 0x%x, event_fd = %d\n", r->server_id, server->event_fd);
    }
    break;

    case SOCKET_CCDK_REQ_SESSION_EVENT_FD: {
        struct ccdk_mp_get_session_event_fd_param *r = (struct ccdk_mp_get_session_event_fd_param *)reply.param;
        const struct ccdk_mp_get_session_event_fd_param *session_m =
            (const struct ccdk_mp_get_session_event_fd_param *)msg->param;

        if (msg->len_param != sizeof(*session_m)) {
            FRAMEWORK_ERR("ccdk received invalid message, len_param is wrong\n");
            r->param.result = EC_CCDK_INV_ARG;
            break;
        }

        if (session_m->session_id < CCDK_MAX_SESSION_NUM) {
            r->param.req = SOCKET_CCDK_REQ_SERVER_EVENT_FD;
            r->param.result = 0;
            r->session_id = session_m->session_id;
            reply.num_fds = 2;
            reply.fds[CCDK_SESSION_SYNC_EVENTFD] = g_framework.session[r->session_id].event_fd[CCDK_SESSION_SYNC_EVENTFD];
            reply.fds[CCDK_SESSION_ASYNC_EVENTFD] =
                g_framework.session[r->session_id].event_fd[CCDK_SESSION_ASYNC_EVENTFD];
            FRAMEWORK_DEBUG("session_id = %d, sync_event_fd = %d, async_event_fd = %d\n",  r->session_id,
                            g_framework.session[r->session_id].event_fd[CCDK_SESSION_SYNC_EVENTFD],
                            g_framework.session[r->session_id].event_fd[CCDK_SESSION_ASYNC_EVENTFD]);
        } else {
            r->param.result = EC_CCDK_OUT_OF_RANGE;
        }
    }
    break;

    default:
        FRAMEWORK_ERR("ccdk received invalid message!\n");
        return EC_CCDK_GENERAL_BASE;
    }

    strcpy(reply.name, CCDK_CFG_MP);
    reply.len_param = msg->len_param;
    ret = rte_mp_reply(&reply, peer);
    return ret;
}

static void
ccdk_framework_primary_check_heartbeat(struct ccdk_framework *framework)
{
    for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
        struct ccdk_framework_session *session = &framework->session[i];

        if (rte_atomic32_read(&session->status) == CCDK_UNUSED || session->is_local) {
            continue;
        }

        rte_atomic32_inc(&session->heartbeat);

        /// @brief 30 seconds
        if (rte_atomic32_read(&session->heartbeat) >= 10) {
            int ret = 0;
            FRAMEWORK_INFO("close session, because heartbeat is timeout, session_id = %d\n", session->session_id);
            //pthread_mutex_lock(&g_framework.mp_mutex);
            ret = ccdk_framework_free_session(i);
            //pthread_mutex_unlock(&g_framework.mp_mutex);

            if (ret < 0) {
                FRAMEWORK_ERR("ccdk_framework_close_session fail. ret = %d\n", ret);
            }
        }
    }
}


/// @brief ccdk_framework_primary_thread_main
/// primary main thread
/// @param arg
static void *
ccdk_framework_primary_thread_main(void *arg)
{
    struct ccdk_framework *framework = (struct ccdk_framework *)arg;
    FRAMEWORK_INFO("primary thread start running\n");

    while (framework->thread_running) {
        ccdk_framework_primary_check_heartbeat(framework);
        //rte_delay_ms(120000); 为cpu忙等函数，不用
        sleep(3);
    }

    return NULL;
}



static int
ccdk_framework_init_primary(void)
{
    int ret = rte_mp_action_register(CCDK_CFG_MP, ccdk_framework_cfg_mp_primary);

    if (ret < 0) {
        FRAMEWORK_ERR("Couldn't register '%s' action\n", CCDK_CFG_MP);
        return EC_CCDK_INIT_FAIL;
    }

    g_framework.thread_running = 1;
    ret = rte_ctrl_thread_create(&g_framework.thread, "primary", NULL,
                                 ccdk_framework_primary_thread_main, &g_framework);

    if (ret < 0) {
        FRAMEWORK_ERR("Cannot create primary thread, ret = %d\n", ret);
        goto fail;
    }

    return EC_CCDK_OK;
fail:
    rte_mp_action_unregister(CCDK_CFG_MP);
    return EC_CCDK_INIT_FAIL;
}

static void
ccdk_framework_secondary_heartbeat(void)
{
    int i, ret = 0;
    int msg_size = CCDK_SIEZOF_MGNT_MSG + sizeof(struct ccdk_framework_mgnt_heartbeat_msg);
    struct ccdk_framework_mgnt_msg *mgnt_msg = (struct ccdk_framework_mgnt_msg *)ccdk_framework_malloc_msg(
                msg_size);

    if (unlikely(!mgnt_msg)) {
        FRAMEWORK_ERR("ccdk_framework_malloc_msg fail.\n");
        return;
    }

    mgnt_msg->cmd = CCDK_MGNT_CMD_HEARTBEAT;
    mgnt_msg->data_len = sizeof(struct ccdk_framework_mgnt_heartbeat_msg);

    for (i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
        struct ccdk_framework_mgnt_heartbeat_msg *heartbeat = (struct ccdk_framework_mgnt_heartbeat_msg *)
                mgnt_msg->data;
        struct ccdk_framework_session *session = &g_framework.session[i];

        if (rte_atomic32_read(&session->status) == CCDK_USED) {
            heartbeat->session_id[i] = session->session_id;
            FRAMEWORK_DEBUG("ccdk_framework_secondary_heartbeat. session_id = %d\n", session->session_id);
        } else {
            heartbeat->session_id[i] = CCDK_INVALID_SESSION_ID;
        }
    }

    ret = ccdk_framework_handle_sync_msg(MGNT_SERVER_ID, mgnt_msg, 3000);

    if (unlikely(ret < EC_CCDK_OK)) {
        FRAMEWORK_ERR("ccdk_framework_handle_sync_msg fail. ret = %d\n", ret);
    }

    ccdk_framework_free_msg(mgnt_msg);
    FRAMEWORK_DEBUG("ccdk_framework_secondary_heartbeat success.\n");
}

static void *
ccdk_framework_secondary_thread_main(void *arg)
{
    int ret = 0;
    struct ccdk_framework *framework = (struct ccdk_framework *)arg;
    FRAMEWORK_INFO("secondary thread start running\n");
    ret = ccdk_framework_open_session(NULL, NULL);

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("open session fail. ret = %d\n", ret);
        return NULL;
    }

    while (framework->thread_running) {
        sleep(3);
        ccdk_framework_secondary_heartbeat();
    }

    ret = ccdk_framework_close_session();

    if (ret < EC_CCDK_OK) {
        FRAMEWORK_ERR("close session fail. ret = %d\n", ret);
    }

    return NULL;
}


static int
ccdk_framework_init_secondary(void)
{
    int ret = 0;
    g_framework.thread_running = 1;
    ret = rte_ctrl_thread_create(&g_framework.thread, "secondary", NULL,
                                 ccdk_framework_secondary_thread_main, &g_framework);

    if (ret < 0) {
        FRAMEWORK_ERR("Cannot create primary thread, ret = %d\n", ret);
        return EC_CCDK_INIT_FAIL;
    }

    return EC_CCDK_OK;
}

static void *
ccdk_framework_response_thread_main(void *arg)
{
    FRAMEWORK_INFO("response thread start running\n");

    while (g_framework.resp_thread_running) {
        int ret = ccdk_framework_async_wait_response(g_framework.session_epfd, CCDK_MAX_SESSION_NUM, 3000);

        if (ret < EC_CCDK_OK && ret != EC_CCDK_TIMEOUT) {
            FRAMEWORK_ERR("ccdk_framework_wait_response failed, error %i (%s)\n", errno, strerror(errno));
            break;
        }
    }

    return NULL;
}

static int
ccdk_framework_mgnt_cb(void *arg, void *msg)
{
    int ret = EC_CCDK_OK;
    struct ccdk_framework *framework = (struct ccdk_framework *)arg;
    struct ccdk_framework_mgnt_msg *mgnt_msg = (struct ccdk_framework_mgnt_msg *)msg;

    switch (mgnt_msg->cmd) {
    case CCDK_MGNT_CMD_HEARTBEAT: {
        for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
            struct ccdk_framework_mgnt_heartbeat_msg *heartbeat = (struct ccdk_framework_mgnt_heartbeat_msg *)
                    mgnt_msg->data;
            uint32_t session_id = heartbeat->session_id[i];

            if (session_id == CCDK_INVALID_SESSION_ID) {
                continue;
            }

            struct ccdk_framework_session *session = &framework->session[session_id];

            if (rte_atomic32_read(&session->status) == CCDK_USED) {
                rte_atomic32_set(&session->heartbeat, 0);
                FRAMEWORK_DEBUG("session heartbeat clean, session_id = %d\n", session_id);
            }
        }
    }
    break;

    case CCDK_MGNT_CMD_GET_LATENCY: {
        int session_cnt = 0;
        struct ccdk_framework_mgnt_session_latency_msg *session_latency = (struct
                ccdk_framework_mgnt_session_latency_msg *)mgnt_msg->data;

        for (int i = 0; i < CCDK_MAX_SESSION_NUM; i++) {
            ret = ccdk_framework_get_session_latency_info(i, &session_latency->latency[session_cnt]);

            if (ret == EC_CCDK_OK) {
                session_cnt++;
            }
        }

        session_latency->session_cnt = session_cnt;
    }
    break;

    default:
        return 0;
    }

    return 0;
}


int
ccdk_framework_init(void)
{
    int ret = 0;

    if (rte_atomic32_read(&g_framework.already_init)) {
        FRAMEWORK_ERR("framework is already inited\n");
        return EC_CCDK_OK;
    }

    memset(&g_framework, 0, sizeof(struct ccdk_framework));
    g_framework.desc.server_binding_policy = 0;
    g_framework.desc.response_binding_policy = 0;
    pthread_mutex_init(&g_framework.mp_mutex, NULL);
    pthread_mutex_init(&g_framework.mempool_mutex, NULL);

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        ret = ccdk_framework_init_primary();
    } else {
        ret = ccdk_framework_init_secondary();
    }

    if (ret < EC_CCDK_OK) {
        return ret;
    }

    if (g_framework.session_epfd <= 0) {
        int epfd = epoll_create(CCDK_MAX_SESSION_NUM);

        if (g_framework.session_epfd < 0) {
            FRAMEWORK_ERR("Cannot create epoll instance, error %i (%s)\n", errno, strerror(errno));
            goto fail;
        }

        g_framework.session_epfd = epfd;
        FRAMEWORK_DEBUG("session_epfd = %d\n", g_framework.session_epfd);
    }

    g_framework.resp_thread_running = 1;

    if (!g_framework.desc.response_binding_policy) {
        ret = pthread_create(&g_framework.resp_thread, NULL, ccdk_framework_response_thread_main, NULL);
        (void)rte_thread_setname(g_framework.resp_thread, "resp-thread-bm");
    } else {
        ret = rte_ctrl_thread_create(&g_framework.resp_thread, "resp-thread-nb", NULL,
                                     ccdk_framework_response_thread_main, NULL);
    }

    if (ret < 0) {
        FRAMEWORK_ERR("ERROR: Cannot create ccdk session thread!\n");
        goto fail;
    }

    ret = ccdk_framework_register_server("mgnt", MGNT_SERVER_ID, 1, ccdk_framework_mgnt_cb, &g_framework, 0);

    if (ret < 0) {
        FRAMEWORK_ERR("ccdk_framework_register_server fail\n");
        goto fail;
    }

    FRAMEWORK_DEBUG("ccdk framework version : %s\n", CCDK_FRAMEWORK_VERSION);
#ifdef EVENTFD_MODE
    FRAMEWORK_INFO("eventfd mode\n");
#else
    FRAMEWORK_INFO("poll mode\n");
#endif
    rte_atomic32_set(&g_framework.already_init, 1);
    return EC_CCDK_OK;
fail:
    ccdk_framework_cleanup();
    return EC_CCDK_INIT_FAIL;
}

static void
ccdk_framework_cleanup_primary(void)
{
    FRAMEWORK_INFO("exit\n");
    rte_mp_action_unregister(CCDK_CFG_MP);
}


static void
ccdk_framework_cleanup_secondary(void)
{
    FRAMEWORK_INFO("exit\n");
}


void
ccdk_framework_cleanup(void)
{
    g_framework.thread_running = 0;
    g_framework.resp_thread_running = 0;

    if (g_framework.resp_thread > 0) {
        pthread_join(g_framework.resp_thread, NULL);
    }

    if (g_framework.thread > 0) {
        pthread_join(g_framework.thread, NULL);
    }

    if (g_framework.session_epfd > 0) {
        close(g_framework.session_epfd);
        g_framework.session_epfd = 0;
    }

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        ccdk_framework_cleanup_primary();
    } else {
        ccdk_framework_cleanup_secondary();
    }

    ccdk_framework_free_all_server();
    ccdk_framework_free_all_session();

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        ccdk_framework_free_mempool();
    }
}
