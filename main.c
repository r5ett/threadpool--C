#include <stdio.h>
#include "threadpool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

/* 示例任务函数 */
void taskFunc(void* arg)
{
    int num = *((int*)arg);
    printf("thread %ld is working, number = %d\n",
         pthread_self(), num);
    sleep(1);/* 模拟任务执行需要1秒 */
}

int main()
{
    /* 创建线程池 */
    ThreadPool* pool = threadPoolCreate(3, 10, 100);/* 最小线程数3，最大线程数10，任务队列容量100 */
    for(int i = 0; i < 100; i++)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAdd(pool, taskFunc, num);/* 向线程池添加任务 */
    }

    sleep(5);/* 主线程休眠10秒，等待任务完成 */

    threadPoolDestroy(pool);/* 销毁线程池 */

    return 0;
}