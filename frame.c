//
// Created by DEV on 2026/4/18.
//

#include "frame.h"
/**
 * 初始化帧队列（环形缓冲区）
 * frame_queue 是固定大小的环形缓冲区
 * @param f
 * @param pktq
 * @param max_size
 * @param keep_last
 * @return
 */
int frame_queue_init(frame_queue_t *f, packet_queue_t *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(frame_queue_t));
    if (!(f->mutex = SDL_CreateMutex()))
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    if (!(f->cond = SDL_CreateCond()))
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    // 记录关联 packet 队列，后续通过它获取 abort_request 标志
    f->pktq = pktq;
}

/**
 * 释放（擦除）AVFrame中数据，但是不释放AVFrame本身
 * @param vp
 */
void frame_queue_unref_item(frame_t *vp)
{
    av_frame_unref(vp->frame);
}

void frame_queue_signal(frame_queue_t *f)
{
    SDL_LockMutex(f->mutex);
    // 唤醒等待cond的线程
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

frame_t *frame_queue_peek(frame_queue_t *f)
{
    const int index = (f->rindex + f->rindex_shown) % f->max_size;
    return &f->queue[index];
}

frame_t *frame_queue_peek_next(frame_queue_t *f)
{
    const int next_index = (f->rindex + f->rindex_shown + 1) % f->max_size;
    return &f->queue[next_index];
}

frame_t *frame_queue_peek_last(frame_queue_t *f)
{
    return &f->queue[f->rindex];
}

frame_t *frame_queue_peek_writable(frame_queue_t *f)
{
    SDL_LockMutex(f->mutex);
    int signaled = 1;
    while (signaled && f->size >= f->max_size && !f->pktq->abort_request)
    {

        // 队列满了，等到消费者读走一帧后，发出的信号，每20ms检查一下while里的条件
        // case 1:  收到 SDL_CondSignal / SDL_CondBroadcast 主动唤醒 signaled 的值就会值0；
        // case 2:  等待超时（20ms 到了没人发信号）        │ SDL_MUTEX_TIMEDOUT（非0）
        // case 3:  出错                               │ -1
        signaled = SDL_CondWaitTimeout(f->cond, f->mutex, 20);
    }
    SDL_UnlockMutex(f->mutex);
    if (f->pktq->abort_request)
    {
        return NULL;
    }
    // 返回写指针当前位置的槽位（环形缓冲区，windex 已对max_size 取模）
    return &f->queue[f->windex];
}

frame_t *frame_queue_peek_readable(frame_queue_t *f)
{
    SDL_LockMutex(f->mutex);
    int signaled = 1;
    // f.size         : 队列中总帧数目
    // f.rindex_shown : =1 表示rindex 那一帧已经展示过，但是没有释放（keep_last导致的）
    // f.size - f.rindex_shown : 实际可读帧数
    // 所以可读帧数，要减去这个“已展示但未释放”的帧
    while (signaled && f->size - f->rindex_shown <= 0 && !f->pktq->abort_request)
    {
        // 队列为空，等待20ms，然后每次轮训
        signaled = SDL_CondWaitTimeout(f->cond, f->mutex, 20);
    }
    SDL_UnlockMutex(f->mutex);
    if (f->pktq->abort_request)
    {
        return NULL;
    }
    const int index = (f->rindex + f->rindex_shown) % f->max_size;
    return &f->queue[index];
}
void frame_queue_push(frame_queue_t *f)
{
    // 写指针+1，若是到达队尾，则回绕到0（实现环形）
    if (++f->windex == f->max_size)
    {
        f->windex = 0;
    }
    SDL_LockMutex(f->mutex);
    // 队列帧数加1
    f->size++;
    // 通知正在等待可读帧的消费者线程（display thread / audio callback）
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}
/**
 * keep_last 机制说明：
 * keep_last=1时，队列会保留最近一次显示的帧，不立即释放；
 * 这样展厅时反复调用frame_queue_peek_last() 拿到者帧继续显示
 * ---
 * 第一次调用frame_queue_next() ,rindex_shown 从0变为1
 *     ：rindex 不动，只是把rindex_shown 立个标记，说rindex 那帧已经显示
 *     ：此时rindex 那帧还在队列里（keep_last 保留）
 * 后续再调用 frame_queue_next() ，rindex_shown 已经是1；
 *      ：正常unref 并前移rindex，释放上一帧
 * @param f
 */
void frame_queue_next(frame_queue_t *f)
{
    if (f->keep_last && !f->rindex_shown)
    {
        // 只标记 “rindex” 那帧已经展示过，不释放，不移动rindex
        f->rindex_shown = 1;
        return;
    }
    // 释放rindex 指向的帧的内部数据
    frame_queue_unref_item(&f->queue[f->rindex]);

    if (++f->rindex == f->max_size)
    {
        f->rindex = 0;
    }
    SDL_LockMutex(f->mutex);
    f->size--;
    // 通知正在等待空槽位的生产者线程（decode 线程）
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}
/**
 * 返回队列中没有显示的帧数
 * @param f
 * @return
 */
int frame_queue_nb_remaining(frame_queue_t *f)
{
    return f->size = f->rindex_shown;
}

int64_t frame_queue_last_pos(frame_queue_t *f)
{
    frame_t *fp = &f->queue[f->rindex];
    // 必须满足2个条件
    // 1. rindex_shown = 1,rindex 那帧确实已经显示过
    // 2. serial 一致，这帧是当前播放序列的（防止seek后串号）
    if (f->rindex_shown && fp->serial == f->pktq->serial)
    {
        return fp->pos;
    }
    // 没有有效的已显示帧，返回-1；
    return -1;
}

void frame_queue_destroy(frame_queue_t *f)
{
    for (int i = 0; i < f->max_size; i++)
    {
        frame_t *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}
