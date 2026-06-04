//
// Created by DEV on 2026/4/18.
//

#ifndef SIMPLEFFPLAY_PLAYER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>

#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#include <SDL.h>
#include <SDL_mutex.h>
#include <SDL_rect.h>
#include <SDL_render.h>
#include <SDL_video.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>
#endif

#define SIMPLEFFPLAY_PLAYER_H

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct
{
    double pts;
    double pts_drift;
    double last_updated;
    double speed; // 时钟速度控制，用于控制播放速度
    int serial;   // 播放序列，
    int paused;
    int *queue_serial; // 指向packet_serial;
} play_clock_t;

typedef struct
{
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} audio_params_t;

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect *rect;

    int window_width;
    int window_height;
    int width;
    int height;
    double height_width_ratio;
} sdl_video_t;

typedef struct packet_list_node
{
    AVPacket *pkt;
    struct packet_list_node *next;

} packet_list_node_t;

typedef struct packet_queue_t
{
    packet_list_node_t *first_pkt, *last_pkt;
    int nb_packets;// 队列中，packet的数量
    int size ;     // 队列多占存储空间大小
    int64_t duration; // 队列中所有packet 总播放时长
    int abort_request;
    int serial;// 播放序列
    SDL_mutex *mutex;
    SDL_cond *cond;

} packet_queue_t;
/*解码以后得帧*/
typedef struct
{
    AVFrame *frame;
    int serial;      // 播放序号
    double pts;      //  显示时间戳
    double duration; // 该帧预计显示时长
    int64_t pos;     // 该帧对应的packet 在输入文件中的字节偏移，用于seek 定位 todo 需要研究
    int width;       // only use in video
    int height;      // only use in audio
    int format;      // video/audio format
    AVRational sar;  // 视频帧的样本宽高比：Sample Aspect Ratio,用于正确缩放显示
    int uploaded;    // 是否已经上传到GPU纹理（1=已上传，避免重复上传一帧）
} frame_t;

typedef struct
{
    frame_t queue[FRAME_QUEUE_SIZE];
    int rindex; // read index
    int windex;
    int size;     // 总帧数
    int max_size; // 最大帧数
    int keep_last;
    int rindex_shown; // 当前帧,是否有在显示
    SDL_mutex *mutex;
    SDL_cond *cond;
    packet_queue_t *pktq; // 指向对应的packet_queue;
} frame_queue_t;

#endif // SIMPLEFFPLAY_PLAYER_H
