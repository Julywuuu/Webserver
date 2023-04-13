#include "http_conn.h"

// 定义http响应的状态信息
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
char * doc_root = "/home/north/Webserver/resources";

// 类中的静态成员需要外部定义
int http_conn::m_epollfd = -1;              // 所有的socket上的事件都被注册到同一个epollfd上
int http_conn::m_user_count = 0;            // 统计所有的用户数量
int http_conn::m_request_cnt = 0;           //请求数量
sort_timer_lst http_conn::m_timer_lst;      // 定时器链表

// 设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 像epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot, bool et){
    epoll_event event;
    memset(&event, 0, sizeof(event));   // 初始化
    event.data.fd = fd;
    if(et) {
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if(one_shot) {
        event.events |= EPOLLONESHOT;                    //每个文件描述符（socket等）只会触发一次；防止一个线程在处理业务呢，然后来数据了，又从线程池里拿一个线程来处理新的业务。
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);                                 //设置文件描述符非阻塞, ET模式下必须
}

// 移除epoll中的文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件 确保下次可读时 epollin事件可以被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = ev|EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

http_conn::http_conn(){}
http_conn::~http_conn(){}

// 初始化连接
void http_conn::init(int fd, struct sockaddr_in &addr) {
    this->m_sockfd = fd;
    this->m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加进入epoll实例中
    addfd(this->m_epollfd, m_sockfd, true, ET);
    ++m_user_count;

    init();         // 初始化其他信息，私有函数

    // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
    util_timer* new_timer = new util_timer;
    new_timer->user_data = this;

    time_t curr_time = time(NULL);
    new_timer->expire = curr_time + 3*TIMESLOT;

    this->timer = new_timer;
    m_timer_lst.add_timer(new_timer);
}

// 初始化状态机状态
void http_conn::init() {
    bzero(m_read_buf, READ_BUFFER_SIZE);            // 清空读缓存
    bzero(m_write_buf, WRITE_BUFFER_SIZE);          // 清空写缓存
    bzero(m_real_file, FILENAME_LEN);               // 清空文件路径
    m_check_state = CHECK_STATE_REQUESTLINE;        // 初始化状态为请求解析首行
    m_check_index = 0;

    m_start_line = 0;
    m_read_index = 0;
    m_write_idx = 0;

    m_url = 0;
    m_method = GET;
    m_version = 0;

    m_linger = false;
    m_host = 0;

    m_content_lenth = 0;

    bytes_have_send = 0;
    bytes_to_send = 0;

    m_file_address = nullptr;
}

// 断开连接
void http_conn::close_conn() {
    if(m_sockfd != -1) {
        this->m_user_count--;
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
    }
}


// 解析HTTP请求行
// GET /index.html HTTP/1.1
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    
    m_url = strpbrk(text, " \t");
    if(!m_url) {
        return BAD_REQUEST;
    }
    //GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }else {
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    //GET\0/index.html\0HTTP/1.1
    *m_version++ = '\0';

    // if(strcasecmp(m_version, "HTTP/1.1") != 0)  return BAD_REQUEST;      // 非HTTP1.1版本，压力测试时为1.0版本，忽略改行


    // 可能出现带地址的格式 http://192.168.1.1:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');     // 寻找第一个/  : /index.html
    }

    if(!m_url || *m_url != '/'){        // 没找到/
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;     // 请求头解析完了。改变主状态为检查header
    
}     


