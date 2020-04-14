#include <netinet/in.h>    
#include <sys/types.h>    
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>        
#include <pthread.h>
#include <sys/errno.h>   
#include <unistd.h>   
#include <assert.h>
#include <time.h>
#include <arpa/inet.h>

#define   MYPORT  10005
#define  BACKLOG  200
#define MAXDATASIZE 1024
#define BUFFER_SIZE 100
#define MAC_ADDRLEN 6
#define MAX_RECV_BUFSIZE 256
#define MAXBUFFERSIZE 4096
#define TRUE 1
#define FALSE 0
#define true 1
#define false 0
#define MIN_WAIT_TASK_NUM 10	
#define DEFAULT_THREAD_NUM 10 
const char RecvBuff[BUFFER_SIZE];
const char SendBuff[BUFFER_SIZE];
//
typedef int STATUS;

//3-23

typedef struct tpool_work  
{
	void (*handler_routine)();            //任务函数指针  
	void *arg;                        //任务函数参数 
        void *(*function)(void*);	
	struct tpool_work *next;              //下一个任务链表  
} tpool_work_t;

typedef struct tpool_t
{
	pthread_mutex_t lock;
	pthread_mutex_t thread_conter;
	pthread_cond_t queue_not_empty;
	pthread_cond_t queue_not_full;
	pthread_t *threads;
	pthread_t admin_id;//管理者线程id
	tpool_work_t *task_queue;//任务队列
	//线程池信息
	int min_num_threads;//最小线程数
	int max_num_threads;//最大线程数
	int live_threads;//存活的线程数
	int busy_threads;//正在工作的线程
	int exit_num;//要销毁的线程
	//任务队列信息
	int queue_head;
	int queue_end;
	int queue_size;
	//存在任务数
	int queue_max_size;
	//线程池状态
	int shutdown;
}tpool_t;
/*创建线程池*/
tpool_t *tpool_create(int min_thr_num, int max_thr_num, int queue_max_size);
/*释放线程池*/
int threadpool_free(tpool_t *pool);
/*销毁线程池*/
int threadpool_destroy(tpool_t *pool);
/*管理线程*/
void *admin_thread(void *threadpool);
/*线程是否存在*/
int is_thread_alive(pthread_t tid);
/*工作线程*/
void *threadpool_thread(void *threadpool);
/*向线程池的任务队列中添加一个任务*/
int threadpool_add_task(tpool_t *pool, void *(*function)(void *arg), void *arg);

//创建线程池
tpool_t *tpool_create(int min_num_threads,int max_num_threads,int queue_max_size)
{
	tpool_t *pool = NULL;
	int i;
	do
	{
		if((pool=(tpool_t *)malloc(sizeof(tpool_t)))==NULL)//线程池开辟空间
		{
			return NULL;;
		}
		//初始化线程池信息
		pool->min_num_threads=min_num_threads;
		pool->max_num_threads=max_num_threads;
		pool->live_threads=min_num_threads;
		pool->busy_threads=0;
		pool->exit_num=0;
		pool->queue_head=0;
		pool->queue_end=0;
		pool->queue_size=0;
		pool->queue_max_size=0;
		pool->shutdown=false;
	
		pool->threads=(pthread_t)malloc(sizeof(pthread_t)*max_num_threads);//根据最大线程数，给工作线程数组开辟空间
		if(pool->threads==NULL)
		{
			return NULL;
		}
		memset(pool->threads,0,sizeof(pthread_t)*max_num_threads);
		pool->task_queue=(tpool_work_t*)malloc(sizeof(tpool_work_t)*queue_max_size);//根据最大任务数给队列开辟空间
		if(pool->task_queue==NULL)
		{
			return NULL;
		}
		if(pthread_mutex_init(&(pool->lock),NULL)!=0||pthread_mutex_init(&(pool->thread_conter),NULL)!=0||pthread_cond_init(&(pool->queue_not_empty),NULL)!=0||pthread_cond_init(&(pool->queue_not_full),NULL)!=0)//初始化互斥锁与条件变量
		{
			return NULL;
		}
		for(i=0;i<min_num_threads;i++)//启动最小个数个工作线程
		{
			pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void *)pool);//pool指向当前线程池
		}
		pthread_create(&(pool->admin_id),NULL,admin_thread,(void*)pool);//管理者线程创建
		return pool;
	}while(0);
	pthread_free(pool);//释放线程池所占内存
	return NULL;
}

