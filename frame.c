#include "frame.h"

#include "player.h"

/**
 * 释放帧内部的数据缓冲区（但不释放 frame_t 结构体本身）
 * av_frame_unref 只释放 AVFrame 内部持有的数据引用（图像/音频数据）
 * AVFrame 结构体本身被复用，下次写入新帧时直接覆盖
 * @param vp 指向帧槽位的指针
 */
void frame_queue_unref_item(frame_t *vp)
{
    // 释放 AVFrame 内部引用的缓冲区（YUV 像素数据或 PCM 音频数据）
    // 注意：只是 unref（减引用计数），不是 free（释放结构体）
    av_frame_unref(vp->frame);
}

/**
 * 初始化帧队列（环形缓冲区）
 * 帧队列用于存放解码后的原始帧（视频 YUV 帧 或 音频 PCM 帧）
 * 与 packet_queue 的链表不同，frame_queue 是固定大小的环形缓冲区
 *
 * @param f         待初始化的帧队列
 * @param pktq      关联的 packet 队列（用于读取 abort_request 标志）
 * @param max_size  队列最大容量（视频通常 3，音频通常 9）
 * @param keep_last 是否保留最后一帧（暂停时需要继续显示最后一帧）
 * @return 0 成功，负数失败
 */
int frame_queue_init(frame_queue_t *f, packet_queue_t *pktq, int max_size, int keep_last)
{
    int i;

    // 清零整个结构体
    memset(f, 0, sizeof(frame_queue_t));

    // 创建互斥锁，保护读写指针和 size 字段的并发访问
    if (!(f->mutex = SDL_CreateMutex()))
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    // 创建条件变量，用于"有帧可读"和"有空位可写"的线程通知
    if (!(f->cond = SDL_CreateCond()))
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    // 记录关联的 packet 队列，后续通过它读取 abort_request 标志
    f->pktq = pktq;

    // 实际容量取 max_size 和 FRAME_QUEUE_SIZE 的较小值，防止越界
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);

    // !! 是双重逻辑非：把任意非零值转为 1，保证 keep_last 只有 0 或 1
    // Note :C11标准 6.3.1.2节：将标量值转换为_Bool时，0→0，非0→1
    f->keep_last = !!keep_last;

    // 预先为每个槽位分配 AVFrame 结构体（只分配结构体，不分配像素数据）
    // 这样后续解码时可以直接把数据写入这些槽位，避免频繁分配/释放
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);

    return 0;
}

/**
 * 销毁帧队列，释放所有资源
 * 程序退出时调用
 * @param f 目标帧队列
 */
void frame_queue_destory(frame_queue_t *f)
{
    int i;

    // 遍历所有槽位，逐个释放
    for (i = 0; i < f->max_size; i++)
    {
        frame_t *vp = &f->queue[i];

        // 先 unref，释放 AVFrame 内部的数据缓冲区（像素/音频数据）
        frame_queue_unref_item(vp);

        // 再 free，释放 AVFrame 结构体本身
        av_frame_free(&vp->frame);
    }

    // 销毁互斥锁和条件变量
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

/**
 * 向所有等待此队列的线程发送信号
 * 用于"强制唤醒"，例如 Seek 或退出时让阻塞的线程立即返回
 * @param f 目标帧队列
 */
void frame_queue_signal(frame_queue_t *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond); // 唤醒等待 cond 的线程
    SDL_UnlockMutex(f->mutex);
}

/**
 * 读取当前待显示帧（不移动读指针，只是"偷看"）
 * 结合 keep_last 特性：
 *   - rindex_shown=0 时，返回 rindex 位置（上次还没"正式消费"的帧）
 *   - rindex_shown=1 时，返回 rindex+1 位置（下一帧，也就是真正当前帧）
 *
 * 环形索引计算：(rindex + rindex_shown) % max_size
 * @param f 帧队列
 * @return 当前帧的指针
 */