// 解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    /* 遇到空行，表示 请求头解析完毕*/
    if(text[0] == '\0') {
        /* 如果有消息体的话，就该解析消息体了 */
        if(m_content_lenth != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    /* Connection: keep-alive */
    else if( strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if( strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    /* connectlenth 字段*/
    else if( strncasecmp(text, "Connect-Length:", 15) == 0 ) {
        text += 15;
        text += strspn( text, " \t" );
        m_content_lenth = atol( text );
    }
    /*Host: 192.168.42.129:10000*/
    else if( strncasecmp( text, "Host:", 5 ) == 0 ) {
        text += 5;
        text += strspn( text, " \t");
        m_host = text;
    } else{
        std::cout<< "oop! unknow header: "<<text<<std::endl;
    }
    return NO_REQUEST;
}        


/* 并没有真正的解析 http请求的消息体，只是判断它是否完整读入 */
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_index >= (m_content_lenth + m_check_index)){
        text [m_content_lenth] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}          


//从状态机； 解析一行， 判断依据是 \r\n
http_conn::LINE_STATUS http_conn::prase_line(){
    int temp;
    for(; m_check_index<m_read_index; m_check_index++) {
        temp = m_read_buf[m_check_index];
        if(temp == '\r') {
            if(m_check_index + 1 == m_read_index) {                     // 读到 \r 了，但是下一位就没了。表示数据还没读完呢
                return LINE_OPEN;       //?
            }else if(m_read_buf[m_check_index + 1] == '\n'){            // 读到 \r 了，下一位是\n ，表示完整的一行解析完了
                m_read_buf[m_check_index++] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n') {                                        // 读到 \n 了， 但是上一位是 \r， 表示漏掉了。这是一个完整的解析行
            if(m_read_buf[m_check_index-1] == '\r'){
                m_read_buf[m_check_index-1] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // return LINE_OPEN;
    }
    return LINE_OPEN;
}     

/*
得到一个完整的，正确的 HTTP请求时，分析目标文件的属性
当目标文件存在、对所有用户可读、并且不是目录、就使用mmap将其映射到内存地址 m_file_stress 处，并告诉调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request(){

    //  /home/north/Webserver/resources
    strcpy(m_real_file, doc_root);
    int len = strlen( doc_root );
    // /home/north/Webserver/resources/index.html
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1);  
    // 获取文件资源的状态信息 -1 失败 0 成功
    if( stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    // 判断访问权限
    if( ! (m_file_stat.st_mode & S_IROTH) ) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if( S_ISDIR(m_file_stat.st_mode) ) {
        return BAD_REQUEST;
    }
    // 创建内存映射， 把 m_real_file 映射到内存 m_file_address 中
    int fd = open( m_real_file, O_RDONLY );

    // 修改智能指针
    // m_file_address = (char*) mmap( NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (m_file_stat.st_size != 0)
    {
        m_file_address = std::shared_ptr<char>(
            (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0),
            [this](char* p) {
                this->unmap(p);
            });
    }
    else
    {
        m_file_address = nullptr;
    }

    close( fd );
    return FILE_REQUEST;

}

void http_conn::unmap(char* p) {
    munmap(p, m_file_stat.st_size);
}

// // 释放内存映射
// void http_conn::unmap(){
//     if( m_file_address ) {
//         munmap(m_file_address.get(), m_file_stat.st_size);
//         m_file_address = NULL;
//     }
// }


bool http_conn::read(){
    // 更新超时时间
    if(timer) {
        time_t curr_time = time(NULL);
        timer->expire = curr_time + 3 * TIMESLOT;
        m_timer_lst.adjust_timer(timer);
    }

    if(this->m_read_index >= READ_BUFFER_SIZE){             // 超过缓存区的大小
        return false;
    }
    int bytes_read = 0;
    while (1)
    {   
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        // 读取失败
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        // 对方关闭连接
        else if(bytes_read == 0) {
            return false;
        }
        // 读取到了
        m_read_index += bytes_read;
    }

    ++m_request_cnt;
    return true;
}


http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 解析到了请求体 或者 解析到了一行完整的数据
    while ((m_check_state == CHECK_STATE_CONTENT) && (line_status ==LINE_OK)   
            || ((line_status = prase_line()) == LINE_OK)){
        text = get_line();
        m_start_line = m_check_index;                   // 更新startline的位置
        std::cout<< "get 1 http line >>>" << text <<std::endl;

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }else if(ret == GET_REQUEST) {
                return do_request();
            }
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }else if(ret == GET_REQUEST){
                return do_request();
            }
            line_status = LINE_OPEN;
            break;

        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}           


bool http_conn::write(){
    int temp = 0;

    if(timer) {
        time_t curr_time = time(NULL);
        timer->expire = curr_time + 3*TIMESLOT;
        m_timer_lst.adjust_timer( timer );
    }

    if (bytes_to_send == 0) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while (1)
    {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= 1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            // unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address.get() + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if( bytes_to_send <= 0 ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            // unmap();
            modfd( m_epollfd, m_sockfd, EPOLLIN );
            if(m_linger) {
                init();
                return true;
            }
            else {
                return false;
            }
        }
    }
    
}
// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ...) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list );
    if( len >= (WRITE_BUFFER_SIZE-1-m_write_idx) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length( int content_len ) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger(){
    return add_response( "Connection: %s\r\n", (m_linger==true) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line(){
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content) {
    return add_response( "%s", content);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 根据解析http请求的结果，决定返回给客户端的内容
/*  
        服务器处理http请求可能的结果：
            NO_REQUESEST：请求不完整，需要继续读取客户数据
            GET_REQUEST：完成了一个客户请求
            BAD_REQUEST：客户请求语法错误
            NO_RESOURCE：服务器没有资源
            FORBIDDEN_REQUEST：客户对资源没有权限
            FILE_REQUEST：文件请求，获取文件成功
            INTERNAL_ERROR：服务器内部错误
            CLOSED_CONNECTION：客户端已经关闭连接了
    */
bool http_conn::process_write( HTTP_CODE ret ){
    switch( ret ) {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen(error_500_form) );
            if( !add_content( error_500_form )) {
                return false;
            }
            break;;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen(error_400_form) );
            if( !add_content( error_400_form ) ) {
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
            m_iv[ 1 ].iov_base = m_file_address.get();
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}


// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );      // 重新监听此socket，并且重新设置epolloneshot
        return;
    }
    
    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
        if(timer) m_timer_lst.del_timer(timer);     // 移除其对应的定时器
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}