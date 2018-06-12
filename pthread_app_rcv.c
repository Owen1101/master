#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
//#define PAGE_SIZE		32*1024*1024
//#define PAGE_SIZE		4*1024*1024
#define WR_SIZE			512*1024*1024
#define IPSTRSIZE		128
#define BUFSIZE			16
#define FPGAPCIE        "/dev/fpga-pcie"
#define PCIEDATA        "/home/ftp/ftp/pcie/*"

#define DMA_TYPE			'D'
#define DMA_START_CHAN0		_IO(DMA_TYPE,1)
#define DMA_CHAN_PARM		_IO(DMA_TYPE,2)
#define DMA_STOP_CHAN0     	_IO(DMA_TYPE,3)
#define DMA_DISABLE_AD		_IO(DMA_TYPE,4)

static int sfd,j = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;
static int m_hava_data =0;
static int m_data_size = 0;
static int pause_mark = 0;
static char *m_data_buf = NULL;
static char buf[BUFSIZE] = {0x14,0x00,0x04,0x0,0xe1,0x7a,0x14,0x6e,0x14,0x12,0x06,0x04,0x09,0x33,0x18};
/*
static void send_job(int sd)
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
//		printf("send data length=%d\n",ret);
		memset(m_data_buf, 0,PAGE_SIZE);
		pthread_cond_signal(&cnd);
		pthread_mutex_unlock(&mtx);
		printf("----\n");
	}
}

static void recv_job(int sd)
{
	int ret;
	while(1){
		if((ret = recv(sd,buf,BUFSIZE,0)) < 0){
			perror("recv()");
			exit(1);
		}
		if(ret == 0){
			printf("client break down\n");
			exit(0);
		}
		printf("======\n");
		pause_mark = 1;
		ioctl(sfd,DMA_CHAN_PARM,buf);
		usleep(5000);
	}
}

void *thr_send(void *p)
{
	int sd = (int)p;
	send_job(sd);
	pthread_exit(NULL);
}

void *thr_recv(void *p)
{
	int sd = (int)p;
	recv_job(sd);
	pthread_exit(NULL);
}
*/
int main(int argc,char **argv)
{
	int dfd,res,i = 0;
	struct timeval tv;
	glob_t globres;
	time_t stamp;
	struct tm *tm;
	int err,ret;
	char time_name[16];
	char filename[40] = "/home/ftp/ftp/pcie/";
	pthread_t tid_snd,tid_rcv;
	int sd,newsd;
	struct sockaddr_in laddr,raddr;
	socklen_t raddr_len;
	char ipstr[IPSTRSIZE];

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
/*
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
	if((ret = recv(newsd,buf,BUFSIZE,0)) < 0){
		perror("recv()");
		exit(1);
	}
*/
	/*改变DMA寄存器设置*/
	ioctl(sfd,DMA_DISABLE_AD);	
	usleep(1000);
	ioctl(sfd,DMA_STOP_CHAN0);
	usleep(1000);
	ioctl(sfd,DMA_CHAN_PARM,buf);
	//write(sfd,buf,BUFSIZE);
//	usleep(5000);
//	sleep(1);
//授时
	struct tm nowtime;
	time_t st;
	memset(&nowtime, 0, sizeof(struct tm));
/*	nowtime.tm_sec = buf[10];
	nowtime.tm_min = buf[9];
	nowtime.tm_hour = buf[8];
	nowtime.tm_mday = buf[7];
	nowtime.tm_mon = buf[6]-1;
	nowtime.tm_year = 5*buf[4] + buf[5];*/
	nowtime.tm_sec = buf[14];
	nowtime.tm_min = buf[13];
	nowtime.tm_hour = buf[12];
	nowtime.tm_mday = buf[11];
	nowtime.tm_mon = buf[10]-1;
	nowtime.tm_year = 5*buf[8] + buf[9];
	nowtime.tm_isdst = -1;
	st = mktime (&nowtime);
	stime(&st);

	system("date");
/*
	err = pthread_create(&tid_snd,NULL,thr_send,(void *)newsd);
	if(err){
		perror("pthread_create()");
		exit(1);
	}

	err = pthread_create(&tid_rcv,NULL,thr_recv,(void *)newsd);
	if(err){
		perror("pthread_create()");
		exit(1);
	}
*/	
	/*遍历/sata/pcie下所有的文件*/
	for(i = 0; i < globres.gl_pathc; i++){

		dfd = open(globres.gl_pathv[i],O_RDWR|O_TRUNC|O_SYNC);//依次打开每个存盘文件
		if(dfd < 0){
			perror("open()"); 
			exit(1);
		}
//以时间和采样频率命名的文件
		time(&stamp);
		tm = localtime(&stamp);
		sprintf(time_name,"%d%02d%02d%02d%02d%02d%03d",\
				tm->tm_year+1900,tm->tm_mon+1,tm->tm_mday,\
				tm->tm_hour,tm->tm_min,tm->tm_sec,buf[2]);
		strcat(filename,time_name);
//		strcat(filename,"k");

		ret = rename(globres.gl_pathv[i],filename);
		if(ret < 0)
			printf("rename() failed\n");
		strtok(filename,"2");

		gettimeofday(&tv, NULL);
		printf("tv.sec = %ld\n",tv.tv_sec);
		printf("tv.usec = %ld\n",tv.tv_usec);
//存盘
		while(j <= 31){

//			pthread_mutex_lock(&mtx);

//			while(m_hava_data != 0)
//				pthread_cond_wait(&cnd,&mtx);

	/*	gettimeofday(&tv, NULL);
		printf("dmatv.sec = %ld\n",tv.tv_sec);
		printf("dmatv.usec = %ld\n",tv.tv_usec);*/
	//		printf("+++++\n");
			ioctl(sfd,DMA_START_CHAN0);//读取通道0的数据
/*
		gettimeofday(&tv, NULL);
		printf("dmaovertv.sec = %ld\n",tv.tv_sec);
		printf("dmaovertv.usec = %ld\n",tv.tv_usec);*/
	//		printf("-----\n");
			res = read(sfd,m_data_buf ,PAGE_SIZE);
//			res = read(sfd,pmap + PAGE_SIZE*j ,PAGE_SIZE);
			if(res < 0){
				perror("read()");
//				pthread_mutex_unlock(&mtx);
				return -1;
			}
			m_hava_data = 1;
			m_data_size = res;
			write(dfd, m_data_buf, res);//可以放到线程中提高效率
//			pthread_cond_signal(&cnd);
//			pthread_mutex_unlock(&mtx);
//			write(dfd, m_data_buf, res);//可以放到线程中提高效率
			j++;
			//			usleep(500);//视情况而定
		
/*			if(pause_mark == 1){//采样率发生变化
				pause_mark = 0;
				ioctl(sfd,DMA_STOP_CHAN0);
				usleep(5000);
				break;
			}*/
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
//	pthread_join(tid_snd,NULL);
//	pthread_join(tid_rcv,NULL);
	free(m_data_buf);
//	pthread_mutex_destroy(&mtx);
//	pthread_cond_destroy(&cnd);
	close(sfd);
	close(newsd);
	close(sd);

	exit(0);
}
