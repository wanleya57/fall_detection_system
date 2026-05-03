/**
 * rtthread_mock.h - RT-Thread API 模拟层 (Linux/POSIX)
 * 用于在 WSL/Linux 上编译运行，验证代码逻辑
 */
#ifndef __RTTHREAD_MOCK_H__
#define __RTTHREAD_MOCK_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

/* ---- 基础类型 ---- */
typedef int rt_err_t;
typedef int rt_bool_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_uint32_t;
typedef size_t rt_size_t;
typedef uint32_t rt_tick_t;
typedef void *rt_timer_t;
typedef void *rt_sem_t;
typedef void *rt_mutex_t;
typedef void *rt_mq_t;
typedef void *rt_thread_t;
typedef void *rt_lwp_t;
typedef struct rt_file *rt_file_t;

#define RT_NULL     NULL
#define RT_EOK      0
#define RT_ERROR    -1
#define RT_ETIMEOUT -2
#define RT_EFULL    -3
#define RT_EEMPTY   -4

#define RT_IPC_FLAG_FIFO     0x00
#define RT_IPC_FLAG_PRIO     0x01

#define RT_WAITING_FOREVER   0xFFFFFFFF
#define RT_THREAD_PRIORITY_MAX  32

#define RT_TICK_PER_SECOND   1000

/* ---- 线程 ---- */
typedef void (*thread_entry_t)(void *parameter);

rt_thread_t rt_thread_create(const char *name,
                              thread_entry_t entry,
                              void *parameter,
                              int stack_size,
                              int priority,
                              int tick);

void rt_thread_startup(rt_thread_t thread);
void rt_thread_millisecond_sleep(uint32_t ms);
#define rt_thread_msleep(ms)  rt_thread_millisecond_sleep(ms)

/* ---- 信号量 ---- */
rt_sem_t rt_sem_create(const char *name, int32_t init_value, uint8_t flag);
void rt_sem_delete(rt_sem_t sem);
rt_err_t rt_sem_take(rt_sem_t sem, int32_t timeout);
rt_err_t rt_sem_release(rt_sem_t sem);

/* ---- 互斥锁 ---- */
rt_mutex_t rt_mutex_create(const char *name, uint8_t flag);
void rt_mutex_delete(rt_mutex_t mutex);
rt_err_t rt_mutex_take(rt_mutex_t mutex, int32_t timeout);
rt_err_t rt_mutex_release(rt_mutex_t mutex);

/* ---- 消息队列 ---- */
rt_mq_t rt_mq_create(const char *name, int32_t msg_size, int32_t max_msgs, uint8_t flag);
void rt_mq_delete(rt_mq_t mq);
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, int32_t size);
rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, int32_t size, int32_t timeout);

/* ---- Tick ---- */
rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(int32_t ms);

/* ---- 内存 ---- */
void *rt_malloc(int32_t size);
void *rt_malloc_align(int32_t size, int32_t align);
void rt_free(void *ptr);
uint32_t rt_memory_remaining(void);

/* ---- 文件系统 ---- */
rt_file_t rt_fopen(const char *filename, const char *mode);
void rt_fclose(rt_file_t fp);
int32_t rt_fread(void *ptr, int32_t size, int32_t count, rt_file_t fp);
int32_t rt_fwrite(const void *ptr, int32_t size, int32_t count, rt_file_t fp);
int32_t rt_fseek(rt_file_t fp, int32_t offset, int whence);
int32_t rt_ftell(rt_file_t fp);
int rt_mkdir(const char *path);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ---- 设备 ---- */
void *rt_device_find(const char *name);

/* ---- DFS ---- */
int dfs_mount(const char *device_name, const char *path, const char *filesystemtype,
              unsigned long rwflag, const void *data);

/* ---- 控制台 ---- */
void rt_kprintf(const char *fmt, ...);

/* ---- 字符串 ---- */
#define rt_strncpy(d, s, n) strncpy(d, s, n)
#define rt_strstr(h, n)     strstr(h, n)
#define rt_strchr(s, c)     strchr(s, c)
#define rt_strlen(s)        strlen(s)
#define rt_memcpy(d, s, n)  memcpy(d, s, n)
#define rt_memset(d, v, n)  memset(d, v, n)
#define rt_memmove(d, s, n) memmove(d, s, n)
#define rt_snprintf          snprintf
#define rt_vsnprintf         vsnprintf
#define RT_TICK_FROM_MILLISEC(ms)  rt_tick_from_millisecond(ms)

/* ---- LWP (进程, Linux 下用线程模拟) ---- */
rt_lwp_t lwp_create(const char *path);
void lwp_startup(rt_lwp_t lwp);
void lwp_free(rt_lwp_t lwp);

#endif /* __RTTHREAD_MOCK_H__ */
