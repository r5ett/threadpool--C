#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

const int NUMBER = 2;/* 每次管理者线程增加或销毁的线程个数 */

/* 任务队列里的任务结构体 */
typedef struct Task
{
    void (*function)(void* arg);/* 任务函数指针 */
    void* arg;/* 任务参数 */
}Task;

/* 线程池结构体 */
struct ThreadPool
{
    /* 任务队列，也就是多个任务的集合,所以需要一个数组，数组大小在初始化的时候定义 */
    /* 在这个结构体中，把任务队列定义为一个Task类型的指针，这个指针指向数组 */
    Task* taskQ;
    int queueCapacity;  /* 任务队列的容量 */
    int queueSize;      /* 任务队列当前的任务个数，也就是当前存储了多少个元素 */
    int queueFront;     /* 任务队列的队头下标 -> 取数据 */
    int queueRear;      /* 任务队列的队尾下标 -> 放数据 */

    /* 线程池相关：管理者线程，工作线程 */
    pthread_t  managerID;   /* 管理者线程ID */
    pthread_t* threadIDs;   /* 工作线程ID数组 */

    /* 线程池参数 */
    int minNum;        /* 线程池最小线程数 */
    int maxNum;        /* 线程池最大线程数 */
    int busyNum;       /* 线程池中忙的线程个数，工作中的线程个数 */
    int liveNum;       /* 线程池中存活的线程个数 */
    int exitNum;       /* 需要杀死的线程个数 */
    pthread_mutex_t mutexPool; /* 整个的线程池锁，需要对整个队列做同步 */
    pthread_mutex_t mutexBusy; /* 工作线程忙的数量锁，因为busyNum是线程池里经常被访问的成员 */
    int shutdown;      /* 判定当前线程池是否工作，1不干活，0干湖哦，是不是要销毁，销毁1，不销毁0 */
    /* 如果消费者把任务消耗完了，消费者线程要阻塞，用条件变量，
       既需要阻塞生产者有需要阻塞消费者，因为任务队列个数有上限和下限 */
    pthread_cond_t notFull;   /* 任务队列不满 */
    pthread_cond_t notEmpty;  /* 任务队列不空 */
};

ThreadPool *threadPoolCreate(int min, int max, int queueSize)
{
    /* 需要先创建线程池的实例，并且通过地址传递给其他函数，因此要保证这块地址不能被释放 */
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    do
    {
        if (pool == NULL)/* 内存分配失败 */
        {
            printf("malloc threadpool failed...\n");
           break;
        }

        /* 初始化结构体里的成员 */
        pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
        if (pool->threadIDs == NULL)/* 内存分配失败 */
        {
            printf("malloc threadIDs failed...\n");
            break;
        }
        memset(pool->threadIDs, 0, sizeof(pthread_t) * max);/* 初始化线程ID数组，将 threadIDs 指向的内存区域的每个字节都赋值为 0 */
        pool->minNum = min;
        pool->maxNum = max;
        pool->busyNum = 0;
        pool->liveNum = min;/* 刚创建线程池的时候，存活的线程数就是最小线程数 */
        pool->exitNum = 0;
        
        /* 互斥锁/条件变量相关初始化 */
        /* 返回0表示互斥锁/条件变量初始化操作完全成功 */
        if( pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
            pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
            pthread_cond_init(&pool->notFull, NULL) != 0 ||
            pthread_cond_init(&pool->notEmpty, NULL) != 0)
        {
            printf("mutex or condition init failed...\n");
            break;
        }

        /* 任务队列 */
        pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
        if (pool->taskQ == NULL)/* 内存分配失败 */
        {
            printf("malloc taskQ failed...\n");
        }
        pool->queueCapacity = queueSize;/* 任务队列的容量 */
        pool->queueSize = 0;            /* 任务队列当前的任务个数 */
        pool->queueFront = 0;           /* 任务队列的队头下标 */
        pool->queueRear = 0;            /* 任务队列的队尾下标 */
        pool->shutdown = 0;             /* 线程池不销毁 */
        
        /* 创建线程 */
        pthread_create(&pool->managerID, NULL, manager, pool);/* 创建管理者线程 */
        for (int i = 0; i < min; i++)/* 创建工作线程 */
        {
            pthread_create(&pool->threadIDs[i], NULL, worker, pool);
        }
        return pool;/* 创建成功，返回线程池地址 */
    } while (0);
    
    /* 释放资源 */
    if(pool && pool->threadIDs) free(pool->threadIDs);/* 释放线程ID数组 */
    if(pool && pool->taskQ) free(pool->taskQ);/* 释放任务队列 */
    if(pool) free(pool);/* 释放线程池结构体 */

    return NULL;
}

