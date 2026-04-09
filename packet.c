#include "packet.h"

/**
 * 初始化 packet 队列
 * 队列是链表结构：demux 线程往里放 packet，decode 线程从里取 packet
 * @param q  待初始化的队列指针
 * @return   0=成功，负数=失败
 */
int packet_queue_init(packet_queue_t *q) {
  memset(q, 0, sizeof(packet_queue_t));  // 清零整个结构体，防止残留脏数据
  q->mutex = SDL_CreateMutex();          // 创建互斥锁，保护链表的并发读写
  if (!q->mutex) {                       // 创建失败时打印错误并返回
    av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
    return AVERROR(ENOMEM);              // AVERROR 把 errno 包装成 FFmpeg 错误码
  }
  q->cond = SDL_CreateCond();  // 创建条件变量，队列有数据时用来唤醒等待的线程
  if (!q->cond) {
    av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
    return AVERROR(ENOMEM);
  }
  q->abort_request = 0;  // 初始化中止标志为 0，表示队列正常工作
  return 0;
}

/**
 * 向队列尾部写入一个 packet（生产者调用，即 demux 线程）
 * pkt 是一包未解码的压缩数据（音频或视频）
 * @param q   目标队列
 * @param pkt 要写入的 packet，调用后其所有权转移，外部不能再使用
 * @return    0=成功，-1=内存分配失败
 */
int packet_queue_put(packet_queue_t *q, AVPacket *pkt) {
  packet_listnode_t *pkt_listnode;

  pkt_listnode = av_malloc(sizeof(packet_listnode_t));  // 分配链表节点内存
  if (!pkt_listnode) {
    return -1;  // 内存不足，直接返回失败
  }
  pkt_listnode->pkt = av_packet_alloc();  // 分配节点内部的 AVPacket 结构体
  if (!pkt_listnode->pkt) {
    av_free(pkt_listnode);  // AVPacket 分配失败，释放节点内存防止泄漏
    return -1;
  }

  // move_ref：把 pkt 的所有权移交给节点内部的 pkt，外部 pkt 的字段会被清零
  // 这样外部调用方可以安全地复用那个 AVPacket 结构体
  av_packet_move_ref(pkt_listnode->pkt, pkt);
  pkt_listnode->next = NULL;  // 新节点插入链表尾部，next 指向 NULL

  SDL_LockMutex(q->mutex);  // 加锁，以下操作涉及共享链表

  if (!q->last_pkt)  // 队列为空（无尾节点），新节点同时作为头节点
  {
    q->first_pkt = pkt_listnode;
  } else {
    q->last_pkt->next = pkt_listnode;  // 队列非空，把新节点挂到尾节点的 next
  }
  q->last_pkt = pkt_listnode;            // 更新尾节点指针
  q->nb_packets++;                       // 队列中 packet 数量加 1
  q->size += pkt_listnode->pkt->size;    // 累加总字节数，用于背压控制

  // 发送信号，唤醒正在 CondWait 等待数据的消费者（decode 线程）
  SDL_CondSignal(q->cond);

  SDL_UnlockMutex(q->mutex);  // 解锁
  return 0;
}

/**
 * 从队列头部读取一个 packet（消费者调用，即 decode 线程）
 * @param q     源队列
 * @param pkt   读出的 packet 写到这里（调用者负责后续 av_packet_unref）
 * @param block 阻塞标志：1=队列空时阻塞等待，0=队列空时立即返回
 * @return      1=成功取到，0=队列空且非阻塞，-1=队列已中止
 */
