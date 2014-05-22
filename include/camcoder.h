#ifndef __CAMCODER_H
#define __CAMCODER_H
#include <libavformat/avformat.h>

#define DEFAULT_OUTPUT_FORMAT "mpegts"

#define DEFAULT_HOST_LEN    128
#define DEFAULT_PATH_LEN    128
#define DEFAULT_FILE_LEN    128
#define CAMERA_ID_LEN       32
#define CAM_REC_TOLERANCE   0.05

typedef struct ipcam_ctx{
    int                 fd;                             // the ipcamera socket fd
    char                id[CAMERA_ID_LEN];              // the ipcamera device id
    char                host[DEFAULT_HOST_LEN];         // the ip address of ipcamera
    char                path[DEFAULT_PATH_LEN];         // the rtsp play path
    char                out_file[DEFAULT_FILE_LEN];     // current record file name
    int                 port;                           // client rtsp port assigned from server
    int                 record_enable;                  // 0. disable / 1. enable
    int                 recording;                      // 0. not recording / 1. recording
    int                 video_duration;                 // Video clip length unit in second
    int                 video_idx;                      // Input RTSP video stream index
    int                 audio_idx;                      // Input RTSP audio stream index
    double              video_start_pts;                // the record file start pts
    int                 first_frame_got;                // Got the key frame, start save frame to file
    int                 thumbnail_got;                // Got the key frame, start save frame to file
    AVCodecContext      *video_ctx;                     // RTSP video context
    AVFormatContext     *fmt_ctx;                       // RTSP video input context
    AVFormatContext     *ofmt_ctx;                      // MPEG-TS video output context
}CAM_CTX;

#endif