/* 工作线程 */
void* worker(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;/* 强制类型转换 */
    /* 线程进入到任务函数之后，要不停的读任务队列 */
    while(1)
    {
        pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
        /* 任务队列是否为空，阻塞工作线程 */
        while(pool->queueSize == 0 && !pool->shutdown)
        {
            /* 不为空就唤醒，空就阻塞 */
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);/* 阻塞工作线程 */
            
            /* 判断是否需要销毁线程 */
            if(pool->exitNum > 0)
            {
                pool->exitNum--;/* 需要销毁的线程数-1 */
                if(pool->liveNum > pool->minNum)
                {
                    pool->liveNum--;/* 存活的线程数-1 */
                    pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
                    threadExit(pool);/* 线程退出 */
                }
            }
        }

        /* 判断线程池是否被关闭了 */
        if(pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
            threadExit(pool);/* 线程退出 */
        } 

        /* 从任务队列中取出一个任务*/
        Task task;/* 把取出的任务保存到这个task里 */
        task.function = pool->taskQ[pool->queueFront].function;/* 从头部取出任务函数指针 */
        task.arg = pool->taskQ[pool->queueFront].arg;/* 从头部取出任务参数 */
        /* 移动头节点，用环形队列 */
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize--;/* 任务队列的任务个数-1 */

        /* 取出任务后，通知生产者线程可以添加任务了 */
        pthread_cond_signal(&pool->notFull);/* 唤醒一个或多个生产者线程 */
        pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
        
        printf("thread %ld start working...\n", pthread_self());

        pthread_mutex_lock(&pool->mutexBusy);/* 工作线程忙的数量锁 */
        pool->busyNum++;/* 忙的线程数+1 */
        pthread_mutex_unlock(&pool->mutexBusy);/* 工作线程忙的数量锁 */

        /* 执行任务 */
        /* C语言允许将函数指针变量直接当作函数名来使用 */
        task.function(task.arg);/* 用函数指针的方式执行任务函数  */
        //(*task.function)(task.arg);/* 用解引用的方式执行任务函数  */
        free(task.arg);/* 任务参数是动态分配的内存，任务执行完毕后要释放 */
        task.arg = NULL;/* 避免悬空指针 */

        printf("thread %ld end working...\n", pthread_self());

        /* 任务执行完毕后，修改忙的线程数 */
        pthread_mutex_lock(&pool->mutexBusy);/* 工作线程忙的数量锁 */
        pool->busyNum--;/* 忙的线程数-1 */
        pthread_mutex_unlock(&pool->mutexBusy);/* 工作线程忙的数量锁 */
    }

    return NULL;
}