int packet_queue_get(packet_queue_t *q, AVPacket *pkt, int block) {
  packet_listnode_t *p_pkt_node;  // 临时节点指针，用于摘下头节点
  int ret;

  SDL_LockMutex(q->mutex);  // 加锁

  while (1) {
    p_pkt_node = q->first_pkt;  // 尝试取链表头节点

    if (p_pkt_node)  // 队列非空，可以取数据
    {
      q->first_pkt = p_pkt_node->next;  // 头指针前移，指向下一个节点
      if (!q->first_pkt) {
        q->last_pkt = NULL;  // 取完后链表为空，同步清空尾指针
      }
      q->nb_packets--;                      // 队列计数减 1
      q->size -= p_pkt_node->pkt->size;     // 总字节数减去本 packet 的大小

      // move_ref：把节点中的 packet 所有权移交给调用者传入的 pkt
      av_packet_move_ref(pkt, p_pkt_node->pkt);

      av_packet_free(&p_pkt_node->pkt);  // 释放节点内部的 AVPacket 结构体（数据已转移，只释放壳）
      av_free(p_pkt_node);               // 释放链表节点本身的内存

      ret = 1;  // 返回值：成功取到 packet
      break;
    } else if (!block)  // 队列空，且调用方不要求阻塞
    {
      ret = 0;  // 返回值：没取到
      break;
    } else  // 队列空，且调用方要求阻塞等待
    {
      // 使用超时等待（20ms），而非永久阻塞
      // 原因：即使信号偶尔丢失，最多 20ms 后也会重新检查队列，保证鲁棒性
      int signaled = 1;
      while (signaled && !q->abort_request) {
        // CondWaitTimeout：原子地释放 mutex 并挂起线程，收到信号或超时后重新加锁返回
        signaled = SDL_CondWaitTimeout(q->cond, q->mutex, 20);
      }
      if (q->abort_request) {     // 被唤醒后检查是否因 abort 退出
        SDL_UnlockMutex(q->mutex);
        return -1;  // 队列已中止，通知调用方退出线程
      }
      // 收到有数据的信号，回到 while(1) 顶部重新尝试取数据
    }
  }

  SDL_UnlockMutex(q->mutex);  // 解锁
  return ret;
}

/**
 * 向队列写入一个空 packet（null packet）
 * 用途：Seek 完成后通知解码器"把内部缓冲区里积压的帧都冲出来"
 * 解码器收到 data=NULL / size=0 的 packet 后会刷新内部缓冲区
 * @param q            目标队列
 * @param stream_index 流索引，让解码器知道这个 null packet 属于哪个流
 */
int packet_queue_put_nullpacket(packet_queue_t *q, int stream_index) {
  AVPacket *pkt = av_packet_alloc();    // 分配一个临时 AVPacket
  pkt->data = NULL;                     // data=NULL 是 null packet 的标志
  pkt->size = 0;                        // size=0 是 null packet 的标志
  pkt->stream_index = stream_index;     // 标记属于哪个流（音频/视频）
  int ret = packet_queue_put(q, pkt);   // 复用普通 put 函数写入队列
  av_packet_free(&pkt);                 // 释放临时 AVPacket（数据已通过 move_ref 转移）
  return ret;
}

/**
 * 清空队列中所有 packet，释放对应内存
 * 典型场景：Seek 时丢弃队列中所有旧数据
 * @param q 目标队列
 */
void packet_queue_flush(packet_queue_t *q) {
  packet_listnode_t *pkt, *pkt1;  // pkt=当前节点，pkt1=下一节点（防释放后丢失指针）

  SDL_LockMutex(q->mutex);  // 加锁，遍历期间不允许其他线程操作

  for (pkt = q->first_pkt; pkt; pkt = pkt1) {
    pkt1 = pkt->next;           // 先保存下一个节点的指针，再释放当前节点
    av_packet_free(&pkt->pkt);  // 释放节点内的 AVPacket（含内部 data 缓冲区）
    av_free(pkt);               // 释放链表节点本身
  }

  q->last_pkt = NULL;   // 重置尾指针
  q->first_pkt = NULL;  // 重置头指针
  q->nb_packets = 0;    // 重置计数
  q->size = 0;          // 重置总字节数
  q->duration = 0;      // 重置总时长

  SDL_UnlockMutex(q->mutex);
}

/**
 * 销毁队列，释放所有资源
 * 程序退出时调用：先清空数据，再销毁同步原语
 * @param q 目标队列
 */
void packet_queue_destroy(packet_queue_t *q) {
  packet_queue_flush(q);        // 先清空队列中所有 packet 数据
  SDL_DestroyMutex(q->mutex);  // 销毁互斥锁
  SDL_DestroyCond(q->cond);    // 销毁条件变量
}

/**
 * 中止队列，唤醒所有正在等待的线程
 * 程序退出时调用：让阻塞在 packet_queue_get 里的 decode 线程感知到退出信号
 * @param q 目标队列
 */
void packet_queue_abort(packet_queue_t *q) {
  SDL_LockMutex(q->mutex);
  q->abort_request = 1;    // 设置中止标志，packet_queue_get 检测到后返回 -1
  SDL_CondSignal(q->cond); // 唤醒所有阻塞在 CondWait 的线程，让它们检查 abort_request
  SDL_UnlockMutex(q->mutex);
}
