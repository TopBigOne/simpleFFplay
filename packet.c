//
// Created by DEV on 2026/5/27.
//

#include "packet.h"

int packet_queue_init(packet_queue_t *q)
{
    // 防止有脏数据
    memset(q, 0, sizeof(packet_queue_t));
    // 创建互斥锁，保护链表并发读写
    q->mutex = SDL_CreateMutex();
    if (!q->mutex)
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex():%s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    // 创建条件变量,队列有数据的时候，用来唤醒等待线程
    q->cond = SDL_CreateCond();
    if (!q->cond)
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond():%s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 0;
    return 0;
}
int packet_queue_put(packet_queue_t *q, AVPacket *pkt)
{
    // 分配节点内存
    packet_list_node_t *packet_node = av_malloc(sizeof(packet_list_node_t));
    if (packet_node == NULL)
    {
        return -1;
    }
    // 分配节点内部的AVPacket的结构体
    packet_node->pkt = av_packet_alloc();
    if (!packet_node->pkt)
    {
        av_free(packet_node);
        return -1;
    }
    // move_ref: 把pkt的所有权移交给节点内部的pkt，外部的字段就会被清零
    // 这样可以安全复用pkt
    av_packet_move_ref(packet_node->pkt, pkt);
    // 链表节点后移动
    packet_node->next = NULL;
    // 加锁，以下操作涉及共享链表
    SDL_LockMutex(q->mutex);
    // 链表为空（没有尾节点），新节点同时作为头节点
    if (!q->last_pkt)
    {
        q->first_pkt = packet_node;
    }
    else
    {
        // 队列非空，把新节点挂到尾节点next
        q->last_pkt = packet_node;
    }
    // 更新尾节点
    q->last_pkt = packet_node;
    // 队列中的packet数量加1；
    q->nb_packets++;
    // 累加字节总数，用户被压控制
    q->size += packet_node->pkt->size;
    // 发送信号，唤醒正在 CondWait 等待数据的线程（decode 线程）
    SDL_CondSignal(q->cond);
    // 线程释放锁
    SDL_UnlockMutex(q->mutex);

    return 0;
}
int packet_queue_get(packet_queue_t *q, AVPacket *pkt, int block)
{
    packet_list_node_t *pkt_node;
    int ret;
    SDL_LockMutex(q->mutex);
    while (1)
    {
        pkt_node = q->first_pkt;
        if (pkt_node)
        {
            q->first_pkt = pkt_node->next;
            if (!q->first_pkt)
            {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt_node->pkt->size;
            av_packet_move_ref(pkt, pkt_node->pkt);
            av_packet_free(&pkt_node->pkt);
            av_free(pkt_node);
            ret = 1;
            break;
        }
        else if (!block) // 队列空，且不阻塞
        {
            ret = 0;
            break;
        }
        else
        {
            // 队列空，且需要等待
            // 使用超时等待20ms，而不是永久阻塞
            // because of:即使信号偶尔丢失，最多20ms 也能重新检查队列
            int signaled = 1;
            while (signaled && !q->abort_request)
            {
                // 0 if the condition variable is signaled,
                signaled = SDL_CondWaitTimeout(q->cond, q->mutex, 20);
            }
            if (q->abort_request)
            {
                SDL_UnlockMutex(q->mutex);
                return -1;
            }
            // 收到有数据的信号，回到while(1) 顶部，重新获取数据
        }
    }

    SDL_UnlockMutex(q->mutex);

    return ret;
}

/**
 * 清空队列中所有packet ，释放对应内存
 * 典型场景：Seek 时丢弃队列中所有的旧数据
 * @param q
 */
void packet_queue_flush(packet_queue_t *q)
{
    packet_list_node_t *pkt, *pkt1;
    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1)
    {
        pkt1 = pkt->next;
        av_packet_free(&pkt->pkt);
        av_free(pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;

    SDL_UnlockMutex(q->mutex);
}

int packet_queue_put_null_packet(packet_queue_t *q, int stream_index)
{
    AVPacket *pkt = av_packet_alloc();
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    int ret = packet_queue_put(q, pkt);
    av_packet_free(&pkt);
    return ret;
}

void packet_queue_destroy(packet_queue_t *q)
{
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void packet_queue_abort(packet_queue_t *q)
{
    SDL_LockMutex(q->mutex);
    // 设置终止标志，[packet_queue_get]检查到后，返回-1；
    q->abort_request = 1;
    // 唤醒所有阻塞在CondWait的线程，让他们检查 abort_request
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}