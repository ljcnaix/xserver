#ifndef XSERVER_THREAD_POOL_H_
#define XSERVER_THREAD_POOL_H_

#include <pthread.h>

typedef void* (*callback_function)(void *);
/*
 * each job corresponding to each request
 *
 */
typedef struct _xserver_job {
        callback_function       callback;
        void                    *args;
        struct _xserver_job     *next;
} xserver_job;

typedef struct _xsever_threadpool {
        int                     thread_number;
        int                     queue_max_job_num;
        int                     queue_cur_job_num;
        
        xserver_job             *queue_head;
        xserver_job             *queue_tail;

        pthread_t               *pthreads;
        pthread_mutex_t         mutex;
        pthread_cond_t          queue_empty;
        pthread_cond_t          queue_not_empty;
        pthread_cond_t          queue_not_full;

        int                     queue_close;
        int                     pool_close;

} xserver_threadpool;

/*---------------------------------------------------------------------------*
        函数名: threadpool_init
        描述:   initialize threadpool.
        
        input:  thread_number,          线程池开启的线程个数
                queue_max_job_num,      任务队列的最大任务数

        output: None

        return: success, 线程池地址
                failure, NULL
 *---------------------------------------------------------------------------*/
xserver_threadpool *
threadpool_init(int thread_number, int queue_max_job_num);

/*---------------------------------------------------------------------------*
        函数名: threadpool_add_job
        描述:   add a job into threadpool's job queue.
        
        input:  pool,                   线程池地址
                callback,               回调函数
                args,                   回调函数参数

        output: None

        return: success, 0
                failure, -1
 *---------------------------------------------------------------------------*/
int
threadpool_add_job(
                xserver_threadpool *pool,
                callback_function callback,
                void *args);

/*---------------------------------------------------------------------------*
        函数名: threadpool_worker
        描述:   thread worker function for doing jobs.
        
        input:  pool,                   线程池地址

        output: None

        return: None
 *---------------------------------------------------------------------------*/
void *
threadpool_worker(void *threadpool);

/*---------------------------------------------------------------------------*
        函数名: threadpool_add_job
        描述:   add a job into threadpool's job queue.
        
        input:  pool,                   线程池地址

        output: None

        return: success, 0
                failure, -1
 *---------------------------------------------------------------------------*/
int
threadpool_destroy(xserver_threadpool *pool);

#endif
