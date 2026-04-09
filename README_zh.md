# simpleFFplay

![play](./play.png)

一个基于 FFmpeg4 和 SDL2 的简化版 ffplay，旨在成为比"如何用不到 1000 行代码编写视频播放器"以及 pockethook/player 等更好的媒体播放器学习示例。

**其他播放器示例存在的问题：**
1. `ffplay.c`：理解其音视频同步代码相当困难。命令行选项解析、avfilter 和字幕功能使代码更难阅读。
2. 一些较简单的示例只能播放视频流。
3. 部分示例使用了过时的 FFmpeg API。FFmpeg API 更新较快，许多接口已被废弃。
4. `leichn/ffplayer` 对初学者来说是个不错的示例，但仍存在一些问题：
	- 无法正常退出。
	- 暂停功能不稳定。
	- 使用了已废弃的 AVPacketList 结构体。
	- 不支持跳转（Seek）。

`simpleFFplay` 是 `leichn/ffplayer` 的改进版本，两者均基于 FFmpeg 的 ffplay.c。

**所需 FFmpeg 版本为 4.4.2，SDL2 版本为 2.0.20，测试系统为 Ubuntu 22.04。其他环境或许也能运行，但尚未经过测试。**

## 使用方法

- `暂停 / 继续播放`：空格键
- `快进 10s | 快退 10s | 快进 60s | 快退 60s`：右方向键 | 左方向键 | 上方向键 | 下方向键
- `退出`：ESC

## 参考资料

以下是一些推荐的实用资料（较新）：
- [leandromoreira/ffmpeg-libav-tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial)
- [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg)
- [中文博客](https://www.cnblogs.com/leisure_chn/p/10301215.html)

## 许可证

GPLv3（继承自 FFmpeg）。
