#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>



#define LL_ADD(item,list)\
 do{\
	 item->next = list;\
	 list = item;\
 } while(0)
	 
 //以边界的条件来设值
#define LL_ROMOVE(item,list)\
do{\
   if (item->pre)  item->pre->next = item->next;\
   if (item->next) item->next->pre = item->pre;\
   if (item==list) list = item->next;\
} while(0)

#define for_each_entity(temp,header,type) for(temp=(type*)header;temp!=NULL;temp==temp->next)

#define ZERO_BUF(buf,size) do{memset(buf,0,size);}while(0)	
	
typedef void(*Task_Proc)(void* arg);	


struct O_worker_;
struct O_task_;


	
//建立
typedef struct O_Worker_Context_
{
	struct O_worker_ *workers;
	struct O_task_   *tasks;
	int worker_size;
	//由于多个worker共享一个tasks，所以存在加锁 和 条件订阅和发布
	pthread_cond_t  cond_notify_event;
	pthread_mutex_t mtx;
	
}O_Worker_Context;

typedef struct O_worker_
{
	struct O_worker_ *pre,*next;
	
	pthread_t thread_entity;
	int isTerminal;
	O_Worker_Context *ctx;
	
}O_worker;	

typedef struct O_task_
{
	struct O_task_ *pre,*next;
	Task_Proc proc;
	//参数描述
	int argc;
	void** ptr_argvs;
	int* types;
	
}O_task;	
	
void* worker_main(void* arg);
void work_context_createWorkerPool(O_Worker_Context *ctx,int workerNum)
{
	if (workerNum<=0)workerNum=1;
	pthread_cond_t blankcond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t blanmtx =  PTHREAD_MUTEX_INITIALIZER;
	
	memcpy(&ctx->cond_notify_event,&blankcond,sizeof(blankcond));
	memcpy(&ctx->mtx,&blanmtx,sizeof(blanmtx));
	
	int i;
	O_worker *worker;
	for (i=0;i<workerNum;++i)
	{
		worker = (O_worker*)malloc(sizeof(O_worker));
		ZERO_BUF(worker,sizeof(O_worker));
		if (pthread_create(&worker->thread_entity,0,worker_main,(void*)worker)<0)
		{
			printf("create thread failed...msg:%s\n",strerror(errno));
			free(worker);
			break ;
		}
		worker->ctx = ctx;
		LL_ADD(worker,ctx->workers);
	}
}	

void work_context_add_task(O_Worker_Context *ctx,O_task* task)
{
	pthread_mutex_lock(&ctx->mtx);
	LL_ADD(task,ctx->tasks);
	pthread_cond_signal(&ctx->cond_notify_event);
	pthread_mutex_unlock(&ctx->mtx);
}

void worker_shutdown(O_Worker_Context* ctx)
{
	O_worker *worker;
	
	for (worker=ctx->workers;worker!=NULL;worker=worker->next)
	{
	  worker->isTerminal =1;
	  printf("isTerminal=1...\n");
	}
	
	pthread_mutex_lock(&ctx->mtx);
	ctx->tasks =NULL;
	pthread_cond_broadcast(&ctx->cond_notify_event);
	pthread_mutex_unlock(&ctx->mtx);
	
	for (worker=ctx->workers;worker!=NULL;worker=worker->next)
	{
	   printf("f\n");
	   pthread_join(worker->thread_entity,0);
	}
	
	ctx->workers =NULL;
	
}

void* worker_main(void* arg)
{
	O_worker *woker =(O_worker*)arg;
	O_Worker_Context* ctx = woker->ctx;

	while(1)
	{
		pthread_mutex_lock(&ctx->mtx);
		while(ctx->tasks==NULL)
		{
			if (woker->isTerminal)  //因为默认是0 所以不会因为启动的时候 任务数量是空 直接退出 让worker退出
			{
				break ;
			}
			pthread_cond_wait(&ctx->cond_notify_event,&ctx->mtx);// pthread_cond_wait(唤醒条件配置) 信号+锁
		}
		if (woker->isTerminal)
		{
			pthread_mutex_unlock(&ctx->mtx);
			break ;
		}
		
		O_task * task = ctx->tasks;
		if (task!=NULL)
		{
			LL_ROMOVE(task,ctx->tasks);
		}
		pthread_mutex_unlock(&ctx->mtx);
		//执行在外
		if (task==NULL)
		{
			continue;
		}
		task->proc(task);
		usleep(1);
	}
	LL_ROMOVE(woker,ctx->workers);
	free(woker);
	printf("free worker...\n");
	//释放操作 管理： 方法1 在所执行的线程里面 好比this ,方法2 pthread_join(&thread_t,0) thread_t 是在当前进程是唯一 ，统一调用管理
	//printf("释放了线程%u",(unsigned int)pthread_self());
	//pthread_exit(0);
	return (void*)(0);
}

void * worker_monitor_controller(void* arg)
{
	O_Worker_Context *ctx =(O_Worker_Context*) arg;
	int input;
	while(1)
	{
		scanf("%d",&input);
		if (input==1)
		{
			printf("enter ...\n");
			worker_shutdown(arg);
		}
	}
	return (void*)(0);
}

static void proc(void* arg)
{
	O_task* t = (O_task*)arg;
	printf("O_task argc=%d...\n",t->argc);
	
	free(t);
}

static void cli_push_task(O_Worker_Context *ctx,int num)
{
	int i;
	O_task *cli_task;
	for (i=0;i<num ; ++i)
	{
		cli_task= (O_task*) malloc(sizeof(O_task));
		ZERO_BUF(cli_task,sizeof(O_task));
		cli_task->argc =i;
		cli_task->proc = proc;
		work_context_add_task(ctx,cli_task);
	}
	
}

int main()
{
	O_Worker_Context ctx;
	ZERO_BUF(&ctx,sizeof(ctx));
	work_context_createWorkerPool(&ctx,10);

	cli_push_task(&ctx,1000);
	
	//监控程序 所出于的线程一定要 工作线程之后 结束
	pthread_t monitor;
	pthread_create(&monitor,0,worker_monitor_controller,&ctx);
	pthread_join(monitor,0);
	//下面的循环执行不到，因为 pthread_join 会堵塞当前线程 ，除非把 pthread_join(monitor,0); 去掉 用下面的循环替代堵塞
	
	#if 0
	while(1)
	{
		sleep(1);
		printf("all worker destory tick....\n");
	}
	#endif

	return 0;
}

	
	
	