//线程是否存活
int is_thread_alive(pthread_t tid)
{
	int kill_rc=pthread_kill(tid,0);//发送0号信息
	if(kill_rc==ESRCH)
	{
		return false;
	}
	return true;
}

//工作线程
void *threadpool_thread(void *threadpool)
{
	tpool_t *pool=(tpool_t *)threadpool;
	tpool_work_t task;
	while(true)
	{
		pthread_mutex_lock(&(pool->lock));
		while((pool->queue_size==0)&&(!pool->shutdown))//若无任务则阻塞在任务队列不为空上，有任务则跳出
		{
			pthread_cond_wait(&(pool->queue_not_empty),&(pool->lock));
		
			if(pool->exit_num>0)//判断是否需要清楚线程，自杀功能
			{
				pool->exit_num--;
				if(pool->live_threads>pool->min_num_threads)
				{
					pool->live_threads;
					pthread_mutex_unlock(&(pool->lock));
					pthread_exit(NULL);//结束线程
				}
			}
		}
		//线程池开关状态
		if(pool->shutdown)
		{
			pthread_mutex_unlock(&(pool->lock));
			pthread_exit(NULL);
		}
		task.function=pool->task_queue[pool->queue_head].function;
		task.arg=pool->task_queue[pool->queue_head].arg;
	
		pool->queue_head=(pool->queue_head+1)%pool->queue_max_size;//环形队列
		pool->queue_size;

      		pthread_cond_broadcast(&(pool->queue_not_full));//当任务队列未满时通知可以添加新任务
		pthread_mutex_unlock(&(pool->lock));//释放线程锁
	
		//执行取出的任务
		pthread_mutex_lock(&(pool->thread_conter));//锁住忙线程变量
		pool->busy_threads++;
		pthread_mutex_unlock(&(pool->thread_conter));
		
		(*(task.function))(task.arg);//处理任务

		//任务处理结束
		pthread_mutex_lock(&(pool->thread_conter));
		pool->busy_threads--;
		pthread_mutex_unlock(&(pool->thread_conter));
	}
	pthread_exit(NULL);
}

//任务队列，向任务队列中添加任务
int threadpool_add_task(tpool_t *pool,void*(*function)(void *arg),void *arg)
{
	pthread_mutex_lock(&(pool->lock));
	while((pool->queue_size==pool->queue_max_size)&&(!pool->shutdown))//当任务队列满时，调用wait阻塞
	{
		pthread_cond_wait(&(pool->queue_not_full),&(pool->lock));
	}
	if(pool->shutdown)//若线程池处于关闭状态
	{
		pthread_mutex_unlock(&(pool->lock));
		return -1;
	}
	
	//清空工作线程回调函数的参数
	if(pool->task_queue[pool->queue_end].arg!=NULL)
	{
		free(pool->task_queue[pool->queue_end].arg);
		pool->task_queue[pool->queue_end].arg=NULL;
	}
	
	//添加任务到任务队列
	pool->task_queue[pool->queue_end].function=function;
	pool->task_queue[pool->queue_end].arg=arg;
	pool->queue_end=(pool->queue_end+1)%pool->queue_max_size;
	pool->queue_size++;
	
	//添加任务后，队列不为空，唤醒一个线程池中的线程
	pthread_cond_signal(&(pool->queue_not_empty));
	pthread_mutex_unlock(&(pool->lock));
	
	return 0;
}

