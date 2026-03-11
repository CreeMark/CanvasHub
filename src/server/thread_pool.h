#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

// 任务类型
typedef enum {
    TASK_TYPE_DB,
    TASK_TYPE_BROADCAST,
    TASK_TYPE_OTHER
} task_type_t;

// 初始化线程池
int thread_pool_init(void);

// 添加任务到线程池
int thread_pool_add_task(task_type_t type, void (*func)(void *), void *arg);

// 关闭线程池
void thread_pool_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_POOL_H
