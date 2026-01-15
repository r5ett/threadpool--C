#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

typedef struct ThreadPool ThreadPool;/* 线程池结构体的前向声明 */

/* 创建线程池并初始化 */
ThreadPool *threadPoolCreate(int min, int max, int queueSize);

/* 销毁线程池 */
int threadPoolDestroy(ThreadPool* pool);

/* 给线程池添加任务*/
void threadPoolAdd(ThreadPool* pool, void (*func)(void*), void* arg);

/* 获取线程池中工作的线程的个数 */
int threadPoolBusyNum(ThreadPool* pool);

/* 获取线程池中活着的线程的个数 */
int threadPoolAliveNum(ThreadPool* pool);

void* worker(void* arg);/* 工作线程函数 */
void* manager(void* arg);/* 管理者线程函数 */
void threadExit(ThreadPool* pool);/* 线程退出函数 */


#endif