#ifndef __CAMCODER_H
#define __CAMCODER_H
#include <libavformat/avformat.h>

#define DEFAULT_OUTPUT_FORMAT "mpegts"

#define DEFAULT_HOST_LEN    128
#define DEFAULT_PATH_LEN    128
#define DEFAULT_FILE_LEN    128

typedef struct ipcam_ctx{
    int                 fd;
    char                host[DEFAULT_HOST_LEN];
    char                path[DEFAULT_PATH_LEN];
    char                out_file[DEFAULT_FILE_LEN];
    int                 port;
	AVFormatContext     *fmt_ctx;
    AVFormatContext     *ofmt_ctx;
}CAM_CTX;

#endif