//管理者线程
void *admin_thread(void *threadpool)
{
	int i;
	tpool_t *pool=(tpool_t *)threadpool;
	while(!pool->shutdown)
	{
		sleep(1000);
		pthread_mutex_lock(&(pool->lock));
		int queue_size=pool->queue_size;
		int live_threads=live_threads;
		pthread_mutex_unlock(&(pool->lock));

		pthread_mutex_lock(&(pool->lock));
		int busy_threads=busy_threads;
		pthread_mutex_unlock(&(pool->lock));
		
		if(queue_size>/*MIN_WAIT_TASK_NUM*/pool->min_num_threads && live_threads<=pool->max_num_threads)//创建线程 实际任务数量大于最小正在等待的任务数量，存活线程数小于最大线程数
		{
			pthread_mutex_lock(&(pool->lock));
			int add;
			for(i=0;i<pool->max_num_threads;i++)
			{
				if(pool->threads[i]==0 || !is_thread_alive(pool->threads[i]))
				{
					pthread_create(&(pool->admin_id),NULL,threadpool_thread,(void *)pool);
					add++;
					pool->live_threads++;
				}
			}
			pthread_mutex_unlock(&(pool->lock));
		}
		if(live_threads>pool->min_num_threads && busy_threads<live_threads)
		{
			pthread_mutex_lock(&(pool->lock));
			pool->exit_num=DEFAULT_THREAD_NUM;
			pthread_mutex_unlock(&(pool->lock));

			for(i=0;i<DEFAULT_THREAD_NUM;i++)
			{
				pthread_cond_signal(&(pool->queue_not_empty));//通知空闲线程自杀
			}
		}
	}
	return NULL;
}

//释放线程池
int threadpool_free(tpool_t *pool)
{
	if(pool==NULL)
	{
		return -1;
	}
	if(pool->task_queue)
	{
		free(pool->task_queue);
	}
	if(pool->threads)
	{
		free(pool->threads);
		pthread_mutex_lock(&(pool->lock));
		pthread_mutex_destroy(&(pool->lock));
		pthread_mutex_lock(&(pool->thread_conter));
		pthread_mutex_destroy(&(pool->thread_conter));
		pthread_cond_destroy(&(pool->queue_not_empty));
		pthread_cond_destroy(&(pool->queue_not_full));
	}
	free(pool);
	pool=NULL;
	return 0;
}

//销毁线程池
int threadpool_destroy(tpool_t *pool)
{
	int i;
	if(pool==NULL)
	{
		return -1;
	}
	pool->shutdown=true;
	pthread_join(pool->admin_id,NULL);//销毁管理者线程
	for(i=0;i<pool->live_threads;i++)//通知所有线程自杀(在自己领任务的过程)
	{
		pthread_cond_broadcast(&(pool->queue_not_empty));
	}
	for(i=0;i<pool->live_threads;i++)//等待线程结束
	{
		pthread_join(pool->threads[i],NULL);
	}
	threadpool_free(pool);
	return 0;
}

//3-23

//
typedef struct package
{
	char recv_data1[1];
	char recv_data2[2];
	char recv_data3[3];
	char recv_data4[4];
	char recv_data5[5];
	char recv_data6[6];
	char recv_data7[7];
	char recv_data8[8];
	char check_code[9];
}datapackage;

void make_xor_checkcode(char RecvBuff,char *checkcode)
{
	int m=strlen(RecvBuff);
	int i,n;
	char ret;
	char s[m];
	char b[8];
	int x="0x80";
	strcpy(s,RecvBuff);
	for(i=0;i<m-1;i++)
	{       
		if(i==0)
		{       
			ret=s[i]^s[i+1];
		}                
		else    
		{       
			ret=ret^s[i+1];
		}       
	}        
	for(n=0;n<8;n++)
	{       
		if((ret^x)==0)
		{
			b[n]='0';
		}
		else    
		{
			b[n]='1';
		}
		x=x>>1; 
	}        
	b[8]='\0';
	strcpy(checkcode,b);
}

