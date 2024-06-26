#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "pool.h"

//任务类型
typedef struct{
	job_t job;
	void *arg;
}threadpool_task_t;

//抽象线程池
typedef struct{
	queue_t *task_queue;
	pthread_t *threads;
	pthread_t admin_tid;
	pthread_mutex_t lock;//队列锁

	pthread_mutex_t busy_lock;//
	pthread_cond_t task_queue_not_full;//任务队列没满
	pthread_cond_t task_queue_not_empty;//任务队列不空

	//线程池属性
	int max_thr_num;
	int min_thr_num;
	int cur_live_thr_num;
	int busy_thr_num;
	int wait_exit_thr_num;

	int queue_size;//当前任务数
	int queue_max_size;//任务队列容纳任务个数

	int shutdown; //1关闭 0 
}threadpool_t;

//任务处理函数
static void *threadpool_job(void *arg)
{
	threadpool_t *pool = arg;
	threadpool_task_t task;

	while(1)
	{
		pthread_mutex_lock(&pool->lock);
		while(pool->queue_size == 0 && pool->shutdown == 0)
		{
			pthread_cond_wait(&pool->task_queue_not_empty, &pool->lock);

			//是否由于线程终止的通知
			if(pool->wait_exit_thr_num > 0)
			{
				pool->wait_exit_thr_num--;
				if(pool->cur_live_thr_num > pool->min_thr_num)
				{
					pool->cur_live_thr_num--;
					pthread_mutex_unlock(&pool->lock);
					pthread_exit(NULL);
				}
			}
		}
			
		//是否是线程池关闭
		if(pool->shutdown)
		{
			pthread_mutex_unlock(&pool->lock);
			pthread_exit(NULL);
		}
		//任务来了
		queue_deq(pool->task_queue, &task);
		pool->queue_size--;
		pthread_cond_broadcast(&pool->task_queue_not_full);
		pthread_mutex_unlock(&pool->lock);

		pthread_mutex_lock(&pool->busy_lock);
		pool->busy_thr_num++;
		pthread_mutex_unlock(&pool->busy_lock);

		//执行任务
		(task.job)(task.arg);
		
		pthread_mutex_lock(&pool->busy_lock);
		pool->busy_thr_num--;
		pthread_mutex_unlock(&pool->busy_lock);
	}
}

//管理者线程
static void * admin_thread(void *arg)
{
	threadpool_t *pool = arg;
	int queue_size, live_thr_num, busy_thr_num, min_thr_num;
	int pos;
	while(!pool->shutdown)
	{
		pthread_mutex_lock(&pool->lock);
		queue_size = pool->queue_size;
		live_thr_num = pool->live_thr_num;
		min_thr_num = pool->min_thr_num;
		pthread_mutex_unlock(&pool->lock);
		
		pthread_mutex_lock(&pool->busy_lock);
		busy_thr_num = pool->busy_thr_num;
		pthread_mutex_unlock(&pool->busy_lock);

		//空线程5
		if(queue_size >= MIN_FREE_THR && pool->cur_live_thr_num < \
				pool->max_thr_num)
		{
			pthread_mutex_lock(&pool->lock);
			
			for(int i = 0, pos = 0;i < DEFAULT_ADD_THR_NUM &&\
					pool->live_thr_num < pool->max_thr_num && \
					pos < pool->max_thr_num; pos++)
			{
				if((pool->threads[pos] != 0 && \
							thread_alive((pool->threads)[pos])))
					continue;
				pthread_create((pool->threads) + pos, NULL, threadpool_job, \
						(void *)pool);
				i++;
				pool->cur_live_thr_num++;
			}
			pthread_mutex_unlock(&pool->lock);
		}

		//销毁多余的线程
		if(busy_thr_num *2 < live_thr_num && live_thr_num > min_thr_num)
		{
			pthread_mutex_lock(&pool->lock);
			pool->wait_exit_thr_num = DEFAULT_DESTROY_NUM;
			pthread_mutex_unlock(&pool->lock);

			for(int i = 0; i < DEFAULT_DESTROY_NUM; i++)
			{
				pthread_cond_signal(&pool->task_queue_not_empty);
			}
		}
		sleep(1);
	}
	pthread_exit((void *)0);
}


