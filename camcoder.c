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
#include "libavutil/audio_fifo.h"
#include "libavutil/frame.h"
#include "libavutil/timestamp.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "avformat.h"
#include "avio_internal.h"
#include "rtsp.h"
#define USER_AGENT "Camcoder-0.1"

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

static double pts2time(int64_t ts, AVRational *tb){
    return (av_q2d(*tb) * ts);
}
static char *const get_error_text(const int error)
{
    static char error_buffer[255];
    av_strerror(error, error_buffer, sizeof(error_buffer));
    return error_buffer;
}

static AVCodecContext *rtsp_find_video_stream_avcodecctx(AVFormatContext *s){
    int i;
    for (i = 0; i < s->nb_streams; i++)
    {
        if (s->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            return s->streams[i]->codec;
        }
    }
    return NULL;
}

static AVCodec *rtsp_find_video_stream_decoder(AVFormatContext *s){
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



static int rtsp_init_output_context(CAM_CTX *ctx)
{
    int idx = 0;
    AVFormatContext     *i = ctx->fmt_ctx;
    ctx->ofmt_ctx = avformat_alloc_context();
    AVFormatContext     *o = ctx->ofmt_ctx;
    AVCodecContext      *occ;
    AVOutputFormat      *out_fmt;
    AVStream            *vs;
    //AVStream            *as;
    if(!o){
        fprintf(stderr, "Allocate AVFormatContext failure\n");
        return -1;
    }
    out_fmt = av_guess_format(DEFAULT_OUTPUT_FORMAT, NULL, NULL);
    if(!out_fmt){
        fprintf(stderr, "AVOutputFormat %s not found\n", DEFAULT_OUTPUT_FORMAT);
        return -1;
    }
    o->oformat = out_fmt;
    for(idx = 0; idx < i->nb_streams ; idx++){
            AVCodecContext *icc = i->streams[idx]->codec;
            vs = avformat_new_stream(o, NULL);
            if(vs){
                occ = vs->codec;
                uint64_t extra_size = (uint64_t) icc->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;
                occ->codec_id       = icc->codec_id;
                occ->codec_type     = icc->codec_type;
                //occ->codec_tag = icc->codec_tag;
                //TODO check codec_tag
                occ->bit_rate       = icc->bit_rate;
                occ->extradata      = av_mallocz(extra_size);
                occ->extradata_size = icc->extradata_size;
                if(!occ->extradata){
                    return AVERROR(ENOMEM);
                }
                memcpy(occ->extradata, icc->extradata, icc->extradata_size);
                occ->bits_per_coded_sample  = icc->bits_per_coded_sample;
                occ->time_base              = i->streams[idx]->time_base;
            }
        switch (i->streams[idx]->codec->codec_type){
            case AVMEDIA_TYPE_VIDEO:
                ctx->video_idx = idx;
                //i->streams[idx]->discard = AVDISCARD_NONE;
                occ->pix_fmt                = icc->pix_fmt;
                //occ->width                  = icc->width;
                //occ->height                 = icc->height;
                //FIXME
                occ->width                  = 640;
                occ->height                 = 480;
                occ->has_b_frames           = icc->has_b_frames;
printf("add video stream %d x %d\n", occ->width, occ->height);
                break;
            case AVMEDIA_TYPE_AUDIO:
                ctx->audio_idx = idx;
                //i->streams[idx]->discard = AVDISCARD_NONE;
                occ->channel_layout         = icc->channel_layout;
                occ->sample_rate            = icc->sample_rate;
                occ->channels               = icc->channels;
                occ->frame_size             = icc->frame_size;
                occ->audio_service_type     = icc->audio_service_type;
                occ->block_align            = icc->block_align;
                occ->delay                  = icc->delay;
printf("add audio stream\n");
                break;
            default:
                i->streams[idx]->discard = AVDISCARD_ALL;
                break;
        }
    }
    return 0;
}

static int rtsp_init_context(CAM_CTX *ctx)
{
    int err = 0;
    int ret = 0;
    char tcpname[1024], control_uri[1024];
    //Default enable recording
    ctx->record_enable = 1;
    //Allocate AVFormatContext
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
    printf("FD:%d assigned to %s\n", tcp_fd, rt->control_uri);
#endif
    s->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;

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
#ifdef DEBUG
    av_dump_format(s, 0, rt->control_uri, 0);
    printf("Keepalive timeout %d\n", rt->timeout);
#endif
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

void rtsp_ts_file_name(char *mac, char *filename, int len)
{
    if(!filename || !mac) return;

    time_t rawtime;
    struct tm * timeinfo;
    char timebuf[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime (timebuf,80,"%F-%H%M%S",timeinfo);
    snprintf(filename, len, "/tmp/%s-%s.ts", mac, timebuf);
#ifdef DEBUG
    printf("file path: %s\n", filename);
#endif
}

void rtsp_thumbnail_file_name(char *mac, char *filename, int len)
{
    if(!filename || !mac) return;

    time_t rawtime;
    struct tm * timeinfo;
    char timebuf[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime (timebuf,80,"%F-%H%M%S",timeinfo);
    snprintf(filename, len, "/tmp/%s-%s.jpg", mac, timebuf);
#ifdef DEBUG
    printf("file path: %s\n", filename);
#endif
}

static int rtsp_pack_ts_open(CAM_CTX *ctx, char *filename)
{
    int error;
    //AVFormatContext     *s              = ctx->fmt_ctx;
    AVFormatContext     *d              = ctx->ofmt_ctx;
    AVIOContext         *out_io_ctx     = NULL;
    //AVCodecContext      *out_codec_ctx  = NULL;
    //AVCodec             *out_codec      = NULL;
    AVDictionary        *avdct          = NULL;
    if ((error = avio_open(&out_io_ctx, filename,
                           AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open output file '%s' (error '%s')\n",
                filename, get_error_text(error));
        goto error;
    }
    d->pb = out_io_ctx;
    if((error = avformat_write_header(d, &avdct)) < 0){
        fprintf(stderr, "Could not write header for output file (%s)\n", av_err2str(error));
        goto cleanup;
    }
    return 0;
cleanup:
    avio_close(d->pb);
error:
    return -1;
}

static int rtsp_pack_ts_check(CAM_CTX *ctx)
{
    if(!ctx) return -1;
    AVFormatContext *d = ctx->ofmt_ctx;
    if(!d) return -1;
    if(!d->pb){//If file not exist create it.
        rtsp_ts_file_name(ctx->id, ctx->out_file, DEFAULT_FILE_LEN);
        rtsp_pack_ts_open(ctx, ctx->out_file);
        fprintf(stdout, "MPEG-TS file %s created\n", ctx->out_file);
        return 1;
    }
    return 0;
}

static int rtsp_pack_ts_close(CAM_CTX *ctx)
{
    AVFormatContext *d = ctx->ofmt_ctx;
    avio_flush(d->pb);
    avio_close(d->pb);
    d->pb = NULL;
    return 0;
}
static int create_thumbnail(CAM_CTX *ctx, AVCodecContext *rtsp_ctx, AVFrame *frame)
{
    int     ret;
    char    filename[128];
    rtsp_thumbnail_file_name(ctx->id, filename, 128);
    AVCodec *jpg_codec = NULL;
    AVCodecContext *jpg_ctx = NULL;
    jpg_codec = avcodec_find_encoder(CODEC_ID_MJPEG);
    if(!jpg_codec){
        fprintf(stderr, "MJPEG codec not found\n");
        goto err;
    }
    jpg_ctx = avcodec_alloc_context3(jpg_codec);
    if(!jpg_ctx){
        fprintf(stderr, "alloc MJPEG Context failure\n");
        goto err;
    }
    jpg_ctx->bit_rate = rtsp_ctx->bit_rate;
    jpg_ctx->width = 320;
    jpg_ctx->height = 180;
    jpg_ctx->pix_fmt = rtsp_ctx->pix_fmt;
    jpg_ctx->codec_id = CODEC_ID_MJPEG;
    jpg_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    jpg_ctx->time_base.num = rtsp_ctx->time_base.num;
    jpg_ctx->time_base.den = rtsp_ctx->time_base.den;
    jpg_ctx->qmin = jpg_ctx->qmax = 3;
    jpg_ctx->mb_lmin = jpg_ctx->lmin = jpg_ctx->qmin * FF_QP2LAMBDA;
    jpg_ctx->mb_lmax = jpg_ctx->qmax = jpg_ctx->qmax * FF_QP2LAMBDA;
    jpg_ctx->flags |= CODEC_FLAG_QSCALE;
    jpg_ctx->strict_std_compliance = -1;
    //open mjpeg codec
    if(avcodec_open2(jpg_ctx, jpg_codec, NULL) < 0){
        fprintf(stderr, "could not open codec\n");
        goto err;
    }
    //open jpeg file
    FILE *fp = fopen(filename, "wb");
    if(!fp){
        fprintf(stderr, "could not open jpg\n");
        goto err;
    }
    AVFrame *scale_frame = av_frame_alloc();
    if(!scale_frame) {
        goto err;
    }
    int size = avpicture_get_size(jpg_ctx->pix_fmt, jpg_ctx->width, jpg_ctx->height);
    uint8_t *pic_buf = malloc(size);
    if(!pic_buf) {
        goto err;
    }
    avpicture_fill((AVPicture *)scale_frame, pic_buf, jpg_ctx->pix_fmt, jpg_ctx->width, jpg_ctx->height);
    struct SwsContext *img_ctx;
    img_ctx = sws_getContext(rtsp_ctx->width, rtsp_ctx->height, rtsp_ctx->pix_fmt, jpg_ctx->width, jpg_ctx->height, rtsp_ctx->pix_fmt, SWS_BICUBIC,  NULL, NULL, NULL);
    sws_scale(img_ctx, frame->data, frame->linesize, 0, rtsp_ctx->height, scale_frame->data, scale_frame->linesize);
    sws_freeContext(img_ctx);


#if 0
    int outbuf_size = avpicture_get_size(jpg_ctx->pix_fmt, jpg_ctx->width, jpg_ctx->height);
    uint8_t *out_buf = (uint8_t *) av_malloc(outbuf_size* sizeof(uint8_t));
    int out_size = avcodec_encode_video(jpg_ctx, out_buf, outbuf_size, scale_frame);
    fwrite(out_buf, 1, out_size, fp);
#else
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    int got_out;
    ret = avcodec_encode_video2(jpg_ctx, &pkt, scale_frame, &got_out);
    if(ret < 0){
        fprintf(stderr, "avcode_encode_video2 encode jpeg failure\n");
        goto err;
    }
    if(got_out){
        fwrite(pkt.data, 1, pkt.size, fp);
        av_free_packet(&pkt);
    }
#endif
err:
    if(fp) fclose(fp);
    //if(out_buf) av_free(out_buf);
    if(scale_frame){
        av_free(scale_frame->data[0]);
        av_free(scale_frame);
    }
    if(jpg_ctx){
        avcodec_close(jpg_ctx);
        av_free(jpg_ctx);
    }
    return 0;
}

static int rtsp_frame_handler(CAM_CTX *ctx){
    int ret = 0;
    //int *got_frame = 0;
    //AVFrame *frame = NULL;
    AVFormatContext *s = ctx->fmt_ctx;//src AVFormatContext
    AVFormatContext *d = ctx->ofmt_ctx;//dst AVFormatContext
    AVPacket pkt;
    int done = av_read_frame(s, &pkt);
    if(!done){
        if(pkt.stream_index != ctx->video_idx  && pkt.stream_index != ctx->audio_idx){
            //check packet is video / audio
            av_free_packet(&pkt);
            goto end;
        }
        if(ctx->record_enable == 0){
            av_free_packet(&pkt);
            //TODO pause this stream and close it now? not sure!!!
            rtsp_stream_pause(ctx->fmt_ctx);
            //rtsp_stream_close(ctx->fmt_ctx);
            goto end;
        }
        if((ret = rtsp_pack_ts_check(ctx)) >= 0){
            //Now is recording, save packet to file
            //reach video record file length
            if(ret == 1) {//new file
                ctx->video_start_pts = 0;
                ctx->first_frame_got = 0;
            }else{
                if(ctx->video_start_pts == 0){
                    ctx->video_start_pts = pts2time(pkt.pts, &s->streams[pkt.stream_index]->time_base);
                }
                double clip_duration = pts2time(pkt.pts, &s->streams[pkt.stream_index]->time_base) - ctx->video_start_pts;
                if(clip_duration >= ctx->video_duration){ // file length reache
                    printf("clip duraiton %0.3g close file\n", clip_duration);
                    rtsp_pack_ts_close(ctx);
                    av_free_packet(&pkt);
                    goto end;
                }
            }
#ifdef DEBUGFRAME
            av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
                    "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s size:%d\n",
                    av_get_media_type_string(s->streams[ctx->video_idx]->codec->codec_type),
                    av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &s->streams[pkt.stream_index]->time_base),
                    av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &s->streams[pkt.stream_index]->time_base),
                    pkt.size
            );
#endif
            //if (ctx->first_frame_got == 0 && pkt.stream_index == ctx->video_idx && pkt.flags & AV_PKT_FLAG_KEY){
                //create thumbnail
            if (pkt.stream_index == ctx->video_idx && pkt.flags & AV_PKT_FLAG_KEY){
#ifdef DEBUG
                fprintf(stdout, "Create thumbnail start\n");
#endif
                //AVCodecContext *codectx = rtsp_find_video_stream_avcodecctx(s);
                AVFrame *thumb = av_frame_alloc();
                //printf("%s%d\n",__FILE__,__LINE__);
                int got_pic = 0;
                int len = avcodec_decode_video2(ctx->video_ctx, thumb, &got_pic, &pkt);
                if(len < 0){
                    fprintf(stdout, "avcodec_decode_video2 decode failure\n");
                    goto write;
                }
                if(got_pic){
                    thumb->quality = 4;
                    thumb->pts = 0;
                    create_thumbnail(ctx, ctx->video_ctx, thumb);
                }
#ifdef DEBUG
                fprintf(stdout, "Create thumbnail end\n");
#endif
            }
write:
                //printf("Got key frame start write\n");
            if(ctx->first_frame_got == 0){
                if(pkt.pts != AV_NOPTS_VALUE){
                    ctx->first_frame_got = 1;
                }else{
                    printf("Skip first no pts frame\n");
                }
            }
            if(ctx->first_frame_got == 1 ){
#ifdef DEBUG
                fprintf(stdout, "av_interleaved_write_frame start\n");
#endif
                ret = av_interleaved_write_frame(d, &pkt);
                if(ret != 0){
                    fprintf(stderr, "write frame error %d\n", ret);
                }
#ifdef DEBUG
                fprintf(stdout, "av_interleaved_write_frame end\n");
#endif
            }
        }else{
            //open file error
            fprintf(stderr, "cannot open output media file");
            goto error;
        }
        av_free_packet(&pkt);
    }
#if 0
    AVCodecContext *codectx = rtsp_find_video_stream_avcodecctx(s);
    frame = av_frame_alloc();
    if(frame) av_free(frame);
#endif
end:
    return 0;
error:
    av_free_packet(&pkt);
    return -1;
}

void camera_context_init(CAM_CTX *ctx)
{
    memset(ctx, 0, sizeof(ctx));
    ctx->record_enable = 1;
    ctx->video_duration = 5; //Record to file every 5 second
}

int main()
{
    AVCodec *video_codec = NULL;
	av_register_all();
	avformat_network_init();
	CAM_CTX *ctx = malloc(sizeof(CAM_CTX));
    memset(ctx, 0,sizeof(ctx));
	if(!ctx) return -1;

    //sprintf(ctx->host, "192.168.15.136");
    sprintf(ctx->host, "10.211.55.2");
    ctx->port = 8554;
    //ctx->port = 8554;
    //sprintf(ctx->path, "/ChannelID=1&ChannelName=Channel1");
    sprintf(ctx->id, "112233445566");
    sprintf(ctx->path, "/demo.264");
    ctx->video_duration = 5;
	int fd = noly_tcp_connect(ctx->host, ctx->port);
    if(fd) {
		printf("Connected\n");
        ctx->fd = fd;
        rtsp_init_context(ctx);
        rtsp_progress(ctx);
        //PLAY
        int ret = 0;
        fd_set fs;
        rtsp_stream_play(ctx->fmt_ctx);
        if(avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0){
            fprintf(stdout, "cannot find stream info of %s\n", ctx->fmt_ctx->filename);
        }
        rtsp_init_output_context(ctx);
        ctx->video_ctx = rtsp_find_video_stream_avcodecctx(ctx->fmt_ctx);
        video_codec = avcodec_find_decoder(ctx->video_ctx->codec_id);
        if(avcodec_open2(ctx->video_ctx, video_codec, NULL) < 0){
            fprintf(stderr, "Cannot open video codec for thumbnail\n");
        }
        while(1){
            FD_ZERO(&fs);
            FD_SET(fd, &fs);
            ret = select(fd+1, &fs, NULL, NULL, NULL);
            if(ret > 0){
                if(FD_ISSET(fd, &fs)){
                    //clear_buf(fd);
                    if (rtsp_frame_handler(ctx) == -1){
                        rtsp_pack_ts_close(ctx);
                    }
                }
            }
        }
        rtsp_stream_pause(ctx->fmt_ctx);
        rtsp_stream_close(ctx->fmt_ctx);
      	close(fd);
    }else{
        printf("Connection failure\n");
    }
	return 0;
}
