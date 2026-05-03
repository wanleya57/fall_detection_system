/**
 * rtthread_mock.c - RT-Thread API 模拟实现 (Linux/POSIX)
 */
#include "rtthread_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

/* ---- 全局 Tick ---- */
static uint32_t g_start_sec = 0;

static void tick_init(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    g_start_sec = tv.tv_sec;
}

rt_tick_t rt_tick_get(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (rt_tick_t)((tv.tv_sec - g_start_sec) * 1000 + tv.tv_usec / 1000);
}

rt_tick_t rt_tick_from_millisecond(int32_t ms)
{
    return (rt_tick_t)ms;
}

/* ---- 内存 ---- */
void *rt_malloc(int32_t size)
{
    return malloc(size);
}

void *rt_malloc_align(int32_t size, int32_t align)
{
    (void)align;
    return malloc(size);
}

void rt_free(void *ptr)
{
    if (ptr) free(ptr);
}

uint32_t rt_memory_remaining(void)
{
    return 128 * 1024 * 1024;
}

/* ---- 线程 (使用 pthread) ---- */
typedef struct {
    char name[32];
    thread_entry_t entry;
    void *parameter;
    int stack_size;
    int priority;
    pthread_t tid;
    volatile int running;
} mock_thread_t;

static void *thread_proc(void *param)
{
    mock_thread_t *t = (mock_thread_t *)param;
    t->entry(t->parameter);
    t->running = 0;
    return NULL;
}

