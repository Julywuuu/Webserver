#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include<signal.h>
#include<iostream>
#include"http_conn.h"
#include <assert.h>
#include<vector>

#define MAX_FD 65535            // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听事件的数量

static int pipefd[2];           // 管道文件描述符 0为读，1为写

// 添加信号捕捉
void addsig(int sig, void(handler)(int) ) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);        // 阻塞信号集
    sigaction(sig, &sa, NULL);
}

// 向管道写数据的信号捕捉回调函数
void sig_to_pipe(int sig){
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );         // 往管道中写的就是捕捉到的信号的值 1号信号...
    errno = save_errno;
}

// 声明epoll信号操作函数
extern void addfd(int epollfd, int fd, bool one_shot, bool et);
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
extern void setnonblocking(int fd);

// 添加文件描述符到epoll中
int main(int argc, char const *argv[])
{
    if(argc <= 1) {
        std::cout<<"按照如下格式运行:"<<basename(argv[0])<<"port_number"<<std::endl;
        exit(-1);
    }
    // 获取端口号
    int port = atoi(argv[1]);

    // 对SIGPIE信号进行处理, 忽略；当服务器仍在尝试发送数据时客户端意外断开连接，也会产生SIGPIPE。为了防止进程被终止，程序员可以选择忽略该信号
    addsig(SIGPIPE, SIG_IGN);

    // 创建listen套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    // 设置端口复用 - before bind!
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 绑定
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert( ret != -1);

    // 监听
    ret = listen(listenfd, 5);
    assert( ret != -1 );

    // 创建epoll实例
    epoll_event events[MAX_EVENT_NUMBER] = {};               // 传出参数,存放发生变化的文件描述符的信息, 进行初始化
    int epollfd = epoll_create(5);                      // 创建epoll实例

    // 将监听的文件描述符添加到epoll对象中, 不需要ONESHOT 、 ET
    addfd(epollfd, listenfd, false, false);


    // 创建管道
    ret = socketpair(PF_UNIX,SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );                   // 设置写端非阻塞
    addfd(epollfd, pipefd[0], false, false);        // 注册监听读端

    // 设置信号处理函数
    addsig(SIGALRM, sig_to_pipe);                   // 定时器信号
    addsig(SIGTERM, sig_to_pipe);                   // SIGTERM 关闭服务器

    bool stop_server = false;                       // 关闭服务器标志位
    
    http_conn::m_epollfd = epollfd;                     // http_conn.h 中的静态变量. 所有的socket上的事件都被注册到同一个epoll实例上
    http_conn* users = new http_conn[MAX_FD];           // 创建一个数组保存所有客户端的信息

    // 改users为智能指针，没成功
    // std::vector<std::shared_ptr<http_conn>> users;
    // users.reserve(MAX_FD);
    // for(int i=0; i<MAX_FD; i++) {
    //     users[i] = std::make_shared<http_conn>();
    // }


    // 创建线程池
    // threadpool<http_conn>* pool = NULL;
    // try
    // {
    //     pool = new threadpool<http_conn>;
    // }
    // catch(const std::exception& e)
    // {
    //     std::cerr << e.what() << '\n';
    //     exit(-1);
    // }
    // 线程池，智能指针版本
    std::unique_ptr<threadpool<http_conn>> pool;
    try {
        pool.reset(new threadpool<http_conn>);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        exit(-1);
    }

    bool timeout = false;       // 定时器周期已到
    alarm(TIMESLOT);            // 定时产生SIGALRM信号
    
    while ( !stop_server )
    {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);        // 阻塞，返回事件的数量
        if( (num<0) && (errno != EINTR)) {
            std::cout<<"epoll failure\n"<<std::endl;
            break;
        }

        // 循环遍历时间数组，epoll_wait的传出参数events
        for(int i=0; i<num; i++) {
            struct epoll_event curevent = events[i];
            int sockfd = events[i].data.fd;
            // 有客户端连接
            if(sockfd == listenfd) {
                struct sockaddr_in clientaddr;
                socklen_t size = sizeof(clientaddr);
                int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &size);
                
                // 连接数满了
                if(http_conn::m_user_count >= MAX_FD) {
                    // 给客户端回复一个信息：服务器正忙
                    close(connfd);
                    continue;
                }
                // 将新的客户的数据初始化，放到user数组中
                // users[connfd].init(connfd, clientaddr);
                users[connfd].init(connfd, clientaddr);
            }
            // 读管道有数据，SIGALRM或者SIGTERM触发
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if( ret == -1) {
                    continue;
                }
                else if( ret == 0) {
                    continue;
                }
                else{
                    for( int i=0; i < ret; i++) {
                        switch (signals[i])
                        {
                        case SIGALRM:   
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                        }
                    }
                }
            }
            // 对方异常断开或者错误等事件
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
                http_conn::m_timer_lst.del_timer( users[sockfd].timer );                        // 删除对应的定时器
            }
            // 一次性读完所有数据
            else if(events[i].events & EPOLLIN) {
                if((users[sockfd]).read()) {
                    pool->append(users + sockfd);  // 插入的是http_conn类型的地址： users数组首地址 + sockfd 就是该user在users中存放的地址
                }
                else{
                    users[sockfd].close_conn();
                    http_conn::m_timer_lst.del_timer( users[sockfd].timer );                        // 删除对应的定时器
                }
            }
            // 一次写完所有的数据
            else if(events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    users[sockfd].close_conn();
                    http_conn::m_timer_lst.del_timer( users[sockfd].timer );                        // 删除对应的定时器
                }
            }
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if(timeout) {
            http_conn::m_timer_lst.tick();          // 定时处理任务，实际上就是调用tick()函数
            alarm(TIMESLOT);                        // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
            timeout = false;                        // 重置timeout
        }
    }
    
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    // delete pool;//?

    return 0;
}
