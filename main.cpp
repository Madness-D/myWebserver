#include<cstdio>
#include<cstdlib>
#include<cstring>
//#include<sys/socket.h>
//#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h> //文件控制
#include<sys/epoll.h>
#include<signal.h>
#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65535//最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大数目

//信号处理,添加信号捕捉
void addsig(int sig,void (handler)(int) ){ //参数：信号、信号处理函数
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa)); //初始化，都置空
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}

//添加文件描述符到epoll中,定义在http_conn
extern void addfd(int epollfd,int fd,bool one_shot);
//从epoll中删除描述符
extern void removefd(int epollfd,int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd,int ev);

int main(int argc, char* argv[]){ //参数用于指定端口号

    //判断参数
    if(argc<=1){//参数不足，未传入端口号
        printf("按照如下格式运行： %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port=atoi(argv[1]);

    //对SIGPIE信号进行处理
    addsig(SIGPIPE,SIG_IGN);//忽略SIGPIPE

    //创建线程池
    threadpool< http_conn> *pool=NULL;
    try{
        pool=new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }


    //创建一个数组保存所有的客户端信息
    http_conn *users=new http_conn[MAX_FD ];

    int listenfd=socket(PF_INET,SOCK_STREAM,0);

    //设置端口复用，必须在绑定之前
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);//大小端转换
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));

    //监听
    listen(listenfd,5);

    //创建epoll对象、事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;

    while(1){ //循环检测事件是否发生
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(num<0 && errno!=EINTR){
            printf("epoll failure \n");
            break;
        }

        //循环遍历事件数组
        for(int i=0;i<num;i++){
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd){
                //有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen=sizeof(client_address);  
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);
                //不判断是否accept成功了
                if(http_conn::m_user_count>=MAX_FD){
                    //目前连接满了
                    //给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放入users数组中
                users[connfd].init(connfd,client_address);
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //异常断开或错误，则断开连接
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN){ //读事件
                if(users[sockfd].read()){//将所有数据 成功读出
                    pool->append(users+sockfd);//传入的是地址,任务追加到线程池中
                }else{
                    users[sockfd].close_conn();
            }
        }else if(events[i].events & EPOLLOUT){
            if(!users[sockfd].write()){//一次性写完，但是失败
                users[sockfd].close_conn();
            }
        }
    }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;


    return 0;
}