rt_thread_t rt_thread_create(const char *name,
                              thread_entry_t entry,
                              void *parameter,
                              int stack_size,
                              int priority,
                              int tick)
{
    (void)stack_size; (void)priority; (void)tick;

    mock_thread_t *t = (mock_thread_t *)malloc(sizeof(mock_thread_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    strncpy(t->name, name ? name : "unnamed", sizeof(t->name) - 1);
    t->entry = entry;
    t->parameter = parameter;
    t->running = 1;

    if (pthread_create(&t->tid, NULL, thread_proc, t) != 0) {
        free(t);
        return NULL;
    }

    pthread_detach(t->tid);
    printf("[THREAD] Created: %s (tid=%lu)\n", t->name, (unsigned long)t->tid);
    return (rt_thread_t)t;
}

void rt_thread_startup(rt_thread_t thread)
{
    (void)thread;
}

void rt_thread_millisecond_sleep(uint32_t ms)
{
    usleep(ms * 1000);
}

/* ---- 信号量 (使用 POSIX semaphore) ---- */
typedef struct {
    sem_t sem;
    char name[32];
} mock_sem_t;

rt_sem_t rt_sem_create(const char *name, int32_t init_value, uint8_t flag)
{
    (void)flag;
    mock_sem_t *s = (mock_sem_t *)malloc(sizeof(mock_sem_t));
    if (!s) return NULL;

    strncpy(s->name, name ? name : "unnamed", sizeof(s->name) - 1);
    sem_init(&s->sem, 0, init_value);
    return (rt_sem_t)s;
}

void rt_sem_delete(rt_sem_t sem)
{
    mock_sem_t *s = (mock_sem_t *)sem;
    if (s) {
        sem_destroy(&s->sem);
        free(s);
    }
}

rt_err_t rt_sem_take(rt_sem_t sem, int32_t timeout)
{
    mock_sem_t *s = (mock_sem_t *)sem;
    if (!s) return RT_ERROR;

    if ((uint32_t)timeout == RT_WAITING_FOREVER) {
        sem_wait(&s->sem);
        return RT_EOK;
    } else if (timeout == 0) {
        return (sem_trywait(&s->sem) == 0) ? RT_EOK : RT_EEMPTY;
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (timeout % 1000) * 1000000;
        ts.tv_sec += timeout / 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        return (sem_timedwait(&s->sem, &ts) == 0) ? RT_EOK : RT_ETIMEOUT;
    }
}

rt_err_t rt_sem_release(rt_sem_t sem)
{
    mock_sem_t *s = (mock_sem_t *)sem;
    if (!s) return RT_ERROR;
    sem_post(&s->sem);
    return RT_EOK;
}

/* ---- 互斥锁 (使用 pthread_mutex) ---- */
typedef struct {
    pthread_mutex_t mutex;
    char name[32];
} mock_mutex_t;

rt_mutex_t rt_mutex_create(const char *name, uint8_t flag)
{
    (void)flag;
    mock_mutex_t *m = (mock_mutex_t *)malloc(sizeof(mock_mutex_t));
    if (!m) return NULL;

    strncpy(m->name, name ? name : "unnamed", sizeof(m->name) - 1);
    pthread_mutex_init(&m->mutex, NULL);
    return (rt_mutex_t)m;
}

void rt_mutex_delete(rt_mutex_t mutex)
{
    mock_mutex_t *m = (mock_mutex_t *)mutex;
    if (m) {
        pthread_mutex_destroy(&m->mutex);
        free(m);
    }
}

rt_err_t rt_mutex_take(rt_mutex_t mutex, int32_t timeout)
{
    mock_mutex_t *m = (mock_mutex_t *)mutex;
    if (!m) return RT_ERROR;
    (void)timeout;
    pthread_mutex_lock(&m->mutex);
    return RT_EOK;
}

rt_err_t rt_mutex_release(rt_mutex_t mutex)
{
    mock_mutex_t *m = (mock_mutex_t *)mutex;
    if (!m) return RT_ERROR;
    pthread_mutex_unlock(&m->mutex);
    return RT_EOK;
}

/* ---- 消息队列 (环形缓冲 + 条件变量) ---- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    char *buf;
    int32_t msg_size;
    int32_t max_msgs;
    int32_t head;
    int32_t tail;
    int32_t count;
    char name[32];
} mock_mq_t;

rt_mq_t rt_mq_create(const char *name, int32_t msg_size, int32_t max_msgs, uint8_t flag)
{
    (void)flag;
    mock_mq_t *mq = (mock_mq_t *)malloc(sizeof(mock_mq_t));
    if (!mq) return NULL;

    memset(mq, 0, sizeof(*mq));
    strncpy(mq->name, name ? name : "unnamed", sizeof(mq->name) - 1);
    mq->msg_size = msg_size;
    mq->max_msgs = max_msgs;
    mq->buf = (char *)malloc(msg_size * max_msgs);
    pthread_mutex_init(&mq->lock, NULL);
    pthread_cond_init(&mq->not_empty, NULL);
    pthread_cond_init(&mq->not_full, NULL);

    return (rt_mq_t)mq;
}

void rt_mq_delete(rt_mq_t handle)
{
    mock_mq_t *mq = (mock_mq_t *)handle;
    if (mq) {
        pthread_mutex_destroy(&mq->lock);
        pthread_cond_destroy(&mq->not_empty);
        pthread_cond_destroy(&mq->not_full);
        free(mq->buf);
        free(mq);
    }
}

rt_err_t rt_mq_send(rt_mq_t handle, const void *buffer, int32_t size)
{
    mock_mq_t *mq = (mock_mq_t *)handle;
    if (!mq || !buffer) return RT_ERROR;

    pthread_mutex_lock(&mq->lock);

    if (mq->count >= mq->max_msgs) {
        pthread_mutex_unlock(&mq->lock);
        return RT_EFULL;
    }

    char *dst = mq->buf + (mq->tail * mq->msg_size);
    int32_t copy_size = (size < mq->msg_size) ? size : mq->msg_size;
    memcpy(dst, buffer, copy_size);
    mq->tail = (mq->tail + 1) % mq->max_msgs;
    mq->count++;

    pthread_cond_signal(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);

    return RT_EOK;
}

rt_err_t rt_mq_recv(rt_mq_t handle, void *buffer, int32_t size, int32_t timeout)
{
    mock_mq_t *mq = (mock_mq_t *)handle;
    if (!mq || !buffer) return RT_ERROR;

    pthread_mutex_lock(&mq->lock);

    while (mq->count == 0) {
        if (timeout == 0) {
            pthread_mutex_unlock(&mq->lock);
            return RT_EEMPTY;
        } else if ((uint32_t)timeout == RT_WAITING_FOREVER) {
            pthread_cond_wait(&mq->not_empty, &mq->lock);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (timeout % 1000) * 1000000;
            ts.tv_sec += timeout / 1000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            int ret = pthread_cond_timedwait(&mq->not_empty, &mq->lock, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&mq->lock);
                return RT_ETIMEOUT;
            }
        }
    }

    char *src = mq->buf + (mq->head * mq->msg_size);
    int32_t copy_size = (size < mq->msg_size) ? size : mq->msg_size;
    memcpy(buffer, src, copy_size);
    mq->head = (mq->head + 1) % mq->max_msgs;
    mq->count--;

    pthread_cond_signal(&mq->not_full);
    pthread_mutex_unlock(&mq->lock);

    return RT_EOK;
}

/* ---- 文件系统 ---- */
typedef struct {
    FILE *fp;
} mock_file_t;

rt_file_t rt_fopen(const char *filename, const char *mode)
{
    mock_file_t *f = (mock_file_t *)malloc(sizeof(mock_file_t));
    if (!f) return NULL;

    char std_mode[8] = {0};
    if (strcmp(mode, "r") == 0) strcpy(std_mode, "rb");
    else if (strcmp(mode, "w") == 0) strcpy(std_mode, "wb");
    else if (strcmp(mode, "a") == 0) strcpy(std_mode, "ab");
    else strcpy(std_mode, mode);

    /* 确保目录存在 */
    char dir_path[256];
    strncpy(dir_path, filename, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    char *p = strrchr(dir_path, '/');
    if (p) {
        *p = '\0';
        char tmp[256] = {0};
        char *tok = strtok(dir_path, "/");
        while (tok) {
            if (tmp[0]) strcat(tmp, "/");
            strcat(tmp, tok);
            mkdir(tmp, 0755);
            tok = strtok(NULL, "/");
        }
    }

    f->fp = fopen(filename, std_mode);
    if (!f->fp) {
        free(f);
        return NULL;
    }

    return (rt_file_t)f;
}

void rt_fclose(rt_file_t fp)
{
    mock_file_t *f = (mock_file_t *)fp;
    if (f) {
        if (f->fp) fclose(f->fp);
        free(f);
    }
}

int32_t rt_fread(void *ptr, int32_t size, int32_t count, rt_file_t fp)
{
    mock_file_t *f = (mock_file_t *)fp;
    if (!f || !f->fp) return 0;
    return (int32_t)fread(ptr, size, count, f->fp);
}

int32_t rt_fwrite(const void *ptr, int32_t size, int32_t count, rt_file_t fp)
{
    mock_file_t *f = (mock_file_t *)fp;
    if (!f || !f->fp) return 0;
    return (int32_t)fwrite(ptr, size, count, f->fp);
}

int32_t rt_fseek(rt_file_t fp, int32_t offset, int whence)
{
    mock_file_t *f = (mock_file_t *)fp;
    if (!f || !f->fp) return -1;
    return fseek(f->fp, offset, whence);
}

int32_t rt_ftell(rt_file_t fp)
{
    mock_file_t *f = (mock_file_t *)fp;
    if (!f || !f->fp) return -1;
    return (int32_t)ftell(f->fp);
}

int rt_mkdir(const char *path)
{
    return mkdir(path, 0755);
}

/* ---- 设备 ---- */
void *rt_device_find(const char *name)
{
    (void)name;
    return NULL;
}

/* ---- DFS ---- */
int dfs_mount(const char *device_name, const char *path, const char *filesystemtype,
              unsigned long rwflag, const void *data)
{
    (void)device_name; (void)path; (void)filesystemtype; (void)rwflag; (void)data;
    return 0;
}

/* ---- 控制台 ---- */
void rt_kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

/* ---- LWP (用线程模拟) ---- */
rt_lwp_t lwp_create(const char *path)
{
    (void)path;
    return NULL;
}

void lwp_startup(rt_lwp_t lwp)
{
    (void)lwp;
}

void lwp_free(rt_lwp_t lwp)
{
    (void)lwp;
}

/* ---- 初始化 ---- */
__attribute__((constructor))
static void mock_init(void)
{
    tick_init();
}
