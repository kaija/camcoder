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

AVCodecContext *rtsp_find_video_stream_avcodecctx(AVFormatContext *s){
    int i;
    for (i = 0; i < s->nb_streams; i++)
    {
        if (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            return s->streams[i]->codec;
        }
    }
    return NULL;
}

AVCodec *rtsp_find_video_stream_decoder(AVFormatContext *s){
    int i;
    for (i = 0; i < s->nb_streams; i++)
    {
        if (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            return avcodec_find_decoder(s->streams[i]->codec->codec_id);
        }
    }
    return NULL;
}

static int rtsp_stream_keepalive(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
	if(rt->get_parameter_supported){
		ff_rtsp_send_cmd_async(s, "GET_PARAMETER", rt->control_uri, NULL);
	}else{
		ff_rtsp_send_cmd_async(s, "OPTIONS", rt->control_uri, NULL);
	}
	return 0;
}

static int rtsp_stream_pause(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;

    if (rt->state != RTSP_STATE_STREAMING)
        return 0;
    else if (!(rt->server_type == RTSP_SERVER_REAL && rt->need_subscription)) {
        ff_rtsp_send_cmd(s, "PAUSE", rt->control_uri, NULL, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            return -1;
        }
    }
    rt->state = RTSP_STATE_PAUSED;
    return 0;
}

static int rtsp_stream_close(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;

    if (!(rt->rtsp_flags & RTSP_FLAG_LISTEN))
        ff_rtsp_send_cmd_async(s, "TEARDOWN", rt->control_uri, NULL);

    ff_rtsp_close_streams(s);
    ff_rtsp_close_connections(s);
    ff_network_close();
    rt->real_setup = NULL;
    av_freep(&rt->real_setup_cache);
    return 0;
}