//


/*HASHTABLE*/
typedef struct _NODE
{
	int data;
	struct _NODE* next;
}NODE;
typedef struct _HASH_TABLE
{
	NODE *value[10];
}HASH_TABLE;
HASH_TABLE* create_hash_table()
{
	HASH_TABLE* pHashTbl=(HASH_TABLE*)malloc(sizeof(HASH_TABLE));
	memset(pHashTbl,0,sizeof(HASH_TABLE));
	free(pHashTbl);
	return pHashTbl;
}
NODE* find_data_in_hash(HASH_TABLE* pHashTbl,int data)
{
	NODE* pNode;
	if(NULL == pHashTbl)
	{
		return NULL;
	}
	if(NULL == (pNode=pHashTbl->value[data%10]))
	{
		return NULL;
	}
	while(pNode)
	{
		if(data==pNode->data)
			return pNode;
		pNode=pNode->next;
	}
	return NULL;
}
STATUS insert_data_into_hash(HASH_TABLE* pHashTbl,int data)
{
	NODE* pNode;
	if(NULL==pHashTbl)
	{
		return FALSE;
	}
	if(NULL==pHashTbl->value[data%10])
	{
		pNode=(NODE*)malloc(sizeof(NODE));
		memset(pNode,0,sizeof(NODE));
		pNode->data=data;
		pHashTbl->value[data%10]=pNode;
		free(pHashTbl);
		return TRUE;
	}
	if(NULL!=find_data_in_hash(pHashTbl,data))
	{
		return FALSE;
	}
	pNode=pHashTbl->value[data%10];
	while(NULL!=pNode->next)
	{
		pNode=pNode->next;
	}
	pNode->next=(NODE*)malloc(sizeof(NODE));
	memset(pNode->next,0,sizeof(NODE));
	pNode->next->data=data;
	free(pNode);
	return TRUE;
}
//


//

