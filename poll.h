#ifndef __POOL_H__
#define __POOL_H__

#include "queue.h"

#define MIN_FREE_THR         5
#define DEFAULT_ADD_THR_NUM  3
#define DEFAULT_DESTROY_NUM  3

typedef void *(*job_t)(void *arg);

//接口
//创建
threadpool_t *threadpool_init(int queue_max_size, int min_thr_num, int max_thr_num);

//添加任务
int threadpool_add_task(threadpool_t  *pool, job_t job, void *arg);

//销毁
void threadpool_destory(threadpool_t *pool);

#endif


