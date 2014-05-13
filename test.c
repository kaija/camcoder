#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libavformat/avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avformat.h"
#include "avio_internal.h"
#define USER_AGENT "Camcoder-0.1"
#include "rtsp.h"

extern const uint8_t ff_log2_tab[256];
typedef struct TCPContext {
    const AVClass *class;
    int fd;
    int listen;
    int open_timeout;
    int rw_timeout;
    int listen_timeout;
} TCPContext;

#include "noly.h"
#include "camcoder.h"

int test()
{
    AVFormatContext *pFormatCtx=NULL;
	pFormatCtx = avformat_alloc_context();
    //avformat_open_input(&pFormatCtx, "rtsp://192.168.15.222/ChannelID=1&ChannelName=Channel1" , NULL,NULL);
    avformat_open_input(&pFormatCtx, "rtsp://10.211.55.2:8554/demo.264" , NULL,NULL);
    avformat_find_stream_info(pFormatCtx, NULL);
    printf("width:%d height:%d\n",pFormatCtx->streams[0]->codec->width, pFormatCtx->streams[0]->codec->height);
    exit(0);
    return 0;
}


int main()
{
	av_register_all();
	avformat_network_init();
	CAM_CTX *ctx = malloc(sizeof(CAM_CTX));
    test();
	if(!ctx) return -1;

    sprintf(ctx->host, "10.211.55.2");
    ctx->port = 8554;
    sprintf(ctx->path, "/demo.264");

	int fd = noly_tcp_connect(ctx->host, ctx->port);
    if(fd) {
		printf("Connected\n");
        ctx->fd = fd;
      	close(fd);
    }
	return 0;
}
