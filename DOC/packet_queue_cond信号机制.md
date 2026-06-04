# packet_queue_put 中 SDL_CondSignal 的信号接收方

## 结论

`SDL_CondSignal(q->cond)` 唤醒的是 **decode 线程**（音频解码线程或视频解码线程）。

---

## 调用链

```
demux 线程
  └─ packet_queue_put()
       └─ SDL_CondSignal(q->cond)   ← 发送信号

decode 线程（audio_decode_thread / video_decode_thread）
  └─ packet_queue_get(block=1)
       └─ SDL_CondWaitTimeout(q->cond, ...)  ← 阻塞等待，收到信号后被唤醒
```

---

## 为什么是 decode 线程？

- **生产者**是 demux 线程，调用 `packet_queue_put` 往队列里塞 packet，塞完后发信号通知"有数据了"。
- **消费者**是 decode 线程，调用 `packet_queue_get(block=1)`，当队列为空时会在 `SDL_CondWaitTimeout` 上挂起。
- `SDL_CondSignal` 唤醒其中一个正在等待该 `cond` 的线程，即挂起中的 decode 线程。

decode 线程被唤醒后：
1. 检查 `abort_request`，若为 1 则返回 -1 并退出线程。
2. 否则回到 `while(1)` 顶部，重新尝试从队列头取 packet。

---

## 同一个 cond 关联两种信号

| 调用位置 | 发信号方 | 含义 |
|---|---|---|
| `packet_queue_put`（packet.c:63） | demux 线程 | "队列有新数据，去取" |
| `packet_queue_flush`（packet.c:188） | 控制线程 | "`abort_request=1`，快退出" |

两种情况都通过同一个 `cond` 发信号，decode 线程被唤醒后通过检查 `abort_request` 来区分是"有数据"还是"要退出"。

---

## 相关代码位置

- `packet_queue_put`：packet.c:32
- `SDL_CondSignal`（put 中）：packet.c:63
- `packet_queue_get`：packet.c:76
- `SDL_CondWaitTimeout`：packet.c:113
- `SDL_CondSignal`（abort 中）：packet.c:188
