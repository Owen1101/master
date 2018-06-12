#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <glob.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

//#include "proto.h"

#define SERVERPORT		"1988"
#define PAGE_SIZE		16*1024*1024
#define WR_SIZE			512*1024*1024
#define IPSTRSIZE		1024

#define FPGAPCIE        "/dev/fpga-pcie"
#define PCIEDATA        "/home/ftp/ftp/pcie/*"

#define DMA_TYPE			'D'
#define DMA_START_CHAN0		_IO(DMA_TYPE,1)
#define DMA_START_CHAN1		_IO(DMA_TYPE,2)
#define DMA_START_CHAN2		_IO(DMA_TYPE,3)
#define DMA_START_CHAN3		_IO(DMA_TYPE,4)

static int sfd,j = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;
//static pthread_mutexattr_t mtx_attr;
static int m_hava_data =0;
static int m_data_size = 0;
//static char *pmap = NULL;	
static char *m_data_buf = NULL;

static void server_job(int sd)
{
	int len = 0, ret = 0;

	while(1){
		pthread_mutex_lock(&mtx);

		while(!m_hava_data){
			pthread_cond_wait(&cnd,&mtx);
			printf("+++++\n");
		}

		if((ret = send(sd,m_data_buf,m_data_size,0)) < 0){
//			if((ret = send(sd,pmap + PAGE_SIZE*j,len,0)) < 0){
			perror("send()");
			pthread_mutex_unlock(&mtx);
			return ;
		}
		m_hava_data = 0;
		m_data_size = 0;
		printf("send data length=%d\n",ret);
		memset(m_data_buf, 0,PAGE_SIZE);
		pthread_cond_signal(&cnd);
		pthread_mutex_unlock(&mtx);
		printf("----\n");
	}
}

void *thr_server(void *p)
{
	int sd,newsd;
	struct sockaddr_in laddr,raddr;
	socklen_t raddr_len;
	char ipstr[IPSTRSIZE];

	sd = socket(AF_INET,SOCK_STREAM,0);
	if(sd < 0){
		perror("socket()");
		exit(1);
	}

	int val = 16*1024*1024;
	if(setsockopt(sd,SOL_SOCKET,SO_SNDBUF,(const char *)&val,sizeof(val)) < 0){
		perror("setsockopt()");
		exit(1);
	}

	laddr.sin_family = AF_INET;
	laddr.sin_port = htons(atoi(SERVERPORT));
	inet_pton(AF_INET,"0.0.0.0",&laddr.sin_addr);

	if(bind(sd,(void *)&laddr,sizeof(laddr)) < 0){
		perror("bind()");
		exit(1);
	}

	if(listen(sd,200) < 0){
		perror("listen()");
		exit(1);
	}

	raddr_len = sizeof(raddr);

	newsd = accept(sd,(void *)&raddr,&raddr_len);
	if(newsd < 0){
		if(errno == EINTR)
			perror("accept()");
		exit(1);
	}

	inet_ntop(AF_INET,&raddr.sin_addr,ipstr,IPSTRSIZE);
	printf("Client:%s:%d\n",ipstr,ntohs(raddr.sin_port));
	server_job(newsd);
	close(newsd);

	close(sd);
	pthread_exit(NULL);
}

int main(int argc,char **argv)
{
	int dfd,res;
	//	FILE *fp;
	int i = 0;
	struct timeval tv;
	glob_t globres;
	time_t stamp;
	struct tm *tm;
	int req = 0,err,ret;
	char time_name[14];
	char filename[40] = "/home/ftp/ftp/pcie/";
	pthread_t tid;

	m_data_buf = malloc(PAGE_SIZE);
	if(NULL == m_data_buf){
		perror("malloc()");
		exit(1);
	}
	sfd = open(FPGAPCIE,O_RDWR);//打开设备文件
	if(sfd < 0){
		perror("open()");
		exit(1);
	}

	err = glob(PCIEDATA,0,NULL,&globres);
	if(err){
		perror("glob()");
		exit(1);
	}

	err = pthread_create(&tid,NULL,thr_server,(void *)i);
	if(err){
		perror("pthread_create()");
		exit(1);
	}


	/*遍历/sata/pcie下所有的文件*/
	for(i = 0; i < globres.gl_pathc; i++){

		dfd = open(globres.gl_pathv[i],O_RDWR|O_TRUNC|O_SYNC);//依次打开每个存盘文件
		if(dfd < 0){
			perror("open()"); 
			exit(1);
		}

		time(&stamp);
		tm = localtime(&stamp);
		sprintf(time_name,"%d%02d%02d%02d%02d%02d",\
				tm->tm_year+1900,tm->tm_mon+1,tm->tm_mday,\
				tm->tm_hour,tm->tm_min,tm->tm_sec);

		strcat(filename,time_name);
		ret = rename(globres.gl_pathv[i],filename);
		if(ret < 0)
			printf("rename() failed\n");
		strtok(filename,"2");

		gettimeofday(&tv, NULL);
		printf("tv.sec = %ld\n",tv.tv_sec);
		printf("tv.usec = %ld\n",tv.tv_usec);

		while(j <= 31){

			pthread_mutex_lock(&mtx);

			while(m_hava_data != 0)
				pthread_cond_wait(&cnd,&mtx);

			ioctl(sfd,DMA_START_CHAN0);//读取通道0的数据

			res = read(sfd,m_data_buf ,PAGE_SIZE);
//			res = read(sfd,pmap + PAGE_SIZE*j ,PAGE_SIZE);
			if(res < 0){
				perror("read()");
				pthread_mutex_unlock(&mtx);
				return -1;
			}
			m_hava_data = 1;
			m_data_size = res;
			write(dfd, m_data_buf, res);//可以放到线程中提高效率
			pthread_cond_signal(&cnd);
			pthread_mutex_unlock(&mtx);
//			write(dfd, m_data_buf, res);//可以放到线程中提高效率
			j++;
			//			usleep(500);//视情况而定
		}		
		j = 0;
		/*
		   gettimeofday(&tv, NULL);
		   printf("tv.sec = %ld\n",tv.tv_sec);
		   printf("tv.usec = %ld\n",tv.tv_usec);
		 */
		close(dfd);
	}

	globfree(&globres);
	pthread_join(tid,NULL);
	free(m_data_buf);
	pthread_mutex_destroy(&mtx);
	pthread_cond_destroy(&cnd);
	close(sfd);

	exit(0);
}
