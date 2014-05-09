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
    exit(0);
    return 0;
}

int ffurl_open_only(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    int ret = ffurl_alloc(puc, filename, flags, int_cb);
    if (ret)
        return ret;
    if (options && (*puc)->prot->priv_data_class &&
        (ret = av_opt_set_dict((*puc)->priv_data, options)) < 0)
        goto fail;
    return 0;
fail:
    ffurl_close(*puc);
    *puc = NULL;
    return ret;
}
int record(CAM_CTX *ctx)
{
    int err;
    int lower_transport_mask = 0;
    int ret = 0;
    char real_challenge[64] = "";
    char tcpname[1024], control_uri[1024], cmd[2048];
    RTSPMessageHeader reply1 = {0}, *reply = &reply1;
	ctx->fmt_ctx = avformat_alloc_context();
    AVFormatContext *s = ctx->fmt_ctx;

    s->priv_data = av_mallocz(sizeof(RTSPState));
    if(!s->priv_data) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    memset(s->priv_data, 0, sizeof(RTSPState));
    if(!ff_network_init()) return AVERROR(EIO);

    //s->max_delay = s->iformat ? DEFAULT_REORDERING_DELAY: 0;

    RTSPState *rt =  s->priv_data;
    ff_url_join(tcpname, sizeof(tcpname), "tcp", NULL, ctx->host, ctx->port, "?timeout=%d", rt->stimeout);

    rt->control_transport = RTSP_MODE_PLAIN;
    lower_transport_mask = rt->lower_transport_mask;
    rt->lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
    lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
    if(ret = ffurl_open_only(&rt->rtsp_hd, tcpname, AVIO_FLAG_READ_WRITE, &s->interrupt_callback, NULL)){
        av_log(s, AV_LOG_ERROR, "Unable to alloc rtsp_hd\n");
        return ret;
    }
    s->iformat = av_find_input_format("rtsp");

    rt->state       = RTSP_STATE_IDLE;
    rt->rtsp_hd_out = rt->rtsp_hd;
    rt->seq         = 0;
    TCPContext *tc = rt->rtsp_hd->priv_data;
    ff_url_join(rt->control_uri, sizeof(control_uri), "rtsp", NULL, ctx->host, ctx->port, "%s", ctx->path);
    printf("%s\n", rt->control_uri);
    tc->fd = ctx->fd;
    rt->rtsp_hd->is_connected = 1;
    rt->user_agent = strdup(USER_AGENT);
    rt->rtp_port_min = 30000;
    rt->rtp_port_max = 60000;
    int tcp_fd = ffurl_get_file_handle(rt->rtsp_hd);
    printf("tcp_fd:%d\n", tcp_fd);
    cmd[0] = 0;
    //OPTIONS
    ff_rtsp_send_cmd(s, "OPTIONS", rt->control_uri, cmd, reply, NULL);
    if (reply->status_code != RTSP_STATUS_OK) {
        goto fail;
    }
    //DESCRIBE
    if (s->iformat){
        err = ff_rtsp_setup_input_streams(s, reply);
    }
    //SETUP
    printf("number of rtsp streams %d\n",rt->nb_rtsp_streams);
    do {
        int lower_transport = RTSP_LOWER_TRANSPORT_TCP;
        err = ff_rtsp_make_setup_request(s, ctx->host, ctx->port, lower_transport,
                                 rt->server_type == RTSP_SERVER_REAL ?
                                     real_challenge : NULL);
        if (err < 0)
            goto fail;
        lower_transport_mask &= ~(1 << lower_transport);
        if (lower_transport_mask == 0 && err == 1) {
            err = AVERROR(EPROTONOSUPPORT);
            goto fail;
        }
    } while (err);
    rt->lower_transport_mask = lower_transport_mask;
    av_strlcpy(rt->real_challenge, real_challenge, sizeof(rt->real_challenge));
    rt->state = RTSP_STATE_IDLE;
    rt->seek_timestamp = 0;
    return 0;
fail:
	return -1;
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
        record(ctx);
      	close(fd);
    }
	return 0;
}
