/*
./example_echosvr 127.0.0.1 10000 100 50
*/


#include "co_routine.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <stack>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "HttpsData.h"
#include "Utils.h"
#include <iostream>
#include <openssl/ssl.h>

using namespace std;

SSL_CTX* ctx;

struct task_t
{
	stCoRoutine_t *co;
	int fd;
};

static stack<task_t*> g_readwrite;
static int g_listen_fd = -1;

static void *readwrite_routine( void *arg )
{
	co_enable_hook_sys();

	task_t *co = (task_t*)arg;
	//char buf[ 1024 * 16 ];
	for(;;)
	{
		if( -1 == co->fd )
		{
			g_readwrite.push( co );
			co_yield_ct();
			continue;
		}

		int fd = co->fd;
		co->fd = -1;

        printf("before!\n");
		HttpsData https_data(fd,ctx);
        bool sslRet=false;
        //进行SSL三次握手
        while(!https_data.isSslConnected()){
            struct pollfd pf = { 0 };
		    pf.fd = fd;
            ///等待读写事件
		    pf.events = (POLLIN|POLLIN|POLLERR|POLLHUP);
		    co_poll( co_get_epoll_ct(),&pf,1,1000);
            sslRet=https_data.sslHandshake();
            if(!sslRet){
                break;
            }
        }
        if(!sslRet) continue;
        printf("after!\n");
		struct pollfd pf = { 0 };
		pf.fd = fd;
		pf.events = (POLLIN|POLLERR|POLLHUP);
		co_poll( co_get_epoll_ct(),&pf,1,1000);

		for(;;)
		{	
			cout<<endl<<co_self()<<" readwrite "<<getTimeNow()<<endl;
			if(https_data.isTimeout()){
                cout<<"timeout 0"<<endl;
				break;
			}
			https_data.parseRequest();
			if(https_data.isResponse()){
				https_data.response();
				if(!https_data.isResponse()&&!https_data.isError()) cout<<"responsed"<<endl;
			}
			if(https_data.isError()||(!https_data.isKeepAlive()&&!https_data.isParseRq())||https_data.isTimeout()){
				if(https_data.isError()) cout<<"https error!"<<endl;
                if(https_data.isTimeout()) cout<<"timeout 1"<<endl;
                break;
			}
			co_poll( co_get_epoll_ct(),&pf,1,1000);
		}
	}
	return 0;
}
int co_accept(int fd, struct sockaddr *addr, socklen_t *len );
static void *accept_routine( void * )
{
	co_enable_hook_sys();
	printf("accept_routine\n");
	fflush(stdout);
	for(;;)
	{
		//printf("pid %ld g_readwrite.size %ld\n",getpid(),g_readwrite.size());
		if( g_readwrite.empty() )
		{
			//printf("empty\n"); //sleep
			struct pollfd pf = { 0 };
			pf.fd = -1;
			poll( &pf,1,1000);

			continue;

		}
		//cout<<co_self<<" accept "<<getTimeNow()<<endl;
		struct sockaddr_in addr; //maybe sockaddr_un;
		memset( &addr,0,sizeof(addr) );
		socklen_t len = sizeof(addr);

		int fd = co_accept(g_listen_fd, (struct sockaddr *)&addr, &len);
		if( fd < 0 )
		{
			struct pollfd pf = { 0 };
			pf.fd = g_listen_fd;
			pf.events = (POLLIN|POLLERR|POLLHUP);
			co_poll( co_get_epoll_ct(),&pf,1,1000 );
			continue;
		}
		if( g_readwrite.empty() )
		{
			close( fd );
			continue;
		}
		SetNonBlock( fd );
		task_t *co = g_readwrite.top();
		co->fd = fd;
		g_readwrite.pop();
		//std::cout<<" stack size: "<<g_readwrite.size()<<std::endl;
		co_resume( co->co );
	}
	return 0;
}

static void SetAddr(const char *pszIP,const unsigned short shPort,struct sockaddr_in &addr)
{
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(shPort);
	int nIP = 0;
	if( !pszIP || '\0' == *pszIP   
	    || 0 == strcmp(pszIP,"0") || 0 == strcmp(pszIP,"0.0.0.0") 
		|| 0 == strcmp(pszIP,"*") 
	  )
	{
		nIP = htonl(INADDR_ANY);
	}
	else
	{
		nIP = inet_addr(pszIP);
	}
	addr.sin_addr.s_addr = nIP;

}