threadpool_t *threadpool_init(int queue_max_size, int min_thr_num, int max_thr_num)
{
	threadpool_t *mypool = NULL;
	int err;

	//开辟线程池
	if((mypool = malloc(sizeof(threadpool_t))) == NULL)
	{
		fprintf(stderr, "malloc() failed\n");
		return NULL;
	}

	//初始化成员
	//初始化任务队列
	mypool->queue_max_size = queue_max_size;
	mypool->min_thr_num = min_thr_num;
	mypool->max_thr_num = max_thr_num;

	mypool->task_queue = queue_init(sizeof(threadpool_task_t), queue_max_size);
	//任务线程数组
	mypool->threads = calloc(max_thr_num, sizeof(pthread_t));
	if(NULL == mypool->threads)
	{
		queue_destroy(mypool->task_queue);
		free(mypool);
		return NULL;
	}
	pthread_mutex_init(&mypool->lock);
	pthread_mutex_init(&mypool->busy_lock);
	pthread_cond_init(&mypool->task_queue_not_full);
	pthread_cond_init(&mypool->task_queue_not_empty);

	mypool->cur_live_thr_num = 0;
	mypool->busy_thr_num = 0;
	mypool->wait_exit_thr_num = 0;

	mypool->queue_size = 0;
	mypool->shutdown = 0; 

	//启动min_thr_num线程
	for(int i = 0;i < min_thr_num;i++)
	{
		err = pthread_create(mypool->threads + i, NULL,threadpool_job, (void *)arg);
		if(err) 
		{
			fprintf(stderr, "pthread_create() fail:%s\n", strerror(err));
			queue_destroy(mypool->task_queue);
			free(mypool->threads);
			free(mypool);
			return NULL;
		}
	}

	// 管理者线程
	err = pthread_create(&mypool->admin_tid, NULL, admin_thread, (void *)mypool);
	if(err)
	{
		threadpool_destory(mypool);
		fprintf(stderr, "admin_pthread_create() fail:%s\n", strerror(err));
		queue_destroy(mypool->task_queue);
		free(mypool->threads);
		free(mypool);
	}

	return mypool;
}
//添加任务
int threadpool_add_task(threadpool_t  *pool, job_t job, void *arg)
{
	threadpool_task_t task;
	pthread_mutex_lock(&pool->lock);
	while(queue_isfull(pool->task_queue))
	{
		pthread_cond_wait(&pool->task_queue_not_full, &pool->lock);
	}
	if(pool->shutdown)
	{
		pthread_mutex_unlock(&pool->lock);
		return -1;
	}
	task.job = job;
	task.arg = arg;
	queue_enq(pool->task_queue, &task);
	pool->queue_size++;
	pthread_cond_signal(&pool->task_queue_not_empty);
	pthread_mutex_unlock(&pool->task_queue);

	return 0;
}
//销毁
void threadpool_destory(threadpool_t *pool)
{
	pool->shutdown = 1;

	//管理者收拾
	pthread_join(pool->admin_tid, NULL);

	for(int i = 0;i < pool->cur_live_thr_num; i++)
		pthread_cond_broadcast(&pool->task_queue_not_empty);

	for(int i = 0; i < pool->max_thr_num; i++)
	{
		if((pool->threads)[i] == 0)
			continue ;
		if(thread_alive(pool->threads)[i])
			pthread_join((pool->threads)[i], NULL);
	}
	free(pool->threads);
	queue_destroy(&pool->lock);
	pthread_mutex_destroy(&pool->lock);
	pthread_mutex_destroy(&pool->busy_lock);
	pthread_cond_destroy(&pool->task_queue_not_full);
	pthread_cond_destroy(&pool->task_queue_not_empty);

	free(pool);
	pool = NULL;
}