static int rtsp_stream_play(AVFormatContext *s)
{
    RTSPState *rt = s->priv_data;
    RTSPMessageHeader reply1, *reply = &reply1;
    int i;
    char cmd[1024];

    av_log(s, AV_LOG_DEBUG, "hello state=%d\n", rt->state);
    rt->nb_byes = 0;

    if (!(rt->server_type == RTSP_SERVER_REAL && rt->need_subscription)) {
        if (rt->transport == RTSP_TRANSPORT_RTP) {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTSPStream *rtsp_st = rt->rtsp_streams[i];
                RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
                if (!rtpctx)
                    continue;
                ff_rtp_reset_packet_queue(rtpctx);
                rtpctx->last_rtcp_ntp_time  = AV_NOPTS_VALUE;
                rtpctx->first_rtcp_ntp_time = AV_NOPTS_VALUE;
                rtpctx->base_timestamp      = 0;
                rtpctx->timestamp           = 0;
                rtpctx->unwrapped_timestamp = 0;
                rtpctx->rtcp_ts_offset      = 0;
            }
        }
        if (rt->state == RTSP_STATE_PAUSED) {
            cmd[0] = 0;
        } else {
            snprintf(cmd, sizeof(cmd),
                     "Range: npt=%"PRId64".%03"PRId64"-\r\n",
                     rt->seek_timestamp / AV_TIME_BASE,
                     rt->seek_timestamp / (AV_TIME_BASE / 1000) % 1000);
        }
        ff_rtsp_send_cmd(s, "PLAY", rt->control_uri, cmd, reply, NULL);
        if (reply->status_code != RTSP_STATUS_OK) {
            return -1;
        }
        if (rt->transport == RTSP_TRANSPORT_RTP &&
            reply->range_start != AV_NOPTS_VALUE) {
            for (i = 0; i < rt->nb_rtsp_streams; i++) {
                RTSPStream *rtsp_st = rt->rtsp_streams[i];
                RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
                AVStream *st = NULL;
                if (!rtpctx || rtsp_st->stream_index < 0)
                    continue;
                st = s->streams[rtsp_st->stream_index];
                rtpctx->range_start_offset =
                    av_rescale_q(reply->range_start, AV_TIME_BASE_Q,
                                 st->time_base);
            }
        }
    }
    rt->state = RTSP_STATE_STREAMING;
    return 0;
}
static int ffurl_open_only(URLContext **puc, const char *filename, int flags,
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

int rtsp_init_output_context(CAM_CTX *ctx)
{
    AVOutputFormat      *out_fmt;
    ctx->ofmt_ctx = avformat_alloc_context();
    if(!ctx->ofmt_ctx){
        fprintf(stderr, "Allocate AVFormatContext failure\n");
        return -1;
    }
    out_fmt = av_guess_format(DEFAULT_OUTPUT_FORMAT, NULL, NULL);
    if(!out_fmt){
        fprintf(stderr, "AVOutputFormat %s not found\n", DEFAULT_OUTPUT_FORMAT);
        return -1;
    }
    ctx->ofmt_ctx->oformat = out_fmt;
    return 0;
}

int rtsp_init_context(CAM_CTX *ctx)
{
    int err = 0;
    int ret = 0;
    char tcpname[1024], control_uri[1024];
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
    rt->lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
    if((ret = ffurl_open_only(&rt->rtsp_hd, tcpname, AVIO_FLAG_READ_WRITE, &s->interrupt_callback, NULL))){
        av_log(s, AV_LOG_ERROR, "Unable to alloc rtsp_hd\n");
        return ret;
    }
    s->iformat = av_find_input_format("rtsp");

    rt->state       = RTSP_STATE_IDLE;
    rt->rtsp_hd_out = rt->rtsp_hd;
    rt->seq         = 0;
    TCPContext *tc = rt->rtsp_hd->priv_data;
    ff_url_join(rt->control_uri, sizeof(control_uri), "rtsp", NULL, ctx->host, ctx->port, "%s", ctx->path);
    tc->fd = ctx->fd;
    rt->rtsp_hd->is_connected = 1;
    rt->user_agent = strdup(USER_AGENT);
    rt->rtp_port_min = RTSP_RTP_PORT_MIN;
    rt->rtp_port_max = RTSP_RTP_PORT_MAX;
    rt->media_type_mask = (1 << (AVMEDIA_TYPE_DATA+1)) - 1;
#ifdef DEBUG
    int tcp_fd = ffurl_get_file_handle(rt->rtsp_hd);
    printf("tcp_fd:%d\n", tcp_fd);
#endif
fail:
    return err;
}

int rtsp_progress(CAM_CTX *ctx)
{
    int lower_transport_mask = 0;
    lower_transport_mask = 1 << RTSP_LOWER_TRANSPORT_TCP;
    int err;
    AVFormatContext *s = ctx->fmt_ctx;
    RTSPState *rt =  s->priv_data;
    RTSPMessageHeader reply1 = {0}, *reply = &reply1;
    char real_challenge[64] = "";
    char cmd[2048];
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
    av_dump_format(s, 0, rt->control_uri, 0);
    rt->lower_transport_mask = lower_transport_mask;
    av_strlcpy(rt->real_challenge, real_challenge, sizeof(rt->real_challenge));
    rt->state = RTSP_STATE_IDLE;
    rt->seek_timestamp = 0;
    return 0;
fail:
	return -1;
}
void clear_buf(int fd)
{
    char buf[16 * 1024];
    int len = read(fd, buf, 16*1024);
    printf("packet coming %d\n", len);
}


int rtsp_pack_ts_open(CAM_CTX *ctx, char *filename)
{
    if(!ctx || !filename) return -1;
    AVFormatContext *s = ctx->ofmt_ctx;
    if(!s) {
        fprintf(stderr, "AVOutputFormat empty\n");
        return -1;
    }
    if( avio_open(&s->pb, filename, AVIO_FLAG_WRITE) < 0){
        fprintf(stderr, "Output file open error\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stdout, "Open output TS %s success\n", filename);
#endif
    return 0;
}
int rtsp_pack_ts_close(CAM_CTX *ctx)
{
    AVFormatContext *s = ctx->ofmt_ctx;
    avio_flush(s->pb);
    avio_close(s->pb);
}

void rtsp_frame_handler(CAM_CTX *ctx){
    AVFormatContext *s = ctx->fmt_ctx;
    AVFrame *frame = NULL;
    AVCodecContext *codectx = rtsp_find_video_stream_avcodecctx(s);
    frame = av_frame_alloc();
    if(frame) av_free(frame);
}

int main()
{
	av_register_all();
	avformat_network_init();
	CAM_CTX *ctx = malloc(sizeof(CAM_CTX));
	if(!ctx) return -1;

    sprintf(ctx->host, "10.211.55.2");
    ctx->port = 8554;
    sprintf(ctx->path, "/demo.264");

	int fd = noly_tcp_connect(ctx->host, ctx->port);
    if(fd) {
		printf("Connected\n");
        ctx->fd = fd;
        rtsp_init_context(ctx);
        rtsp_init_output_context(ctx);
        rtsp_progress(ctx);
        //PLAY
        int ret = 0;
        fd_set fs;
        rtsp_stream_play(ctx->fmt_ctx);
        while(1){
            FD_ZERO(&fs);
            FD_SET(fd, &fs);
            ret = select(fd+1, &fs, NULL, NULL, NULL);
            if(ret > 0){
                if(FD_ISSET(fd, &fs)){
                    clear_buf(fd);
                }
            }
        }
        rtsp_stream_pause(ctx->fmt_ctx);
        rtsp_stream_close(ctx->fmt_ctx);
      	close(fd);
    }
	return 0;
}
