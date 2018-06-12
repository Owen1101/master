#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVERPORT      "1988"
#define PAGE_SIZE       16*1024*1024
#define WR_SIZE         512*1024*1024
#define IPSTRSIZE       128
#define BUFSIZE         16
#define FPGAPCIE        "/dev/fpga-pcie"

#define DMA_TYPE            'D'
#define DMA_START_CHAN0     _IO(DMA_TYPE,1)
#define DMA_CHAN_PARM       _IO(DMA_TYPE,2)
#define DMA_STOP_CHAN0      _IO(DMA_TYPE,3)
#define DMA_DISABLE_AD		_IO(DMA_TYPE,4)

static int sfd = -1,j = 0, accetp_sd_bak = -1, send_flag = 1;
static int m_data_size = 0;
static int pause_mark = 0;
static char *m_data_buf = NULL;
static char buf[BUFSIZE];
static pthread_t thrd_accept[128];
static int sd = -1,accept_sd = -1;
static pthread_attr_t attr;

static void *thrd_new_socket(void *accept_sd);
static void *rcv_stop_socket(void *sd);
int main(int argc,char **argv)
{
	int dfd,res,i = 0;
	int err,ret;
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

	while(1){
		accept_sd = accept(sd,(void *)&raddr,&raddr_len);
		if(accept_sd < 0){
			if(errno == EINTR)
				perror("accept()");
			exit(1);
		}

		inet_ntop(AF_INET,&raddr.sin_addr,ipstr,IPSTRSIZE);
		printf("Client:%s:%d\n",ipstr,ntohs(raddr.sin_port));
		
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		err = pthread_create(&thrd_accept[i%64], &attr, thrd_new_socket, &accept_sd);
		if(err){
			perror("pthread_create()");

		}
		if((i%128) > 0){
			;//关闭上一个线程，停止发送数据。
			if(accetp_sd_bak > -1){
				close(accetp_sd_bak);
				accetp_sd_bak = -1;
			}
		}
		accetp_sd_bak = accept_sd;
		i++;
	}

	free(m_data_buf);
	close(sfd);

	exit(0);
}

static void send_job(int sd)
{
	int len = 0, ret = 0;

	ret = send(sd,m_data_buf,m_data_size,0); 
	if(ret < 0)
	{
		perror("send()");
	}
	m_data_size = 0;
	memset(m_data_buf, 0,PAGE_SIZE);

	if(send_flag == 0){
		printf("=====\n");
		pthread_exit(NULL);
	}
}

static void *rcv_stop_socket(void *sd)
{
	int *stop_sd = (int *)sd;
	int ret;
	char buf_stop[1]={0};
	if((ret = recv(*stop_sd,buf_stop,1,0)) < 0)
		perror("recv()");
	memset(m_data_buf, 0,PAGE_SIZE);
	ioctl(sfd,DMA_DISABLE_AD);
	pthread_exit(NULL);
}
static void *thrd_new_socket(void *accept_sd)
{
	int *sd = (int *)accept_sd;
	int ret;
	struct timeval tv;
	pthread_t tid;

	printf("accept_sd = %d\n", *sd);
	if((ret = recv(*sd,buf,BUFSIZE,0)) < 0){
		perror("recv()");
	//	return NULL;
	}
	/*改变DMA寄存器设置*/
	send_flag = 0;

	ioctl(sfd,DMA_CHAN_PARM,buf);
	usleep(1000);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&tid, NULL, rcv_stop_socket, (void *)sd);
	if(ret){
		perror("pthread_create()");
	}

	send_flag = 1;
	while(send_flag){
		ioctl(sfd,DMA_START_CHAN0);//读取通道0的数据
		m_data_size = read(sfd,m_data_buf ,PAGE_SIZE);
		if(m_data_size < 0){
			perror("read()");
			return NULL;
		}
		send_job(*sd);
	}
	return NULL;
}


