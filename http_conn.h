#ifndef HTTPCONNECT_H
#define HTTPCONNECT_H

#include<sys/epoll.h>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
// #include "locker.h"
#include"lst_timer.h"
#include<sys/uio.h>
#include<iostream>
#include<string.h>
#include<memory>

class sort_timer_lst;               // 定时器链表声明
class util_timer;                   // 定时器类声明

#define COUT_OPEN 1
#define TIMESLOT 5                  // 定时周期 /s
const bool ET = true;

class http_conn
{
public:
    static int m_epollfd;                               // 所有的socket上的事件都被注册到同一个epollfd上
    static int m_user_count;                            // 统计所有的用户数量

    static const int READ_BUFFER_SIZE = 2048;           // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;          // 写缓存区的大小
    static const int FILENAME_LEN = 200;                // 文件名的最大长度

    static int m_request_cnt;                           // 接收到的请求的次数
    static sort_timer_lst m_timer_lst;                  // 定时器链表

    util_timer* timer;
public:
    // 状态机 的 状态
    // HTTP请求方法   只支持get
    enum MTEHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACR, OPTIONS, CONNECT, PATCH};         

    /* 
        解析客户的htp请求时， 主状态机的状态
            CHECK_STATE_REQUESTLINE：当前正在分析请求行
            CHECK_STATE_HEADER：正在分析请求头
            CHECK_STATE_CONTENT：正在分析请求体
    */
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER, CHECK_STATE_CONTENT} ;

    /*
        从状态机的状态
            1. LINE_OK 读取到一个完整的行
            2. LINE_BAD 行出错
            3. LINE_OPEN 行数据还没检测完 /r/n
    */
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN};  

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
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

public:
    http_conn();
    ~http_conn();
    void init(int sockfd, struct sockaddr_in &addr);// 初始化新的连接
    void close_conn();                              // 关闭连接
    void process() ;                                // 处理客户端的请求、对客户端的响应
    bool read();                                    // 非阻塞的读
    bool write();                                   // 非阻塞的写

    void del_fd();                                  // 定时器回调函数


private:
    HTTP_CODE process_read();                       // 解析http
    bool process_write( HTTP_CODE ret );            // 响应http
    // process READ 调用

    HTTP_CODE parse_request_line(char* text);       // 解析请求行
    HTTP_CODE parse_headers(char* text);            // 解析请求头
    HTTP_CODE parse_content(char* text);            // 解析请求体
    LINE_STATUS prase_line();                       // 解析一行数据
    HTTP_CODE do_request();                         // 处理具体请求
    char* get_line() {return m_read_buf + m_start_line; }   // 获取一行数据 return m_rd_buf + m_line_start;

    // process WRITE 调用
    // void unmap();
    void unmap(char* p);          // 111111111

    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();

    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    

private:
    int m_sockfd;                           // 该http连接的socket
    sockaddr_in m_address;                  // 通信的客户端的socket的地址

    char m_read_buf[READ_BUFFER_SIZE];      // 读缓冲区
    int m_read_index;                       // 标识读缓冲区中已经读入的客户端数据最后一位的下一位。
    int m_check_index;                      // 当前正在分析的字符再读缓冲区的位置
    int m_start_line;                       // 当前解析的行的起始位置

    CHECK_STATE m_check_state;              // 主状态机当前的状态
    MTEHOD m_method;                        // 请求方法
    
    char* m_url;                            // 请求目标文件的文件名
    char* m_version;                        // 协议版本，只支持http1.1
    char* m_host;                           // 主机名
    bool m_linger;                          // HTTP是否保持连接
    int m_content_lenth;                    // 请求内容的长度
    char m_real_file[ FILENAME_LEN ];       // 资源的路径 , 其内容是 doc_root + m_url, doc_root是网站的根目录
    std::shared_ptr<char> m_file_address;   

    // char * m_file_address;                  // 客户目标文件被映射到内存中的起始位置
    struct stat m_file_stat;                // 目标文件的状态

    

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;                         // 被写内存块的数量
    int bytes_to_send;                      // 将要发送的字节
    int bytes_have_send;                    // 已经发送的字节

    void init();                            // 初始化连接以外的信息

};

#endif