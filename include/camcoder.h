#ifndef __CAMCODER_H
#define __CAMCODER_H
#include <libavformat/avformat.h>
typedef struct ipcam_ctx{
    int                 fd;
    char                host[128];
    char                path[128];
    int                 port;
	AVFormatContext     *fmt_ctx;
}CAM_CTX;

#endif
