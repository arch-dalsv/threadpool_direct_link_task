#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


#define mymalloc malloc

#define LL_ADD(item, list) do { 	\
	item->pre = NULL;				\
	item->next = list;				\
	list = item;					\
} while(0)

	
#define LL_REMOVE(item,list)\
	do{\
	if (item->pre!=NULL)  item->pre->next = item->next;\
	if (item->next!=NULL) item->next->pre = item->pre;\
	if (item==list) list = item->next ;\
	item->pre = item->next = 0;\
}while(0)

#define BZERO(buffer,size) do{memset(buffer,0,size);}while(0)

struct NJob;

typedef int* Data ;
typedef void(*Proc)(struct NJob* job);


static int cas(int* dest ,int oldValue,int newValue)
{
   if (*dest==oldValue)
   	{
      *dest = newValue;
	  return 1;
    }
    return 0;
}

static int workerdestoryTimes=0;
static int jobdestoryTimes = 0;


   
typedef struct NJob
{
  Data udata;
  Proc procfun;
  int* resultCode;
  int  pargc;
  void*params;
  struct NJob *pre,*next;
}NJob_t;


static void job_free(NJob_t *job)
{
	cas(&jobdestoryTimes,jobdestoryTimes,jobdestoryTimes+1);

    free(job->udata);
	free(job);
	printf("释放了job...\n");
	
}


struct WorkerContext;
struct NWorker;

typedef struct WorkerContext
{
	struct NWorker *worker_pool;
    pthread_cond_t cond;
	pthread_mutex_t  pmtx;
	NJob_t *wait_jobs;

}WorkerContext_t;

typedef struct NWorker
{
   pthread_t thread;
   int isTerminate;
   struct NWorker *pre,*next;
   WorkerContext_t *context;
   char name[30];
   
}NWorker_t;


static void destoryWorker(NWorker_t *worker)
{

}

static void* multi_workersMain(void* arg)
{
    NWorker_t *worker = (NWorker_t*)arg;
	printf("multi_workersMain...\n");
	int awake =0;
	while(1)
	{
	   //加锁
	   pthread_mutex_lock(&worker->context->pmtx);
	   //等任务 等条件/ 等资源 等时间 
       while(worker->context->wait_jobs==NULL)
	   {
	        printf ("wait_jobs==NULL \n");
	        if (worker->isTerminate) 
				{
				  printf("will exit 1\n");
                  break;
				}
			 awake =0;
	         pthread_cond_wait(&worker->context->cond,&worker->context->pmtx);
			 printf("挂起释放... \n");
			 awake = 1;
		}

	    
		 if (worker->isTerminate && awake==1)
       	 {
       	     pthread_mutex_unlock(&worker->context->pmtx);	   	
	       	 break;
       	 }
	   printf("if (worker->isTerminate fffff )... \n");
	   NJob_t * job = worker->context->wait_jobs;
	   LL_REMOVE(job, worker->context->wait_jobs);
	   pthread_mutex_unlock(&worker->context->pmtx);	   	
	    //exe fun
	   if (job->procfun) job->procfun(job);
	  // usleep(1);
	}

     free(worker);
	 printf("worker destory... \n");
	 pthread_exit(0);
	
	//destoryWorker(worker);
	return (void*)0;
}


int createWorkerPool(WorkerContext_t* context,int workNum,const char* workerNames[])
{
   if (workNum<=0)workNum=1;
   if (context==0)
   	{
       printf("error :createWorkerPool context ==null \n ");
	   return -1;
    }
    BZERO(context, sizeof(WorkerContext_t));
    //初始化两个变量 互斥变量(装钥匙的盒子) 和 条件变量
    pthread_cond_t blank_con = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t blank_mtx = PTHREAD_MUTEX_INITIALIZER;

    memcpy(&context->cond,&blank_con,sizeof(blank_con));
	memcpy(&context->pmtx,&blank_mtx,sizeof(blank_mtx));

	int i;
	NWorker_t *temp=0;
	for (i=0;i<workNum;++i)
	{
	   temp = (NWorker_t*)mymalloc(sizeof(NWorker_t));
	   temp->context = context;
	  if (pthread_create(&temp->thread,0,multi_workersMain,temp)<0)
	  {
          printf("create thread indx:%d failed msg :%s\n",i,strerror(errno));		 
		  free(temp);
		  return -1;
	  }
	  LL_ADD(temp,context->worker_pool);
	}

  return 0;
}


void  worker_context_add_job(WorkerContext_t* context,NJob_t* job)
{
    pthread_mutex_lock(&context->pmtx);
    LL_ADD(job, context->wait_jobs);
	pthread_cond_signal(&context->cond) ;
	pthread_mutex_unlock(&context->pmtx);
}

void worker_context_shut_down(WorkerContext_t* context)
{

    
	NWorker_t *worker =0;
	for (worker=context->worker_pool;worker!=NULL;worker=worker->next)
	{
	  worker->isTerminate =1;
	  printf("  worker->isTerminate =1\n");
	}
	
	pthread_mutex_lock(&context->pmtx);
    context->wait_jobs = NULL;  //引用设置null  job有自己的生命 ，完成使命会释放的
	context->worker_pool = NULL;//引用设置null worker早已经获得生命 ，完成使命会释放的
    pthread_cond_broadcast(&context->cond);//isTerminate =1; 唤醒等待线程       回收 收工回家
	printf("pthread_cond_broadcast after...\n");
	pthread_mutex_unlock(&context->pmtx);


}

#define MAX_THEAD_SIZE 5
#define MAX_JOB_SIZE   5

static void custom_function_common(NJob_t * job)
{
    int a = *(int*)job->udata;
	printf("thread id =%lu ,pass data %d\n",pthread_self(),a);
	job_free(job);
}

//监控终端
void* onMonnitorConsole(void* argvs)
{
    int input =0;
	WorkerContext_t * ctx = (WorkerContext_t*)argvs;
   while (1)
   	{
     scanf("%d",&input);
	 if (input==1)
	 	{
		 worker_context_shut_down(ctx);
	   }
   
   }

  return (void*)(0);
}

int main(int argc,const char * argvs)
{
 WorkerContext_t wctx;

 pthread_t controller;
 pthread_create(&controller,0,onMonnitorConsole,&wctx);

 createWorkerPool(&wctx, MAX_THEAD_SIZE,0);

 int i;
 NJob_t * job;
 for (i=0;i<MAX_JOB_SIZE;++i)
 {
    job = (NJob_t*)mymalloc(sizeof(NJob_t));
	BZERO(job, sizeof(NJob_t));
	job->procfun = custom_function_common;
	job->udata =(int*) mymalloc(sizeof(int));
	worker_context_add_job(&wctx,job);
 }

  while(1)
  {
     sleep(5);
	 printf("all worker shutdown now ,tick....\n");
  }
  //pthread_join(controller,0);

 //printf("jobdestoryTimes=%d, workerdestoryTimes=%d",jobdestoryTimes,workerdestoryTimes);
 //getchar();
 return 0; 
}