//12-4
static inline int ____setsockopt(int sock,int timeout)
{
	if(setsockopt(sock,SOL_SOCKET,SO_RCVBUF,&RecvBuff[BUFFER_SIZE],sizeof(RecvBuff))!=0)
	{
		printf("error\n");
		return -1;
	}
	if(setsockopt(sock,SOL_SOCKET,SO_SNDBUF,&SendBuff[BUFFER_SIZE],sizeof(SendBuff))!=0)
	{
		printf("error\n");
		return -1;
	}
	if(timeout)
	{
		struct timeval maxtimeout={3000,0};
		if(setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,(const char*)&maxtimeout,sizeof(maxtimeout))<0)
		{
			printf("error\n");
			return -1;
		}
		if(setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&maxtimeout,sizeof(maxtimeout))<0)
		{
			printf("error\n");
			return -1;
		}
	}
	return 0;
}
int talk_to_client1(struct package datapackage,void *data)
{
	//pthread_detach(pthread_self());
	int new_server_socket = (int)(long)data;
	for(;;)
	{
		long length = recv(new_server_socket, RecvBuff,BUFFER_SIZE, 0);
		//unsigned char crc_data;//1-3
		int i=0;
		int j;//
		printf("\nFrom client,Socket Num:%d,message:%s,length:%lu\n",new_server_socket,RecvBuff,strlen(RecvBuff));
		//crc_data^=length;//1-3
		//_____recv_analysis_message(&length);
		sleep(2);
		if ((strcmp(RecvBuff, "quit") == 0) || (strcmp(RecvBuff, "Quit") == 0))
		{
			strcpy(SendBuff, "Client disconnect!\n");
		}
		else
		{
			strcpy(SendBuff, "Client connect!\n");
		}
		if(send(new_server_socket, SendBuff, strlen(SendBuff), 0)==-1)
		{
			printf("Send error!\n");
		}
		else
		{
			printf("Send to client:%s\n", SendBuff);
		}
		if ((strcmp(RecvBuff, "quit") == 0) || (strcmp(RecvBuff, "Quit") == 0))
		{
			break;
		}
		if(____setsockopt(new_server_socket,1)<0)
		{
			close(new_server_socket);
			return -1;
		}
	}
	HASH_TABLE* hashtable=create_hash_table();
	insert_data_into_hash(hashtable,RecvBuff);
	//
	char strtmp[16];
	char check_str[8];
	memset(RecvBuff,0x0,sizeof(RecvBuff));

	memset(RecvBuff,0x0,1);
	sprintf(RecvBuff,"%-1s",datapackage.recv_data1);
	strcat(RecvBuff,strtmp);

	//memset(strtmp,0x0,2);
	//sprintf(strtmp,"%-2s",datapackage.recv_data2);
	memset(RecvBuff,0x0,2);
	sprintf(RecvBuff,"%-2s",datapackage.recv_data2);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,3);
	sprintf(RecvBuff,"%-3s",datapackage.recv_data3);
	strcat(RecvBuff,strtmp);
	
	memset(RecvBuff,0x0,4);
	sprintf(RecvBuff,"%-4s",datapackage.recv_data4);
	strcat(RecvBuff,strtmp);
	
	memset(RecvBuff,0x0,5);
	sprintf(RecvBuff,"%-5s",datapackage.recv_data5);
	strcat(RecvBuff,strtmp);
	
	memset(RecvBuff,0x0,6);
	sprintf(RecvBuff,"%-6s",datapackage.recv_data6);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,7);
	sprintf(RecvBuff,"%-7s",datapackage.recv_data7);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,8);
	sprintf(RecvBuff,"%-8s",datapackage.recv_data8);
	strcat(RecvBuff,strtmp);

	make_xor_checkcode(RecvBuff,check_str);

	char str[64];
	sprintf(str,"%d",strcmp(datapackage.check_code,check_str));
	if(strcmp(datapackage.check_code,check_str)==0)
	return 1; 
	else
	return 0;
	printf("Thread Exit!");
	pthread_exit(pthread_self());	

}

int talk_to_client2(struct package datapackage,void *data)
{
	//pthread_detach(pthread_self());
	int new_server_socket = (int)(long)data;
	for(;;)
	{
		long length = recv(new_server_socket, RecvBuff,BUFFER_SIZE, 0);
		//unsigned char crc_data;//1-3
		int i=0;
		int j;//
		printf("\nFrom client,Socket Num:%d,message:%s,length:%lu\n",new_server_socket,RecvBuff,strlen(RecvBuff));
		//crc_data^=length;
		//_____recv_analysis_message(&length);
		sleep(2);
		if ((strcmp(RecvBuff, "quit") == 0) || (strcmp(RecvBuff, "Quit") == 0))
		{
			strcpy(SendBuff, "Client disconnect!\n");
		}
		else
		{
			strcpy(SendBuff, "Client connect!\n");
		}
		if(send(new_server_socket, SendBuff, strlen(SendBuff), 0)==-1)
		{
			printf("Send error!\n");
		}
		else
		{
			printf("Send to client:%s\n", SendBuff);
		}
		if ((strcmp(RecvBuff, "quit") == 0) || (strcmp(RecvBuff, "Quit") == 0))
		{
			break;
		}
		if(____setsockopt(new_server_socket,1)<0)
		{
			close(new_server_socket);
			return -1;
		}
	}
	HASH_TABLE* hashtable=create_hash_table();
	insert_data_into_hash(hashtable,RecvBuff);
	//
	
	char strtmp[16];
	char check_str[8];
	memset(RecvBuff,0x0,sizeof(RecvBuff));

	memset(RecvBuff,0x0,1);
	sprintf(RecvBuff,"%-1s",datapackage.recv_data1);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,2);
	sprintf(RecvBuff,"%-2s",datapackage.recv_data2);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,3);
	sprintf(RecvBuff,"%-3s",datapackage.recv_data3);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,4);
	sprintf(RecvBuff,"%-4s",datapackage.recv_data4);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,5);
	sprintf(RecvBuff,"%-5s",datapackage.recv_data5);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,6);
	sprintf(RecvBuff,"%-6s",datapackage.recv_data6);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,7);
	sprintf(RecvBuff,"%-7s",datapackage.recv_data7);
	strcat(RecvBuff,strtmp);

	memset(RecvBuff,0x0,8);
	sprintf(RecvBuff,"%-8s",datapackage.recv_data8);
	strcat(RecvBuff,strtmp);
	
	make_xor_checkcode(RecvBuff,check_str);

	char str[64];
	sprintf(str,"%d",strcmp(datapackage.check_code,check_str));
	if(strcmp(datapackage.check_code,check_str)==0)
		return 1; 
	else
		return 0;
	//
	printf("Thread Exit!");
	pthread_exit(pthread_self());
}

