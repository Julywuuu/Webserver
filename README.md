# Webserver

### 项目名称：基于Linux的轻量级Web服务器			 	 	项目时间：2023年1月——2023年3月        
### 关 键 词：Linux；C++；Socket；HTTP；epoll；线程池

### 项目描述：项目在Linux环境下使用C++语言来搭建轻量级多线程服务器，服务器能够支持一定数量的客户端并发访问并及时响应，服务器支持客户端访问服务器中的图片。

###1、设计使用线程池 + 非阻塞Socket + epoll (ET模式) + 模拟Proactor的并发模型；
###2、使用状态机解析HTTP请求报文，支持GET请求；
###3、使用基于升序链表的定时器将不活跃的客户访问及时关闭；
###4、修改线程池+内存映射地址为智能指针，不需要手动释放，防止内存泄漏。

# My Webserver finished!
# i wanna a grogeous job! please!
##  during:2023/2/15 - 2023/4/13