frame_t *frame_queue_peek(frame_queue_t *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/**
 * 读取下一帧（当前帧的后一帧，同样不移动读指针）
 * 用于提前判断下一帧的时间戳，辅助计算当前帧的显示时长
 * 环形索引：(rindex + rindex_shown + 1) % max_size
 * @param f 帧队列
 * @return 下一帧的指针
 */
frame_t *frame_queue_peek_next(frame_queue_t *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

/**
 * 读取上一帧（已经显示过的帧，keep_last 模式下不会被释放）
 * 用途：暂停时反复显示这一帧；也用于部分计算需要参考上一帧
 * 直接返回 rindex 位置（rindex 始终指向"最近一次显示过的帧"）
 * @param f 帧队列
 * @return 上一帧的指针
 */
frame_t *frame_queue_peek_last(frame_queue_t *f)
{
    return &f->queue[f->rindex];
}

/**
 * 向队列尾部申请一个可写的帧槽位（生产者调用，即 decode 线程）
 * 如果队列已满（所有槽位都被占用），则阻塞等待，直到有槽位空出来
 * 注意：此函数只返回槽位指针，不移动写指针；
 *       调用方往槽位写完数据后，还需调用 frame_queue_push() 提交
 *
 * @param f 帧队列
 * @return 可写槽位的指针，若队列已中止则返回 NULL
 */
frame_t *frame_queue_peek_writable(frame_queue_t *f)
{
    SDL_LockMutex(f->mutex);

    // 循环等待，直到队列有空位（size < max_size）或收到中止信号
    int signaled = 1;
    while (signaled && f->size >= f->max_size && !f->pktq->abort_request)
    {
        // 队列满了，等待消费者读走一帧后发出的信号
        // SDL_CondWaitTimeout：释放 mutex，挂起，等信号或最多 20ms
        signaled = SDL_CondWaitTimeout(f->cond, f->mutex, 20);
    }
    SDL_UnlockMutex(f->mutex);

    // 如果是因为 abort 而退出等待，返回 NULL 通知调用方退出
    if (f->pktq->abort_request)
        return NULL;

    // 返回写指针当前位置的槽位（环形缓冲区，windex 已对 max_size 取模）
    return &f->queue[f->windex];
}

/**
 * 从队列头部读取一帧（消费者调用，即 display 线程或音频 callback）
 * 如果队列没有可读帧，则阻塞等待，直到 decode 线程写入新帧
 * 注意：此函数只返回帧指针，不移动读指针；
 *       调用方显示完帧后，还需调用 frame_queue_next() 移动读指针
 *
 * @param f 帧队列
 * @return 可读帧的指针，若队列已中止则返回 NULL
 */
frame_t *frame_queue_peek_readable(frame_queue_t *f)
{
    int signaled = 1;
    SDL_LockMutex(f->mutex);

    // f->size - f->rindex_shown：实际可读帧数
    //   f->size      = 队列中总帧数
    //   f->rindex_shown = 1 表示 rindex 那一帧已经"展示过"但未释放（keep_last）
    // 所以可读帧数要减去这个"已展示未释放"的帧
    while (signaled && f->size - f->rindex_shown <= 0 && !f->pktq->abort_request)
    {
        // 队列空，等待 decode 线程写入新帧后发出的信号
        signaled = SDL_CondWaitTimeout(f->cond, f->mutex, 20);
    }
    SDL_UnlockMutex(f->mutex);

    // 如果是因为 abort 而退出等待，返回 NULL
    if (f->pktq->abort_request)
        return NULL;

    // 返回当前可读帧：(rindex + rindex_shown) % max_size
    // 与 frame_queue_peek() 逻辑相同
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/**
 * 提交一帧到队列（生产者调用，配合 frame_queue_peek_writable 使用）
 * 调用方已经把数据写入 windex 指向的槽位，这里只负责移动写指针、更新计数
 * @param f 帧队列
 */
void frame_queue_push(frame_queue_t *f)
{
    // 写指针加 1，如果到达末尾则回绕到 0（实现环形）
    if (++f->windex == f->max_size)
        f->windex = 0;

    SDL_LockMutex(f->mutex);

    // 队列中帧数加 1
    f->size++;

    // 通知正在等待可读帧的消费者线程（display 线程 / 音频 callback）
    SDL_CondSignal(f->cond);

    SDL_UnlockMutex(f->mutex);
}

/**
 * 消费完当前帧，移动读指针（消费者调用，配合 frame_queue_peek_readable 使用）
 *
 * keep_last 机制说明：
 *   keep_last=1 时，队列会保留"最近一次显示的帧"，不立即释放
 *   这样暂停时可以反复调用 frame_queue_peek_last() 拿到这帧继续显示
 *
 *   第一次调用 frame_queue_next()：rindex_shown 从 0 变为 1
 *     → rindex 不动，只是把 rindex_shown 立个标记说"rindex 那帧已显示"
 *     → 此时 rindex 那帧还在队列里（keep_last 保留）
 *   后续再调用 frame_queue_next()：rindex_shown 已经是 1
 *     → 正常 unref 并前移 rindex，释放上上帧
 *
 * @param f 帧队列
 */
void frame_queue_next(frame_queue_t *f)
{
    // keep_last=1 且 rindex_shown=0，说明这是第一次"消费"当前帧
    if (f->keep_last && !f->rindex_shown)
    {
        // 只标记"rindex 那帧已经展示过"，不释放、不移动 rindex
        f->rindex_shown = 1;
        return;
    }

    // 释放 rindex 指向的帧的内部数据（像素/音频缓冲区）
    frame_queue_unref_item(&f->queue[f->rindex]);

    // 读指针加 1，如果到达末尾则回绕到 0（实现环形）
    if (++f->rindex == f->max_size)
        f->rindex = 0;

    SDL_LockMutex(f->mutex);

    // 队列中帧数减 1
    f->size--;

    // 通知正在等待空槽位的生产者线程（decode 线程）
    SDL_CondSignal(f->cond);

    SDL_UnlockMutex(f->mutex);
}

/**
 * 返回队列中尚未显示的帧数
 * = 总帧数 - rindex_shown（keep_last 保留的那帧不算"未显示"）
 * 用途：视频播放线程判断是否有新帧需要显示
 * @param f 帧队列
 * @return 未显示帧数
 */
int frame_queue_nb_remaining(frame_queue_t *f)
{
    return f->size - f->rindex_shown;
}

/**
 * 返回最近一次显示帧在文件中的字节偏移位置
 * 用途：Seek 时用来对齐 packet 位置，确保 Seek 后从正确位置解码
 *
 * @param f 帧队列
 * @return 字节偏移（pos），若没有有效的已显示帧则返回 -1
 */
int64_t frame_queue_last_pos(frame_queue_t *f)
{
    // 取 rindex 处的帧（最近一次已显示的帧）
    frame_t *fp = &f->queue[f->rindex];

    // 必须满足两个条件才返回有效位置：
    // 1. rindex_shown=1：rindex 那帧确实已经显示过
    // 2. serial 一致：这帧是当前播放序列的（防止 Seek 后串号）
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1; // 没有有效的已显示帧，返回 -1
}
