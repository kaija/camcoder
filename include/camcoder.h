#ifndef __CAMCODER_H
#define __CAMCODER_H
#include <libavformat/avformat.h>

#define DEFAULT_OUTPUT_FORMAT "mpegts"

typedef struct ipcam_ctx{
    int                 fd;
    char                host[128];
    char                path[128];
    int                 port;
	AVFormatContext     *fmt_ctx;
    AVFormatContext     *ofmt_ctx;
}CAM_CTX;

#endif
