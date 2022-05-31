#pragma once

#include "HttpData.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

//Https应该继承自Http,并且重载http的read write函数
class HttpsData:public HttpData{
private:
    SSL* ssl_;
    bool ssl_connected_;
private:
    //覆盖基类的同名函数
    virtual ssize_t hRead(void *buf,size_t nbyte){
        return SSL_read(ssl_,buf,nbyte);
    }
    virtual ssize_t hWrite(const void *buf,size_t nbyte){
        return SSL_write(ssl_,buf,nbyte);
    }
    virtual ssize_t hWriten(std::string &sbuff){

        size_t nleft = sbuff.size();
        ssize_t nwritten = 0;
        ssize_t writeSum = 0;
        const char *ptr = sbuff.c_str();
        while (nleft > 0) {
            if ((nwritten = hWrite(ptr, nleft)) <= 0) {
            if (nwritten < 0) {
                if (errno == EINTR) {
                nwritten = 0;
                continue;
                } else if (errno == EAGAIN)
                break;
                else
                return -1;
            }
            }
            writeSum += nwritten;
            nleft -= nwritten;
            ptr += nwritten;
        }
        if (writeSum == sbuff.size())
            sbuff.clear();
        else
            sbuff = sbuff.substr(writeSum);
        return writeSum;
    }

public:
    HttpsData(int connfd,SSL_CTX * ctx):HttpData(connfd),ssl_(SSL_new(ctx)),ssl_connected_(false){
        SSL_set_fd(ssl_,this->getConnfd());
        //设置ssl非阻塞，需要异步建立SSL连接
        SSL_set_accept_state(ssl_);

        /*当fd是阻塞的时候，同步建立SSL连接
        int ret=SSL_accept(ssl_);
        printf("%d\n",ret);*/
    }
    ~HttpsData(){
        /* 关闭SSL 连接*/
        SSL_shutdown(ssl_);
        /* 释放SSL */
        SSL_free(ssl_);
    }
    
    bool isSslConnected(){ return ssl_connected_;}
    bool sslHandshake(){
        int r=SSL_do_handshake(ssl_);
        if(r==1){
            ssl_connected_=true;
            return true;
        }
        int err = SSL_get_error(ssl_, r);
        //还要继续读或者继续写
        if(err==SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ){
            //printf("SSL_do_handshake return %d error %d errno %d msg %s\n", r, err, errno, strerror(errno));
            if(err==SSL_ERROR_WANT_WRITE) printf("ssl handshake WANT_WRITE\n");
            if(err==SSL_ERROR_WANT_READ) printf("ssl handshake WANT_READ\n");
            return true;
        }else{
            return false;
        }
        return true;
    }

};
