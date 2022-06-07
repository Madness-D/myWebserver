#include"http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
//TODO: 更改文件地址
const char* doc_root = "/home/ubuntu/myWebServer/resources";

//所有的socket上的事件都注册到同一个epoll事件内核事件中
int http_conn::m_epollfd =-1;
int http_conn::m_user_count =0; //所有客户数

//设置文件为非阻塞
int setnonblocking( int fd){
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag=old_flag| O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
    return old_flag;
}

//添加需要监听的文件描述符到epoll中
void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd=fd;
    //水平触发、边缘触发,
    event.events=EPOLLIN | EPOLLRDHUP ;//水平触发
    //event.events=EPOLLIN | EPOLLET | EPOLLRDHUP;//边缘触发
    if(one_shot){//保障一个socket连接在任一时刻只被一个线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //设置文件描述符非阻塞
    setnonblocking(fd);

}

//从epoll中删除文件描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//修改文件描述符,
//重置socket的EPOLLONESHOT事件，确保下次可读时EPOLLIN事件能被触发，否则只会被触发一次
void modfd(int epollfd, int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    event.events=ev |EPOLLET| EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//关闭连接
void http_conn::close_conn(){
    if(m_sockfd!=-1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        --m_user_count;//关闭一个连接，用户数--
    }
}


//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd,const sockaddr_in &addr){
    m_sockfd=sockfd;
    m_address=addr;

    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd,m_sockfd,true);
    ++m_user_count; //总用户数+1

    init();//调用，初始化一些私有信息
    //为什么分成两个init？ 
    //有时候重置读写的连接，用户数并不一定++
}

void http_conn::init(){//初始化连接的其他信息
    m_check_state=CHECK_STATE_REQUESTLINE;//初始状态：解析请求首行

    m_checked_idx=0;
    m_start_line=0;
    m_read_idx=0;
    m_write_idx=0;

    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_linger=false; //默认不保持连接，keep-alive字段

    //清空读写缓冲区, 文件名
    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_write_buf,WRITE_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);

}


//循环读取数据，直到无数据可读或客户关闭连接
bool http_conn::read(){
    
    if(m_read_idx>=READ_BUFFER_SIZE){//已经读取完毕了
        return false;
    }

    //读取到的字节
    int bytes_read=0;
    while(true){
        //recv(int sockfd, void *buf, size_t len, int flags);
        //receive a message from a socket,返回接收的字节数，出错则返回-1
        //从m_read_buf+m_read_idx处开始保存数据，大小为READ_BUFFER_SIZE-m_read_idx
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read==-1){ //读取失败
            if(errno==EAGAIN || errno==EWOULDBLOCK){
                //没有数据
                break;
            }
            return false;
        }else if(bytes_read == 0){
            //对方关闭连接，没读到数据
            return false;
        }
        //更新读指针
        m_read_idx+=bytes_read;

    }
    //printf("读取到数据：%s \n",m_read_buf);//打印读取到的数据
    return true;
}

//主状态机，解析请求
//TODO:检查是否一致
http_conn::HTTP_CODE http_conn::process_read(){//解析请求，分别调用下面的“子”函数
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;

    char* text=0;
    //当已经解析到请求体 或 解析一行完整的数据，则进入循环体
    while((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK)||((line_status=parse_line())==LINE_OK)){

        //获取一行数据
        text=getline();
        m_start_line=m_checked_idx;

        printf("got 1 http line : %s \n",text);
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{ //解析首部
                ret=parse_header(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    //获取完整的请求头,且无content
                    return do_request();//解析具体的信息
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret=parse_content(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    //获取完整的请求头
                    return do_request();//解析具体的信息
                }
                line_status = LINE_OPEN; //TODO: ?
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
} 

//解析首行， 获取请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    //首行举例：   GET /index.html HTTP/1.1    中间以制表符分隔
    //方法： 正则，或字符串拆分

    //tips：C的库函数，检索返回str1中第一个匹配str2的字符的位置
    m_url=strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }
    //GET\0/index.html HTTP/1.1
    *m_url++='\0';//将GET后的\t置为字符串结束符

    char* method=text; //根据置的\0，截取"方法"字符串
    //   只处理get方法，其他的方法一律返回BAD_REQUEST
    if(strcasecmp(method,"GET")==0){ //忽略大小写比较
        m_method=GET;
    }else{
        return BAD_REQUEST;
    }
    //url: /index.html HTTP/1.1
    m_version=strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }

    *m_version++='\0';
    //只处理1.1版本的
    //⭐，m_version="HTTP/1.1"
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }

    //下面处理中间的url字段
    //有些请求，可能如 http://192.168.1.1:10000/index.html
    if(strncasecmp(m_url,"http://",7)==0){ //即，以http:// 开头
        m_url+=7; //跳到IP地址或域名开始的地方
        m_url=strchr(m_url,'/'); //跳转到/第一次出现的位置
    }
    //⭐ m_url :"/index.html"
    if(!m_url || m_url[0]!='/'){
        return BAD_REQUEST;
    }
    m_check_state=CHECK_STATE_HEADER;//主状态机检查状态改变为检查请求头
    return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
//仅解析： connection、content_length  host 头部字段
http_conn::HTTP_CODE http_conn::parse_header(char* text){

    //遇到空行标识头部字段解析完毕
    if(text[0]=='\0'){
        //如果HTTP请求有消息体，则需要继续读取m_content_length字节的消息体
        //主状态机转移至 CHECK_STATE_CONTENT状态
        if(m_content_length!=0){
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则。说明请求已经读取完毕
        return GET_REQUEST;        
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) { 
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;//若为keep-alive,设置m_linger字段
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text); //读取消息体长度
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text; 
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
} 

//解析请求体，这里只是判断是否完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx>=(m_checked_idx+m_content_length)){
        text[ m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//解析报文中单独的某一行，行结束的标志： \r\n, 
//这个函数只是将结束的标识符转化成\0 即转换成字符串以进一步解析
//tips: \r是回车标识，\n是换行标识
http_conn::LINE_STATUS http_conn::parse_line(){

    char temp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        temp=m_read_buf[m_checked_idx];
        if(temp=='\r'){
            if((m_checked_idx+1)==m_read_idx){ // 仅回车就到达读的数据的末尾，表明该行未读取完整
                return LINE_OPEN; 
            }else if(m_read_buf[m_checked_idx+1]=='\n'){
                //读取到完整的 \r\n
                m_read_buf[m_checked_idx++]='\0';//将'\r \n'改成'、0'
                m_read_buf[m_checked_idx++]='\0';
                return  LINE_OK;
            }
            return LINE_BAD;//只有一个单独的\r，格式错误
        }else if(temp=='\n'){ //不知为何读取到了\n,反过来判断上一个字符是否为\r
            if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r')){
                //\r\n
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_read_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
    
}


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，
//并告诉调用者获取文件成功
http_conn::HTTP_CODE  http_conn::do_request(){
    // "/home/nowcoder/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}


//写HTTP请求
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init(); //响应结束，初始化连接信息
        return true;
    }

    while(true) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger) {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    return
    add_content_length(content_len)&&
    add_content_type() &&
    add_linger() &&
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

//由线程池中的工作线程调用。处理HTTP请求的入口函数
void http_conn::process(){
    //解析HTTP请求,利用有限状态机模型，状态见h文件
    HTTP_CODE  read_ret=process_read();
    if(read_ret==NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return ;
    }

// 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}