#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include "logger.h"

#define MAX_THREADS 4
#define MAX_QUEUE 100

// 任务类型
typedef enum {
    TASK_TYPE_DB,
    TASK_TYPE_BROADCAST,
    TASK_TYPE_OTHER
} task_type_t;

// 任务结构体
typedef struct task_s {
    task_type_t type;
    void (*func)(void *);
    void *arg;
    struct task_s *next;
} task_t;

// 线程池结构体
typedef struct {
    pthread_t threads[MAX_THREADS];
    task_t *task_queue;
    task_t *task_queue_tail;
    pthread_mutex_t queue_mutex;
    sem_t semaphore;
    int running;
} thread_pool_t;

static thread_pool_t g_thread_pool = {0};

// 线程函数
static void *thread_function(void *arg) {
    (void)arg;
    
    while (g_thread_pool.running) {
        // 等待任务
        sem_wait(&g_thread_pool.semaphore);
        
        if (!g_thread_pool.running) break;
        
        // 取出任务
        pthread_mutex_lock(&g_thread_pool.queue_mutex);
        task_t *task = g_thread_pool.task_queue;
        if (task) {
            g_thread_pool.task_queue = task->next;
            if (!g_thread_pool.task_queue) {
                g_thread_pool.task_queue_tail = NULL;
            }
        }
        pthread_mutex_unlock(&g_thread_pool.queue_mutex);
        
        if (task) {
            // 执行任务
            task->func(task->arg);
            free(task);
        }
    }
    
    return NULL;
}

// 初始化线程池
int thread_pool_init(void) {
    // 初始化互斥锁
    if (pthread_mutex_init(&g_thread_pool.queue_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize mutex");
        return -1;
    }
    
    // 初始化信号量
    if (sem_init(&g_thread_pool.semaphore, 0, 0) != 0) {
        LOG_ERROR("Failed to initialize semaphore");
        pthread_mutex_destroy(&g_thread_pool.queue_mutex);
        return -1;
    }
    
    g_thread_pool.running = 1;
    g_thread_pool.task_queue = NULL;
    g_thread_pool.task_queue_tail = NULL;
    
    // 创建线程
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&g_thread_pool.threads[i], NULL, thread_function, NULL) != 0) {
                LOG_ERROR("Failed to create thread %d", i);
                g_thread_pool.running = 0;
                for (int j = 0; j < i; j++) {
                    pthread_join(g_thread_pool.threads[j], NULL);
                }
                sem_destroy(&g_thread_pool.semaphore);
                pthread_mutex_destroy(&g_thread_pool.queue_mutex);
                return -1;
            }
    }
    
    LOG_INFO("Thread pool initialized with %d threads", MAX_THREADS);
    return 0;
}

// 添加任务到线程池
int thread_pool_add_task(task_type_t type, void (*func)(void *), void *arg) {
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        LOG_ERROR("Failed to allocate task");
        return -1;
    }
    
    task->type = type;
    task->func = func;
    task->arg = arg;
    task->next = NULL;
    
    pthread_mutex_lock(&g_thread_pool.queue_mutex);
    
    if (!g_thread_pool.task_queue) {
        g_thread_pool.task_queue = task;
        g_thread_pool.task_queue_tail = task;
    } else {
        g_thread_pool.task_queue_tail->next = task;
        g_thread_pool.task_queue_tail = task;
    }
    
    pthread_mutex_unlock(&g_thread_pool.queue_mutex);
    
    // 通知线程
    sem_post(&g_thread_pool.semaphore);
    
    return 0;
}

// 关闭线程池
void thread_pool_shutdown(void) {
    g_thread_pool.running = 0;
    
    // 唤醒所有线程
    for (int i = 0; i < MAX_THREADS; i++) {
        sem_post(&g_thread_pool.semaphore);
    }
    
    // 等待所有线程结束
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(g_thread_pool.threads[i], NULL);
    }
    
    // 清理任务队列
    pthread_mutex_lock(&g_thread_pool.queue_mutex);
    task_t *task = g_thread_pool.task_queue;
    while (task) {
        task_t *next = task->next;
        free(task);
        task = next;
    }
    g_thread_pool.task_queue = NULL;
    g_thread_pool.task_queue_tail = NULL;
    pthread_mutex_unlock(&g_thread_pool.queue_mutex);
    
    // 销毁资源
    sem_destroy(&g_thread_pool.semaphore);
    pthread_mutex_destroy(&g_thread_pool.queue_mutex);
    
    LOG_INFO("Thread pool shutdown");
}
