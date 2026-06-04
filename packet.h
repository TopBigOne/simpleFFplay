//
// Created by DEV on 2026/5/27.
//

#include "player.h"
#ifndef SIMPLEFFPLAY_PACKET_H
#define SIMPLEFFPLAY_PACKET_H

int packet_queue_init(packet_queue_t *q);

int packet_queue_put(packet_queue_t *q, AVPacket *pkt);

/**
 * 从队列头部读取一个packet（消费者使用：decode线程）
 * @param q
 * @param block
 * @return
 */
int packet_queue_get(packet_queue_t *q, AVPacket *pkt, int block);

void packet_queue_flush(packet_queue_t *q);

int  packet_queue_put_null_packet(packet_queue_t *q,int stream_index);

/**
 * 销毁队列，释放都有资源
 * 程序退出时调用：先清空数据，再销毁同步原语
 * @param q
 */
void packet_queue_destroy(packet_queue_t *q);

/**
 * 终止队列，唤醒所有正在等待的线程
 * @param q
 */
void packet_queue_abort(packet_queue_t *q);

#endif // SIMPLEFFPLAY_PACKET_H