/* 管理者线程 */
/* 它需要不停的去检测当前任务队列里任务的个数，以及工作的线程的个数，根据他们的比例去选择创建线程还是销毁线程 */
void* manager(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;/* 强制类型转换 */
    /* 管理者线程不断地管理线程池 */
    while(!pool->shutdown)/* 线程池关闭的时候停止 */
    {
        /* 每隔3秒钟管理一次线程池 */
        sleep(3);

        /* 取出线程池中的任务数量和当前线程数量 */
        pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
        int queueSize = pool->queueSize;/* 任务数量 */
        int liveNum = pool->liveNum;    /* 存活的线程数量 */
        pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */

        /* 取出忙的线程数量 */
        pthread_mutex_lock(&pool->mutexBusy);/* 枷锁工作线程忙的数量锁 */
        int busyNum = pool->busyNum;    /* 忙的线程数量 */
        pthread_mutex_unlock(&pool->mutexBusy);/* 解锁工作线程忙的数量锁 */

        /* 添加线程 */
        /* 什么时候添加什么时候销毁 */
        /* 先随便写一个 */
        /* 任务的个数 > 存活的线程个数 && 存货的线程个数 < 最大线程数 */
        if(queueSize > liveNum && liveNum < pool->maxNum)/* 也就是线程干不过来了*/
        {
            pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
            int counter = 0;/* 记录创建了多少个新线程 */
            for(int i = 0; i < pool->maxNum && counter < NUMBER 
                && pool->liveNum < pool->maxNum; i++)
            {
                if(pool->threadIDs[i] == 0)/* 表示这个位置的线程没有存储线程ID，可以使用 */
                {
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);/* 创建新的线程 */
                    counter++;
                    pool->liveNum++;/* 存活的线程数+1 */
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
        }



        /* 销毁线程 */
        /* 忙的线程数 * 2 < 存活的线程数 && 存活的线程数 > 最小线程数 */
        if(busyNum * 2 < liveNum && liveNum > pool->minNum)/* 线程太多，闲的线程太多了 */
        {
            pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
            pool->exitNum = NUMBER;/* 每次销毁NUMBER个线程 */
            for(int i = 0; i < NUMBER; i++)
            {
                /* 通知空闲的线程退出 */
                pthread_cond_signal(&pool->notEmpty);/* 唤醒一个或多个工作的线程，但是只有一个能抢到锁，让它有机会退出 */
            }
            pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
        }
    }

    return NULL;
}

/* 线程退出函数 */
void threadExit(ThreadPool* pool)
{
    pthread_t tid = pthread_self();/* 获取当前要退出的线程ID */
    for(int i = 0; i < pool->maxNum; i++)
    {
        if(pool->threadIDs[i] == tid)/* 找到这个线程在数组中的位置 */
        {
            pool->threadIDs[i] = 0;/* 置为0，表示这个位置可以被使用 */
            printf("threadExit() called,  %ld exiting...\n", tid);
            break;
        }
    }
    pthread_exit(NULL);/* 线程退出 */
}

/* 给线程池添加任务 */
void threadPoolAdd(ThreadPool* pool, void (*func)(void*), void* arg)
{
    pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
    /* 任务队列是否满了，阻塞生产者线程 */
    while(pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        /* 任务队列满了，等待notFull条件变量 */
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);/* 阻塞生产者线程 */
    }

    /* 判断线程池是否被关闭了 */
    if(pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
        return;
    }

    /* 添加任务到任务队列 */
    pool->taskQ[pool->queueRear].function = func;/* 放入任务函数指针 */
    pool->taskQ[pool->queueRear].arg = arg;/* 放入任务参数 */
    /* 移动队尾下标，环形队列 */
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;/* 任务队列的任务个数+1 */

    /* 添加完任务后，通知消费者线程可以取任务了 */
    pthread_cond_signal(&pool->notEmpty);/* 唤醒一个或多个工作的线程 */

    pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
}

/* 获取线程池中忙的线程的个数 */
int threadPoolBusyNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexBusy);/* 加锁工作线程忙的数量锁 */
    int busyNum = pool->busyNum;/* 忙的线程数量 */
    pthread_mutex_unlock(&pool->mutexBusy);/* 解锁工作线程忙的数量锁 */
    return busyNum;
}

/* 获取线程池中活着的线程的个数 */
int threadPoolAliveNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexPool);/* 加锁整个线程池 */
    int liveNum = pool->liveNum;/* 存活的线程数量 */
    pthread_mutex_unlock(&pool->mutexPool);/* 解锁整个线程池 */
    return liveNum;
}

/* 销毁线程池 */
int threadPoolDestroy(ThreadPool* pool)
{
    if(pool == NULL)/* 线程池不存在 */
    {
        return -1;
    }

    /* 关闭线程池 */
    pool->shutdown = 1;
    /* 先销毁管理者线程 */
    pthread_join(pool->managerID, NULL);/* 阻塞回收管理者线程 */
    /* 再唤醒阻塞的消费者线程，让它们自己退出 */
    for(int i = 0; i < pool->liveNum; i++)
    {
        pthread_cond_signal(&pool->notEmpty);/* 唤醒后所有子线程就全部被销毁了 */
    }

    /* 释放堆内存 */
    /* 1.任务队列堆内存 */
    if(pool->taskQ)
    {
        free(pool->taskQ);
    }
    /* 2.线程ID数组堆内存 */
    if(pool->threadIDs)
    {
        free(pool->threadIDs);
    }

    /* 销毁互斥锁和条件变量 */
    pthread_mutex_destroy(&pool->mutexPool);/* 销毁整个线程池锁 */
    pthread_mutex_destroy(&pool->mutexBusy);/* 销毁工作线程忙的数量锁 */
    pthread_cond_destroy(&pool->notEmpty); /* 销毁任务队列不空条件变量 */
    pthread_cond_destroy(&pool->notFull);  /* 销毁任务队列不满条件变量 */

    /* 3.pool */
    free(pool);
    pool = NULL;
    return 0;
}