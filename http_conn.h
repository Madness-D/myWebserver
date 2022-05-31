#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>

class http_conn{
public:

    static int m_epollfd;//所有的socket事件都被注册到同一个epoll对象上
    static int m_user_count; //统计用户数量


    http_conn(){}
    ~http_conn(){}

    void process();//处理客户端请求，解析http请求报文
    void init(int sockfd,const sockaddr_in &addr);
    void close_conn();
    bool read();//非阻塞，读
    bool write();//非阻塞，写

private:
    int m_sockfd;//连接的socket
    sockaddr_in m_address;//socket地址
};
#endif