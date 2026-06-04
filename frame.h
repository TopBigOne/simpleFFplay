//
// Created by DEV on 2026/4/18.
//

#ifndef SIMPLEFFPLAY_FRAME_H
#define SIMPLEFFPLAY_FRAME_H
#include "player.h"
int frame_queue_init(frame_queue_t *f, packet_queue_t *pktq, int max_size, int keep_last);

void  frame_queue_unref_item(frame_t *vp);

/**
 * 向所有等待此队列的线程发动信号
 * @param f
 */
void frame_queue_signal(frame_queue_t *f);

/**
 *  读取当前待显示的帧，（不移动指针，peek，只是偷看）
 * @param f
 * @return
 */
frame_t *frame_queue_peek(frame_queue_t *f);

/**
 * 读取下一帧（不移动指针）
 * @param f
 * @return
 */
frame_t *frame_queue_peek_next(frame_queue_t *f);

/**
 * 读取上一帧（已经显示过的帧，keep_last 模式下，不会释放）
 * 用途：视频暂停时，反复显示这一帧
 * @param f
 * @return
 */
frame_t *frame_queue_peek_last(frame_queue_t *f);

/**
 *  向队列尾部申请一个可写的帧槽位置
 *  Note 1: 如果队列已经满了，就阻塞等待，直到有槽位空出来
 *  Note 2: 此函数返回的槽位指针，不移动写指针，
 *  调用者往槽位写完数据以后，需要主动push一下，便于提交
 * @param f
 * @return
 */
frame_t *frame_queue_peek_writable(frame_queue_t *f);

/**
 * 从队列头部取一帧（消费者调用：display thread 或者音频callback）
 * @param f
 * @return
 */
frame_t *frame_queue_peek_readable(frame_queue_t *f);

/**
 * 提交一帧到队列，同时移动指针，并更新计数
 * @param f
 */
void frame_queue_push(frame_queue_t *f);
/**
 *  消费当前帧，移动读指针（消费者调用，配合 frame_queue_peek_readable 使用）
 *
 * @param f
 */
void frame_queue_next(frame_queue_t *f);

int frame_queue_nb_remaining(frame_queue_t *f);

/**
 * 返回最近一次显示在文件之后的字节偏移位置
 * @param f
 * @return
 */
int64_t frame_queue_last_pos(frame_queue_t *f);

/**
 *  销毁队列，释放所有资源
 * @param f
 * @return
 */
void frame_queue_destroy(frame_queue_t *f);

#endif // SIMPLEFFPLAY_FRAME_H
