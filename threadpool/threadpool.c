#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "threadpool.h"

#define OBJ_INIT(type, number)          (type *)malloc(sizeof(type)*number);\

#define CONDITION_INIT(v)\
        do {\
                if (pthread_cond_init(&(pool->v), NULL) != 0) {\
                        perror("failed to init " #v ": ");\
                        exit(EXIT_FAILURE);\
                }\
        } while(0)

#define CONDITION_DESTROY(v)\
        do {\
                pthread_cond_destroy(&(pool->v));\
        } while(0)

#define RTN_FAILURE                     return -1
#define EXT_FAILURE                     pthread_exit(NULL)

#define CRITICAL_IN(v)\
        do {\
                if (pthread_mutex_lock(&(pool->mutex)) != 0) {\
                        perror("failed to lock the mutex: ");\
                        v;\
                }\
        } while(0)

#define CRITICAL_OUT(v)\
        do {\
                if (pthread_mutex_unlock(&(pool->mutex)) != 0) {\
                        perror("failed to unlock the mutex: ");\
                        v;\
                }\
        } while(0)

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
threadpool_init(int thread_number, int queue_max_job_num) {

        assert(thread_number > 0);
        assert(queue_max_job_num > 0);

        // 创建并初始化线程池
        xserver_threadpool *pool = OBJ_INIT(xserver_threadpool, 1);
        if (pool == NULL) {
                perror("failed to malloc xserver_threadpool: ");
                return NULL;
        }
        pool->thread_number = thread_number;
        pool->queue_max_job_num = queue_max_job_num;
        pool->queue_cur_job_num = 0;
        pool->queue_head = NULL;
        pool->queue_tail = NULL;
        pool->queue_close = 0;
        pool->pool_close = 0;

        if (pthread_mutex_init(&(pool->mutex), NULL) != 0) {
                perror("failed to init mutex: ");
                exit(EXIT_FAILURE);
        }

        CONDITION_INIT(queue_empty);
        CONDITION_INIT(queue_not_full);
        CONDITION_INIT(queue_not_empty);

        // 创建并启动线程
        do {
                pool->pthreads = OBJ_INIT(pthread_t, pool->thread_number);
                if (pool->pthreads == NULL) {
                        perror("failed to malloc pthreads: ");

                        free(pool);
                        pool = NULL;
                        break;
                }

                for (int i = 0; i < pool->thread_number; i++) {
                        pthread_create(&(pool->pthreads[i]), NULL, threadpool_worker, (void *)pool);
                }

        } while(0);

        return pool;
}

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
                void *args) {

        assert(pool != NULL);
        assert(callback != NULL);

        CRITICAL_IN(RTN_FAILURE); 

        // 任务队列已满且任务队列未关闭，则阻塞等待
        while (pool->queue_cur_job_num == pool->queue_max_job_num &&
                !pool->queue_close) {
               pthread_cond_wait(&(pool->queue_not_full), &(pool->mutex)); 
        }

        // 任务队列已关闭，放弃添加任务直接返回
        if (pool->queue_close) {
                pthread_mutex_unlock(&(pool->mutex));
                return -1;
        }

        // 创建任务，并添加到任务队列尾部
        xserver_job *job = OBJ_INIT(xserver_job, 1);
        if (job == NULL) {
                perror("failed to malloc job: ");
                pthread_mutex_unlock(&(pool->mutex));
                return -1;
        }

        job->callback = callback;
        job->args = args;
        job->next = NULL;

        if (pool->queue_cur_job_num == 0) {
                pool->queue_head = pool->queue_tail = job;
        } else {
                pool->queue_tail->next = job;
                pool->queue_tail = job;
        }
        
        pool->queue_cur_job_num++;

        CRITICAL_OUT(RTN_FAILURE); 

        // 添加任务完成，通知一个工作线程来接取任务
        if (pthread_cond_signal(&(pool->queue_not_empty)) != 0) {
                perror("failed to send signal queue_not_empty: ");
        }

        return 0;
}

/*---------------------------------------------------------------------------*
        函数名: threadpool_worker
        描述:   worker thread for doing jobs.
        
        input:  pool,                   线程池地址

        output: None

        return: None
 *---------------------------------------------------------------------------*/
void *
threadpool_worker(void *threadpool) {
        assert(threadpool != NULL);

        xserver_threadpool *pool = (xserver_threadpool *)threadpool;
        while (1) {
                
                CRITICAL_IN(EXT_FAILURE);

                // 任务队列为空且线程池未关闭则阻塞等待
                while (pool->queue_cur_job_num == 0 && !pool->pool_close) {
                        if (pthread_cond_wait(&(pool->queue_not_empty),
                                                &(pool->mutex)) != 0) {
                                perror("failed to wait queue_not_empty: ");
                                pthread_exit(NULL);
                        }
                }

                // 线程池已关闭，工作线程直接退出
                if (pool->pool_close) {
                        pthread_mutex_unlock(&(pool->mutex));
                        pthread_exit(NULL);
                }

                // 从任务队列头部取得任务
                xserver_job *job = pool->queue_head;
                pool->queue_cur_job_num--;

                if (pool->queue_cur_job_num == 0) {
                        pool->queue_head = pool->queue_tail = NULL;
                } else {
                        pool->queue_head = job->next;
                }

                // 任务队列为空，发送信号通知threadpool_destroy函数
                if (pool->queue_cur_job_num == 0) {
                        pthread_cond_signal(&(pool->queue_empty));
                }

                // 任务队列未满，发送信号通知threadpool_add_job函数
                if (pool->queue_cur_job_num == pool->queue_max_job_num - 1) {
                        pthread_cond_broadcast(&(pool->queue_not_full)); 
                }

                CRITICAL_OUT(EXT_FAILURE);

                // 执行任务
                (*(job->callback))(job->args);
                free(job);
                job = NULL;
        }
}

/*---------------------------------------------------------------------------*
        函数名: threadpool_add_job
        描述:   add a job into threadpool's job queue.
        
        input:  pool,                   线程池地址

        output: None

        return: success, 0
                failure, -1
 *---------------------------------------------------------------------------*/
int
threadpool_destroy(xserver_threadpool *pool) {
        assert(pool != NULL);

        CRITICAL_IN(RTN_FAILURE);

        // 线程池已经关闭或正在关闭，直接返回
        if (pool->queue_close || pool->pool_close) {
                pthread_mutex_unlock(&(pool->mutex));
                return -1;
        }

        // 关闭任务队列，等待工作线程将任务队列清空
        pool->queue_close = 1;
        while (pool->queue_cur_job_num != 0) {
                pthread_cond_wait(&(pool->queue_empty), &(pool->mutex));
        }

        // 关闭线程池
        pool->pool_close = 1;

        CRITICAL_OUT(RTN_FAILURE);

        pthread_cond_broadcast(&(pool->queue_not_empty));
        pthread_cond_broadcast(&(pool->queue_not_full));

        // 等待线程池的所有线程执行完毕
        for (int i = 0; i < pool->thread_number; ++i) {
                pthread_join(pool->pthreads[i], NULL);
        }

        pthread_mutex_destroy(&(pool->mutex));
        CONDITION_DESTROY(queue_empty);
        CONDITION_DESTROY(queue_not_full);
        CONDITION_DESTROY(queue_not_empty);

        free(pool->pthreads);

        xserver_job *job;
        while (pool->queue_head != NULL) {
                job = pool->queue_head;        
                pool->queue_head = job->next;
                free(job);
        }

        free(pool);
        return 0;
}