//

int main(int argc, char *argv[])
{
	//
	
	char buf[MAXDATASIZE];
        int numbytes;
        int sock_fd,new_fd,connfd;
	int finish;//3-12	
	struct sockaddr_in my_addr;
	struct sockaddr_in their_addr;
	memset(&my_addr,0,sizeof(my_addr));
	int sin_size;
	my_addr.sin_family =AF_INET;
	my_addr.sin_port =htons(MYPORT);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	sock_fd=socket(AF_INET,SOCK_STREAM,0);
        if(sock_fd==-1)
	{
		printf("Create socket failed!");
		exit(1);
	}
	printf("socket create successful!\n");
	if(bind(sock_fd,(struct sockaddr*)&my_addr,sizeof(struct sockaddr_in)))
	{
		printf("Bind error .IP[%c], Port[%d]\n",INADDR_ANY,my_addr.sin_port);
		exit(1);
	}
	printf("Bind success.IP[%c], Port[%d]\n",INADDR_ANY,my_addr.sin_port);
	if(listen(sock_fd,BACKLOG))
	{
		printf("Listen error!\n");
		exit(1);
	}
	else
	{
		printf("Listening on port[%d]\n",my_addr.sin_port);
	}
	struct sockaddr_in clientAddr;
	int ilength=sizeof(clientAddr);
	int i;
	int client[FD_SETSIZE];
	for(i = 0;i < FD_SETSIZE;i++)
	{
		client[i]=-1;
	}
	int nready = 0;
	int maxfd = sock_fd;
	fd_set rset;
	fd_set allset;
	FD_ZERO(&allset);
	FD_SET(sock_fd,&allset);
	tpool_t *pool;//
	pool=tpool_create(1,1000,2000);//
	for(;;)
	{
		int server_socket=accept(sock_fd,(struct sockaddr*)&clientAddr,&ilength);
		printf("\nNew client touched. Socket Num: %d\n",server_socket);
		void* threadReturn;
		pthread_t *child_thread1,*child_thread2;
		pthread_create(&child_thread1, NULL, talk_to_client1, (void *)(long)server_socket);
		pthread_create(&child_thread2, NULL, talk_to_client2, (void *)(long)server_socket);		
		char num_buff;
		int i;
		
		printf("Create Thread Success!\n");
		
		if(server_socket==-1)
		{       
			printf("Server Accept Failed!");
			break;  
		}
		rset = allset; 
		nready = select(maxfd +1,&rset,NULL,NULL,NULL);
		if(nready < 0)
		{       
			perror("select error!\n");
			exit(1);
		}
		else if(nready == 0)
		{
			threadpool_add_task(&pool,talk_to_client1,1);
			threadpool_add_task(&pool,talk_to_client2,1);
			continue;
		}
		sleep(1);
	}
threadpool_destroy(&pool);
pthread_exit(NULL);
}
