#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include <map>

#include <pthread.h>
#include <unistd.h>


/* mimetype */
//单例模式
class MimeType{
private:
    MimeType();
    MimeType(const MimeType& m);
    static void init();
private:
    static std::unordered_map<std::string,std::string> mime;
    static pthread_once_t once_control;

public:
    static std::string getMime(const std::string &suffix);
    
};



/* httpdata */
enum ProcessState{
    STATE_PARSE_REQUEST_LINE,
    STATE_PARSE_HEADERS,
    STATE_RECV_BODY,
    STATE_RESPONSE,
    STATE_FINISH,
    STATE_ERROR
};

enum RequestLineState{
    RL_AGAIN,
    RL_ERROR,
    RL_SUCCESS
};

enum HeaderState{
    HEADER_AGAIN,
    HEADER_ERROR,
    HEADER_SUCCESS
};
enum ParseHeaderState{
    H_START,
    H_KEY,
    H_COLON,
    H_SPACES_AFTER_COLON,
    H_VALUE,
    H_CR,
    H_LF,
    H_END_CR,
    H_END_LF,
    H_FINISH
};

enum ResponseState{
    RESPONSE_AGAIN,
    RESPONSE_ERROR,
    RESPONSE_SUCCESS
};

const int MAX_BUFF = 4096;

enum HttpMethod { METHOD_POST , METHOD_GET, METHOD_HEAD };
enum HttpVersion { HTTP_10, HTTP_11 };

//短连接过期时间 ms
const static int  SHORT_CONNECTION_TIME=5*1000;
//长连接过期时间 ms
const static int  KEEPALIVE_TIME=60*1000;



class HttpData{
private:
    int connfd_;
    std::string inBuffer_;
    std::string outBuffer_;

    //处理的状态
    ProcessState proc_state_;
    //暂存解析头部的状态，因为有可能一次read不能读到完整的http请求
    //上一次解析到的状态
    ParseHeaderState parse_header_state_;
    //上一次解析到的位置
    size_t parse_header_pos_;
    
    HttpMethod method_;
    std::string fileName_;
    HttpVersion http_version_;
    std::map<std::string,std::string> headers_;

    bool keep_alive_;
    //the time http expired
    unsigned long long expired_time_;

    
private:
    RequestLineState parseRequestLine();
    HeaderState parseHeaders();
    void handleError(int fd,int err_num, std::string short_msg);

    //输入输出定义为虚函数，方便继承
    virtual ssize_t hRead(void *buf,size_t nbyte);
    virtual ssize_t hWrite(const void *buf,size_t nbyte);
    virtual ssize_t hWriten(std::string &sbuff);

public:
    HttpData(int connfd);
    ~HttpData(){ close(connfd_);};

    int getConnfd(){ return connfd_;}

    ProcessState parseRequest();
    ResponseState response();
    
    //是否还在解析请求
    bool isParseRq();
    bool isResponse();
    bool isError();
    bool isTimeout();
    bool isKeepAlive();

};