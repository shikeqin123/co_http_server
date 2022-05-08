#include "HttpData.h"
#include "Utils.h"
#include "co_routine.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

using namespace std;
#define MAXSIZE 1024
#define IPADDRESS "127.0.0.1"
#define SERV_PORT 8888
#define FDSIZE 1024
#define EPOLLEVENTS 20

int setSocketNonBlocking1(int fd) {
  int flag = fcntl(fd, F_GETFL, 0);
  if (flag == -1) return -1;

  flag |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flag) == -1) return -1;
  return 0;
}

int main(){
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, IPADDRESS, &servaddr.sin_addr);
    int fd_server=socket(AF_INET,SOCK_STREAM,0);
    if(bind(fd_server,(sockaddr*)&servaddr,sizeof(servaddr))<0){
        cout<<"socket bind error!"<<endl;
        exit(EXIT_FAILURE);
    }
    if(listen(fd_server,1000)<0){
        cout<<"listen error!"<<endl;
    }

    char buff[1024]="GET /HTTP/1.1\r\nHost: 192.168.52.135\r\nContent-Type: "
                "text/plain\r\nConnection: Keep-Alive\r\n\r\n";
    int sockfd;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == 0) {
         setSocketNonBlocking1(sockfd);
         ssize_t n = write(sockfd, buff, strlen(buff));
         cout<<"write: "<<n<<endl;
    }

    sockaddr_in addr_client;
    int fd_client=accept(fd_server,NULL,NULL);

    HttpData http_data(fd_client);
    if(http_data.isTimeout()){
        cout<<"超时"<<endl;
    }
    http_data.parseRequest();
    cout<<http_data.isKeepAlive()<<endl;
    if(http_data.isResponse()){
        http_data.response();
    }
    char rbuff[1024];
    bzero(rbuff,sizeof(rbuff));
    ssize_t n=read(sockfd,rbuff,sizeof(rbuff));
    cout<<rbuff<<endl;
    if(!http_data.isKeepAlive()||http_data.isTimeout()){
        cout<<"超时或者短连接"<<endl;
        close(fd_client);
    }else{
        cout<<"长连接且未超时"<<endl;
    }
    close(fd_server);
    close(sockfd);
    return 0;
}