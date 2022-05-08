#pragma once
#include <sys/time.h>
#include <string>
#include <fcntl.h>

unsigned long long getTimeNow();

ssize_t readn(int fd, std::string &inBuffer, bool &zero);
ssize_t writen(int fd, std::string &sbuff);

int SetNonBlock(int iSock);

