#include "lst_timer.h"

// 往链表中添加定时器timer
void sort_timer_lst::add_timer(util_timer* timer) {
    // timer是null 直接返回
    if(!timer) {
        return;
    }
    // 定时器链表为空，timer就是链表首
    if( !head ) {
        head = timer;
        tail = timer;
    }
    // timer的时间比链表头部时间还小，说明timer的时间最小，需要放到头部作为新的头节点！
    else if( timer->expire < head->expire ) {
        timer->next = head;
        head->prev = timer;
        head = timer;
    }
    // 往链表中间找个合适的位置插入，调用重载函数，保证链表的升序特性
    else {
        add_timer(timer, head);
    }
}

// 重载函数add_timer,该函数表示将目标定时器 timer 添加到节点 lst_head 之后的部分链表中 
void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head){
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;
    /* 遍历 list_head 节点之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间节点并将目标定时器插入该节点之前 */
    while( tmp ) {
        if( timer->expire < tmp->expire ) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = tmp;
            tmp->prev = timer;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    /* 如果遍历完 lst_head 节点之后的部分链表，仍未找到超时时间大于目标定时器的超时时间的节点，则将目标定时器插入链表尾部，并把它设置为链表新的尾节点。*/
    if( !tmp ) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

/* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。这个函数只考虑被调整的定时器的超时时间延长的情况，即该定时器需要往链表的尾部移动。*/
void sort_timer_lst::adjust_timer(util_timer* timer) {
    if( !timer ) {
        return;
    }
    util_timer* tmp = timer->next;
    // 如果被调整的目标定时器处在链表的尾部，或者该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
    if( !tmp || (timer->expire < tmp->expire)) {

    }
    else if( timer == head) {
        head = head->next;      // 把当前的头节点删除
        head->prev = nullptr;   
        timer->next = nullptr;
        timer->prev = nullptr;                                          //!
        add_timer( timer, head );  // 重新调用重载函数插入结点
    }
    // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer( timer, timer->next );
    }
}

// 将目标定时器 timer 从链表中删除
void sort_timer_lst::del_timer( util_timer* timer ) {
    if( !timer ) {
        return;
    }
    // 下面这个条件成立表示链表中只有一个定时器，即目标定时器
    if( timer == head && tail == timer ) {
        delete timer;
        head = nullptr;
        tail = nullptr;
    }
    /* 如果链表中至少有两个定时器，且目标定时器是链表的头节点 */
    else if( timer == head ) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
    }
    /* 如果链表中至少有两个定时器，且目标定时器是链表的尾节点 */
    else if( timer == tail ){
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
    }
    /* 目标是中间结点 */
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
}

/* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理链表上到期任务。*/
void sort_timer_lst::tick(){
    if( !head ) {
        return;
    }
    time_t curr_time = time(NULL);          // 获取当前系统时间
    util_timer* tmp = head;
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while( tmp ) {
        // 升序排列的，所以第一个没超时，后面的都没超
        if( curr_time < tmp->expire ) {
            break;
        }
        // 超时了，后面的都得删
        tmp->user_data->close_conn();
        del_timer(tmp);
        tmp = head;
    }

}
