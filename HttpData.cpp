#include "co_routine.h"
#include "HttpData.h"
#include "Utils.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>

#include <string>
#include <iostream>

using std::string;

/* mimetype */
pthread_once_t MimeType::once_control=PTHREAD_ONCE_INIT;
std::unordered_map<std::string,std::string> MimeType::mime;

void MimeType::init(){
    mime[".html"] = "text/html";
    mime[".avi"] = "video/x-msvideo";
    mime[".bmp"] = "image/bmp";
    mime[".c"] = "text/plain";
    mime[".doc"] = "application/msword";
    mime[".gif"] = "image/gif";
    mime[".gz"] = "application/x-gzip";
    mime[".htm"] = "text/html";
    mime[".ico"] = "image/x-icon";
    mime[".jpg"] = "image/jpeg";
    mime[".png"] = "image/png";
    mime[".txt"] = "text/plain";
    mime[".mp3"] = "audio/mp3";
    mime["default"] = "text/html";
}

std::string MimeType::getMime(const std::string &suffix){
    pthread_once(&once_control,MimeType::init);
    if(mime.find(suffix)==mime.end()){
        return mime["default"];
    }else{
        return mime[suffix];
    }
}



/* httpdata */

char indexFile[4096];
pthread_once_t once_ctl=PTHREAD_ONCE_INIT;
void bufferInit(){
    struct stat sbuff;
    int stat_ret=stat("index.html",&sbuff);                
    int src_fd = open("index.html", O_RDONLY, 0);
    if (src_fd < 0) {
      std::cout<<"index.html not find!"<<std::endl;
    }
    void *mmapRet = mmap(NULL, sbuff.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    close(src_fd);
    if (mmapRet == (void *)-1) {
      munmap(mmapRet, sbuff.st_size);
    }
    strcpy(indexFile,static_cast<char *>(mmapRet));
    munmap(mmapRet, sbuff.st_size);
}


HttpData::HttpData(int connfd)
                :connfd_(connfd),
                proc_state_(STATE_PARSE_REQUEST_LINE),
                parse_header_state_(H_START),
                parse_header_pos_(0),
                method_(METHOD_GET),
                http_version_(HTTP_11),
                keep_alive_(false),
                expired_time_(0){

}

RequestLineState HttpData::parseRequestLine(){
    size_t prev_pos=0;
    size_t curr_pos=inBuffer_.find('\n');
    //请求行不完整等待下一次socket read获得更多数据
    if(curr_pos==string::npos){
        return RL_AGAIN;
    }
    string rq_line=inBuffer_.substr(0,curr_pos+1);
    inBuffer_=inBuffer_.substr(curr_pos+1);
    
    curr_pos=rq_line.find(' ');
    if(curr_pos==string::npos) return RL_ERROR;
    string method=rq_line.substr(prev_pos,curr_pos-prev_pos);
    prev_pos=curr_pos+1;
    if(method=="GET"){
        method_=METHOD_GET;
    }else if(method=="POST"){
        method_=METHOD_POST;
    }else if(method=="HEAD"){
        method_=METHOD_HEAD;
    }else{
        //fixme:  add other method such as PUT、 PATCH、 OPTIONS、 DELETE
        return RL_ERROR;
    }

    //获取filename
    curr_pos=rq_line.find('/',prev_pos);
    if(curr_pos==string::npos){
        return RL_ERROR;
    }
    prev_pos=curr_pos;

    curr_pos=rq_line.find(' ',prev_pos);
    if(curr_pos==string::npos) return RL_ERROR;
    string file=rq_line.substr(prev_pos,curr_pos-prev_pos);
    prev_pos=curr_pos+1;
    if(file.empty()){
        return RL_ERROR;
    }else if(file=="/"){
        //默认index.html
        fileName_="index.html";
    }else{
        //去掉‘/’
        fileName_=file.substr(1);
    }
    
    curr_pos=rq_line.find('\r',prev_pos);
    if(curr_pos==string::npos){
        return RL_ERROR;
    }
    string version=rq_line.substr(prev_pos,curr_pos-prev_pos);
    if(version=="HTTP/1.0"){
        http_version_=HTTP_10;
    }else if(version=="HTTP/1.1"){
        http_version_=HTTP_11;
    }else{
        //fixme: http0.9 http2 http3
        return RL_ERROR;
    }

    return RL_SUCCESS;
}

//解析key-value部分的头部
//可能存在一次读不完的状态,此时返回HEADER_AGAIN
HeaderState HttpData::parseHeaders(){
    
    string &buffer=inBuffer_;
    ParseHeaderState parse_state=parse_header_state_;
    size_t &parse_pos=parse_header_pos_;
    size_t key_begin=-1,key_end=-1,value_begin=-1,value_end=-1;
    bool finish=false;
    for(int i=parse_pos;i<buffer.size()&&!finish;++i){
        switch (parse_state){

        case H_START:
            key_begin=i;
            parse_state=H_KEY;
            break;

        case H_KEY:
            if(buffer[i]==':'){
                key_end=i;
                parse_state=H_COLON;
            }
            break;

        case H_COLON:
            if(buffer[i]==' '){
                parse_state=H_SPACES_AFTER_COLON;
            }else{
                return HEADER_ERROR;
            }
            break;

        case H_SPACES_AFTER_COLON:
            value_begin=i;
            parse_state=H_VALUE;
            break;

        case H_VALUE:
            if(buffer[i]=='\r'){
                value_end=i;
                parse_state=H_CR;
            }
            break;

        case H_CR:
            if(buffer[i]=='\n'){
                string key=buffer.substr(key_begin,key_end-key_begin);
                string value=buffer.substr(value_begin,value_end-value_begin);
                headers_[key]=value;

                parse_pos=i;
                parse_state=H_LF;
            }else{
                return HEADER_ERROR;
            }
            break;

        case H_LF:
            if(buffer[i]=='\r'){
                parse_state=H_END_CR;
            }else{
                key_begin=i;
                parse_state=H_KEY;
            }
            break;
            
        case H_END_CR:
            if(buffer[i]=='\n'){
                finish=true;
                parse_pos=i;
                parse_state=H_END_LF;
            }else{
                return HEADER_ERROR;
            }
            break;
        }
    }

    buffer=buffer.substr(parse_pos+1);
    parse_pos=0;
    auto last_state=parse_state;
    //每次进入都是开始的状态
    parse_state=H_START;
    if(last_state==H_END_LF){
        return HEADER_SUCCESS;
    }else{
        return HEADER_AGAIN;
    }
}

ProcessState HttpData::parseRequest(){
    co_enable_hook_sys();
    char buff[4096];
    bzero(buff,sizeof(buff));
    ssize_t read_num=read(connfd_,buff,sizeof buff);
    if(read_num<0){
        if(errno==EINTR||errno==EAGAIN){
            //std::cout<<(errno==EINTR?"EINTR":"EAGAIN")<<std::endl;
            return proc_state_;
        }
        //std::cout<<"errno: "<<errno<<std::endl;
        handleError(connfd_, 400, "Bad Request");
        //说明read return 0, 对面断开连接
        proc_state_=STATE_ERROR;
        return proc_state_;
    }else if(read_num==0){
        // 有请求出现但是读不到数据，可能是Request
        // Aborted，或者来自网络的数据没有达到等原因
        // 最可能是对端已经关闭了，统一按照对端已经关闭处理
        //std::cout<<"read_num==0 关闭连接： "<<errno<<std::endl;
        proc_state_=STATE_ERROR;
        return proc_state_;
    }
    else{
        //std::cout<<"read buffer: "<<read_num<<std::endl;
    }
    inBuffer_+=buff;

    while(proc_state_!=STATE_RESPONSE){
        switch (proc_state_){

        case STATE_PARSE_REQUEST_LINE:{
            auto rl_state=parseRequestLine();

            switch (rl_state){
            case RL_ERROR:
                handleError(connfd_,400,"Bad Request");
                proc_state_=STATE_ERROR;
                return  proc_state_;
            case RL_AGAIN:
                return proc_state_;
            case RL_SUCCESS:
                proc_state_=STATE_PARSE_HEADERS;
                break;
            }
            break;
        }
            
        
        case STATE_PARSE_HEADERS:{
            auto hd_state=parseHeaders();

            switch (hd_state){
            case HEADER_ERROR:
                handleError(connfd_,400,"Bad Request");
                proc_state_=STATE_ERROR;
                return proc_state_;
            case HEADER_AGAIN:
                return proc_state_;
            case HEADER_SUCCESS:
                //检查请求是否有body
                if(headers_.count("Content-Length")||headers_.count("Transfer-Encoding")){
                    proc_state_=STATE_RECV_BODY;
                    break;
                }
                proc_state_=STATE_RESPONSE;
                break;
            }
            break;
        }
            
        
        case STATE_RECV_BODY:
            //fixme
            proc_state_=STATE_RESPONSE;
            break;
        case STATE_FINISH:
            proc_state_=STATE_PARSE_REQUEST_LINE;
            break;
        }
    }
    if (headers_.find("Connection") != headers_.end() &&
        (headers_["Connection"] == "Keep-Alive" ||
         headers_["Connection"] == "keep-alive")) {
        keep_alive_ = true;
        expired_time_=getTimeNow()+KEEPALIVE_TIME;
    }else{
        expired_time_=getTimeNow()+SHORT_CONNECTION_TIME;
    }
    return STATE_RESPONSE;
}

ResponseState HttpData::response(){
    //如果没有到处理步骤
    if(proc_state_!=STATE_RESPONSE) return RESPONSE_AGAIN;

    struct stat sbuff;
    int stat_ret=stat(fileName_.c_str(),&sbuff);
    if(stat_ret==-1){
        //文件不存在直接返回404
        outBuffer_.clear();
        handleError(connfd_,404,"Not Found!");
        return RESPONSE_ERROR;
    }

    string header;
    header += "HTTP/1.1 200 OK\r\n";
    if (keep_alive_) {
      header += string("Connection: Keep-Alive\r\n") +
                "Keep-Alive: timeout=" + std::to_string(KEEPALIVE_TIME) +
                "\r\n";
    }

    int dot_pos = fileName_.rfind('.');
    string filetype;
    if (dot_pos ==string::npos)
      filetype = MimeType::getMime("default");
    else
      filetype = MimeType::getMime(fileName_.substr(dot_pos));
    header += "Content-Type: " + filetype + "\r\n";
    header += "Content-Length: " + std::to_string(sbuff.st_size) + "\r\n";
    header += "Server: co_http_server\r\n";
    header += "\r\n";
    outBuffer_ += header;
    if(method_ == METHOD_HEAD){
        size_t ret=writen(connfd_,outBuffer_);
        if(ret<0){
            return RESPONSE_ERROR;
        }
        return RESPONSE_SUCCESS;
    }

/*
    if(fileName_=="index.html"){
        //std::cout<<"default html"<<std::endl;
        pthread_once(&once_ctl,bufferInit);
        outBuffer_ += indexFile;
        size_t ret=writen(connfd_,outBuffer_);
        if(ret<0){
            return RESPONSE_ERROR;
        }
        proc_state_=STATE_FINISH;
        return RESPONSE_SUCCESS;
    }
*/
    int src_fd = open(fileName_.c_str(), O_RDONLY, 0);
    if (src_fd < 0) {
      outBuffer_.clear();
      handleError(connfd_, 404, "Not Found!");
      return RESPONSE_ERROR;
    }
    void *mmapRet = mmap(NULL, sbuff.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    close(src_fd);
    if (mmapRet == (void *)-1) {
      munmap(mmapRet, sbuff.st_size);
      outBuffer_.clear();
      handleError(connfd_, 404, "Not Found!");
      return RESPONSE_ERROR;
    }
    char *src_addr = static_cast<char *>(mmapRet);
    outBuffer_ += string(src_addr, src_addr + sbuff.st_size);
    size_t ret=writen(connfd_,outBuffer_);
    if(ret<0){
        return RESPONSE_ERROR;
    }
    munmap(mmapRet, sbuff.st_size);

    proc_state_=STATE_FINISH;
    return RESPONSE_SUCCESS;
}

void HttpData::handleError(int fd,int err_num,string short_msg){
  short_msg = " " + short_msg;
  char send_buff[4096];
  string body_buff, header_buff;
  body_buff += "<html><title>error</title>";
  body_buff += "<body bgcolor=\"ffffff\">";
  body_buff += std::to_string(err_num) +" "+ short_msg;
  body_buff += "<hr><em> co_http_server</em>\n</body></html>\0\0";
  
  header_buff += "HTTP/1.1 " + std::to_string(err_num) + short_msg + "\r\n";
  header_buff += "Content-Type: text/html\r\n";
  header_buff += "Connection: Close\r\n";
  header_buff += "Content-Length: " + std::to_string(body_buff.size()) + "\r\n";
  header_buff += "Server: co_http_server\r\n";
  header_buff += "\r\n";

  sprintf(send_buff, "%s", header_buff.c_str());
  write(fd, send_buff, strlen(send_buff));
  sprintf(send_buff, "%s", body_buff.c_str());
  write(fd, send_buff, strlen(send_buff));
  //std::cout<<body_buff.size()<<" "<<strlen(send_buff)<<std::endl;
}

bool HttpData::isResponse(){
    return proc_state_==STATE_RESPONSE;
}

bool HttpData::isError(){
    return proc_state_==STATE_ERROR;
}

//判断keep-alive情况下是否超时
bool HttpData::isTimeout(){
    //如果是第一次连接，还未设置expired_time_ 返回未超时
    if(expired_time_==0) return false;
    auto time_now=getTimeNow();
    //std::cout<<"time_now:"<<time_now<<" expired_time: "<<expired_time_<<std::endl;
    if(expired_time_>=time_now){
        return false;
    }else{
        return true;
    }
}

bool HttpData::isKeepAlive(){
    return keep_alive_;
}

bool HttpData::isParseRq(){
    return proc_state_==STATE_PARSE_REQUEST_LINE ||
           proc_state_==STATE_PARSE_HEADERS ||
           proc_state_==STATE_RECV_BODY;
}