static int CreateTcpSocket(const unsigned short shPort /* = 0 */,const char *pszIP /* = "*" */,bool bReuse /* = false */)
{
	int fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
	if( fd >= 0 )
	{
		if(shPort != 0)
		{
			if(bReuse)
			{
				int nReuseAddr = 1;
				setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&nReuseAddr,sizeof(nReuseAddr));
			}
			struct sockaddr_in addr ;
			SetAddr(pszIP,shPort,addr);
			int ret = bind(fd,(struct sockaddr*)&addr,sizeof(addr));
			if( ret != 0)
			{
				close(fd);
				return -1;
			}
		}
	}
	return fd;
}


int main(int argc,char *argv[])
{
	if(argc<5){
		printf("Usage:\n"
               "example_echosvr [IP] [PORT] [TASK_COUNT] [PROCESS_COUNT]\n"
               "example_echosvr [IP] [PORT] [TASK_COUNT] [PROCESS_COUNT] -d   # daemonize mode\n");
		return -1;
	}
	const char *ip = argv[1];
	int port = atoi( argv[2] );
	int cnt = atoi( argv[3] );
	int proccnt = atoi( argv[4] );
	bool deamonize = argc >= 6 && strcmp(argv[5], "-d") == 0;


    char cacert[]="/home/robot-seu/cpp_project/co_http_server/cacert.pem";
    char privkey[]="/home/robot-seu/cpp_project/co_http_server/privkey.pem";
    //printf("started init openssl\n");
    /* SSL 库初始化*/
    SSL_library_init();
    /* 载入所有SSL 算法*/
    OpenSSL_add_all_algorithms();
    /* 载入所有SSL 错误消息*/
    SSL_load_error_strings();
    /* 以SSL V2 和V3 标准兼容方式产生一个SSL_CTX ，即SSL Content Text */
    ctx = SSL_CTX_new(SSLv23_server_method());
    /*
    也可以用SSLv2_server_method() 或SSLv3_server_method() 单独表示V2 或V3标准
    */
    if (ctx == NULL)
    {
        ERR_print_errors_fp(stdout);
        exit(1);
    }
    //printf("1\n");
    /* 载入用户的数字证书， 此证书用来发送给客户端。证书里包含有公钥*/
    if (SSL_CTX_use_certificate_file(ctx, cacert, SSL_FILETYPE_PEM) <= 0)
    {
        //ERR_print_errors_fp(stdout);
        printf("未载入用户数字证书\n");
        exit(1);
    }
    //printf("2\n");
    /* 载入用户私钥*/
    if (SSL_CTX_use_PrivateKey_file(ctx, privkey, SSL_FILETYPE_PEM) <= 0)
    {
        //ERR_print_errors_fp(stdout);
        printf("未载入私钥\n");
        exit(1);
    }
    //printf("3\n");
    /* 检查用户私钥是否正确*/
    if (!SSL_CTX_check_private_key(ctx))
    {
        //ERR_print_errors_fp(stdout);
        printf("私钥错误\n");
        exit(1);
    }
    //printf("end init\n");


	g_listen_fd = CreateTcpSocket( port,ip,true );
	listen( g_listen_fd,1024 );
	if(g_listen_fd==-1){
		printf("Port %d is in use\n", port);
		return -1;
	}
	printf("listen %d %s:%d\n",g_listen_fd,ip,port);

	SetNonBlock( g_listen_fd );

	for(int k=0;k<proccnt;k++)
	{

		pid_t pid = fork();
		if( pid > 0 )
		{
			continue;
		}
		else if( pid < 0 )
		{
			break;
		}
		for(int i=0;i<cnt;i++)
		{
			task_t * task = (task_t*)calloc( 1,sizeof(task_t) );
			task->fd = -1;

			co_create( &(task->co),NULL,readwrite_routine,task );
			co_resume( task->co );

		}
		stCoRoutine_t *accept_co = NULL;
		co_create( &accept_co,NULL,accept_routine,0 );
		co_resume( accept_co );

		co_eventloop( co_get_epoll_ct(),0,0 );

		exit(0);
	}
	if(!deamonize) wait(NULL);
	return 0;
}

