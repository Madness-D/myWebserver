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

    //定义读写的缓冲区大小,定义成常量，也可以定义为宏
    static const int  READ_BUFFER_SIZE=2048; //2k
    static const int WRITE_BUFFER_SIZE=1024;//1k,也可以改为2k

    //定义文件名的最大长度
    static const int FILENAME_LEN=200;

    
    //HTTP请求的方法，这里只支持GET
    enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT};

    //解析请求用到的有限状态机模型，下面是定义的相关的状态
    /*
    解析客户端请求时，主状态机状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
    服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    

    //构造与析构
    http_conn(){}
    ~http_conn(){}

    
    void init(int sockfd,const sockaddr_in &addr); //初始化新的接受的连接
    void close_conn(); //关闭连接
    void process();//处理客户端请求，解析http请求报文
    bool read();//非阻塞，读
    bool write();//非阻塞，写

private:

    void init();//初始化连接的其他信息
    
    int m_sockfd;//连接的socket
    sockaddr_in m_address;//socket地址

    CHECK_STATE m_check_state;//主状态机当前所处状态


    //--------------------解析请求的函数以及解析到的结果----------------
    HTTP_CODE process_read(); //解析请求，分别调用下面的“子”函数

    HTTP_CODE parse_request_line(char * text); //解析首行
    HTTP_CODE parse_header(char* text); //解析头部
    HTTP_CODE parse_content(char* text);//解析请求体
    char* getline(){return m_read_buf+m_start_line;}//内联函数，获取一行数据
    HTTP_CODE   do_request();
    LINE_STATUS parse_line();//解析单独的某一行
    //-------------数据----------
    char m_read_buf[READ_BUFFER_SIZE];// 读缓冲区，就是一个数组
    int m_read_idx;//标识读缓冲区中已经读入的数据的最后一个字节的下标，即，读指针
    //解析报文需要的指针
    int m_checked_idx;//解析报文时，正在分析的字符在读缓冲区的位置
    int m_start_line;//当前正在解析的行的起始位置
    //解析到的信息
    char m_real_file[ FILENAME_LEN ]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char* m_url;//解析到的url，锁清秋的目标文件的文件名
    char* m_version;//版本，设定只支持1.1
    METHOD m_method;//请求方法
    char* m_host;//host
    int m_content_length;//HTTP请求的消息总长度
    bool m_linger;//是否为keep alive

    //-----------------构造响应的函数与数据--------------
    bool process_write(HTTP_CODE ret);//填充HTTP应答,同样是调用下面的这些函数
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    //----------------用于构造请求的数据
    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;


};
#endif