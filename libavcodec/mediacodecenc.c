#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/pixfmt.h"
#include "internal.h"

#include "mediacodec.h"
#include "mediacodec_wrapper.h"
#include "mediacodecenc_common.h"


#define LOG_TAG		"[mediacodecenc]"

#define LOCAL_BUFFER_FLAG_SYNCFRAME 1
#define LOCAL_BUFFER_FLAG_CODECCONFIG 2

#define TIMEOUT_USEC 10000

#define OFFSET(x) offsetof(MediaCodecEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

#define RC_MODE_CQ  0 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CQ
#define RC_MODE_VBR 1 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR
#define RC_MODE_CBR 2 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR

typedef struct MediaCodecEncContext {
    AVClass*  avclass;
    AMediaFormat* format;
    AMediaCodec* codec;
    AVFrame  frame;
    bool     saw_output_eos;
    int rc_mode;
    int width;
    int height;
} MediaCodecEncContext;


static const AVOption options[] = {
    { "rc-mode", "The bitrate mode to use", OFFSET(rc_mode), AV_OPT_TYPE_INT, { .i64 = RC_MODE_VBR }, RC_MODE_VBR, RC_MODE_CBR, VE, "rc_mode"},
//    { "cq", "Constant quality", 0, AV_OPT_TYPE_CONST, {.i64 = RC_MODE_CQ}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "vbr", "Variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = RC_MODE_VBR}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "cbr", "Constant bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = RC_MODE_CBR}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "mediacodec_output_size", "Temporary hack to support scaling on output", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.i64 = 0} , 48, 3840, AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static int mediacodec_encode_header(AVCodecContext* avctx) {
    MediaCodecEncContext* ctx = avctx->priv_data;
    int ret = AVERROR_BUG;

    do {
        media_status_t status = AMEDIA_OK;
        if ((status = AMediaCodec_configure(ctx->codec, ctx->format, NULL, 0, AMEDIACODEC_CONFIGURE_FLAG_ENCODE)) != AMEDIA_OK) {
            hi_logi(avctx, LOG_TAG, "%s %d status: %d", __FUNCTION__, __LINE__, status);
            break;
        }

        status = AMediaCodec_start(ctx->codec);
        if (status != AMEDIA_OK) {
            hi_logi(avctx, LOG_TAG, "%s %d status: %d", __FUNCTION__, __LINE__, status);
            break;
        }

        {//input buff
            ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(ctx->codec, TIMEOUT_USEC);
            if (bufferIndex < 0) {
                hi_logd(avctx, LOG_TAG, "No input buffers available");
                break;
            }

            size_t bufferSize = 0;
            uint8_t* buffer = AMediaCodec_getInputBuffer(ctx->codec, bufferIndex, &bufferSize);
            if (!buffer) {
                hi_loge(avctx, LOG_TAG, "Cannot get input buffer!");
                break;
            }

            AMediaCodec_queueInputBuffer(ctx->codec, bufferIndex, 0, bufferSize, 0, 0);
        }

        {//output buff
            bool got_config = false;
            int try_times = 3;
            while (try_times --) {
                AMediaCodecBufferInfo bufferInfo;
                int buff_idx = AMediaCodec_dequeueOutputBuffer(ctx->codec, &bufferInfo, TIMEOUT_USEC);
                if (buff_idx < 0) {
                    if (buff_idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                        hi_logi(avctx, LOG_TAG, "AMediaCodec_dequeueOutputBuffer idx: %d", buff_idx);
                    }

                    continue;
                }

                size_t outSize = 0;
                uint8_t* outBuffer = AMediaCodec_getOutputBuffer(ctx->codec, buff_idx, &outSize);
                if (!outBuffer) {
                    hi_loge(avctx, LOG_TAG, "AMediaCodec_getOutputBuffer failed, flags: %d idx: %d size: %u", bufferInfo.flags, buff_idx, outSize);
                    AMediaCodec_releaseOutputBuffer(ctx->codec, buff_idx, false);
                    break;
                }

                uint8_t* data = outBuffer;
                uint32_t dataSize = bufferInfo.size;

                hi_logd(avctx, LOG_TAG, "AMediaCodec OutputBuffer idx: %d outsize: %u flags: %d offset: %d size: %d pts: %lld nalu: [%x %x %x %x %x %x]", 
                    buff_idx, outSize, bufferInfo.flags, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, data[0], data[1], data[2], data[3], data[4], data[5]);

                if (bufferInfo.flags & LOCAL_BUFFER_FLAG_CODECCONFIG) {
                    hi_logi(avctx, LOG_TAG, "Got extradata of size %d ", dataSize);

                    if (!avctx->extradata) {
                        avctx->extradata = av_mallocz(bufferInfo.size + AV_INPUT_BUFFER_PADDING_SIZE);
                        avctx->extradata_size = bufferInfo.size;    
                        memcpy(avctx->extradata, data, avctx->extradata_size);
                    }

                    got_config = true;
                    AMediaCodec_releaseOutputBuffer(ctx->codec, buff_idx, false);
                    break;
                }

                AMediaCodec_releaseOutputBuffer(ctx->codec, buff_idx, false);
            }

            if (!got_config) {
                break;
            }
        }

        ret = 0;
    } while (false);

    AMediaCodec_stop(ctx->codec);

    hi_logi(avctx, LOG_TAG, "%s %d ret: %d", __FUNCTION__, __LINE__, ret);
    return ret;
}

static av_cold int mediacodec_encode_init(AVCodecContext* avctx) {
    int ret = 0;

    do {
        MediaCodecEncContext* ctx = avctx->priv_data;
        ctx->saw_output_eos = false;

        hi_logi(avctx, LOG_TAG, "init start globalHdr: [%d %s] rc_mode: %d", 
            avctx->flags, (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? "yes" : "no", ctx->rc_mode);

        ctx->format = AMediaFormat_new();
        if (!ctx->format) {
            ret = AVERROR_BUG;
            hi_loge(avctx, LOG_TAG, "AMediaFormat_new failed!");
            break;
        }

        const char* mime = "video/avc";

        AMediaFormat_setString(ctx->format, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(ctx->format, AMEDIAFORMAT_KEY_HEIGHT, avctx->height);
        AMediaFormat_setInt32(ctx->format, AMEDIAFORMAT_KEY_WIDTH, avctx->width);
        AMediaFormat_setInt32(ctx->format, AMEDIAFORMAT_KEY_BIT_RATE, avctx->bit_rate);
        AMediaFormat_setFloat(ctx->format, AMEDIAFORMAT_KEY_FRAME_RATE, av_q2d(avctx->framerate));
        AMediaFormat_setInt32(ctx->format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        AMediaFormat_setInt32(ctx->format, AMEDIAFORMAT_KEY_COLOR_FORMAT, ff_mediacodec_get_color_format(avctx->pix_fmt));
        AMediaFormat_setInt32(ctx->format, "bitrate-mode", 2);
        
        const char* format_str = AMediaFormat_toString(ctx->format);
        hi_logi(avctx, LOG_TAG, "AMediaCodec_configure %s!", format_str ? format_str : "");

        ctx->codec = AMediaCodec_createEncoderByType(mime);

        if (ctx->codec == NULL) {
            hi_loge(avctx, LOG_TAG, "AMediaCodec_createEncoderByType failed!");
            ret = AVERROR_EXTERNAL;
            break;
        }

        if ((ret = mediacodec_encode_header(avctx)) != 0) {
            hi_loge(avctx, LOG_TAG, "mediacodec_encode_header failed (%d) !", ret);
            break;
        }

        media_status_t status = AMediaCodec_configure(ctx->codec, ctx->format, NULL, 0, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        if (status != 0) {
            hi_loge(avctx, LOG_TAG, "AMediaCodec_configure failed (%d) !", status);
            ret = AVERROR(EINVAL);
            break;
        }

        status = AMediaCodec_start(ctx->codec);
        if (status != 0) {
            hi_loge(avctx, LOG_TAG, "AMediaCodec_configure failed (%d) !", status);
            ret = AVERROR(EIO);
            break;
        }
    } while (0);

    hi_logi(avctx, LOG_TAG, "%s %d init ret (%d)", __FUNCTION__, __LINE__, ret);
    return ret;
}

static int mediacodec_encode_send_frame(AVCodecContext* avctx, const AVFrame* frame) {
    MediaCodecEncContext* ctx = avctx->priv_data;

    if (ctx->saw_output_eos) {
        return AVERROR_EOF;
    }

    ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(ctx->codec, TIMEOUT_USEC);
    if (bufferIndex < 0) {
        hi_logd(avctx, LOG_TAG, "%s %d No input buffers available (%d)", __FUNCTION__, __LINE__, bufferIndex);
        return AVERROR(EIO);
    }

    if (!frame) {
        AMediaCodec_queueInputBuffer(ctx->codec, bufferIndex, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        hi_logi(avctx, LOG_TAG, "%s %d AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM", __FUNCTION__, __LINE__);
        return 0;
    }

    size_t bufferSize = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(ctx->codec, bufferIndex, &bufferSize);
    if (!buffer) {
        hi_loge(avctx, LOG_TAG, "%s %d AMediaCodec_getInputBuffer failed idx: %d !", __FUNCTION__, __LINE__, bufferIndex);
        return AVERROR_EXTERNAL;
    }

    int flags = 0;
    int64_t pts = av_rescale_q(frame->pts, avctx->time_base, AV_TIME_BASE_Q);
    size_t copy_size = bufferSize;

    int ret = av_image_copy_to_buffer(buffer, bufferSize, (const uint8_t **)frame->data,
                            frame->linesize, frame->format,
                            frame->width, frame->height, 1);

    if (ret > 0) {
        copy_size = ret;
    }

    if (frame->pict_type == AV_PICTURE_TYPE_I) {
        flags |= LOCAL_BUFFER_FLAG_SYNCFRAME;
    }

    media_status_t status = AMediaCodec_queueInputBuffer(ctx->codec, bufferIndex, 0, copy_size, pts, flags);

    hi_logt(avctx, LOG_TAG, "%s %d (%d %d), buffInfo (idx: %u size: %u flags: %d) frameInfo (width: %d height: %d format: %d pts: [%lld %lld] type: %d)", __FUNCTION__, __LINE__,
        ret, status, bufferIndex, bufferSize, flags, frame->width, frame->height, frame->format, frame->pts, pts, frame->pict_type);

    if (status != 0) {
        hi_loge(avctx, LOG_TAG, "AMediaCodec_queueInputBuffer failed (%d)", status);
        return AVERROR(EAGAIN);
    }

    return 0;
}

static int mediacodec_encode_receive_packet(AVCodecContext* avctx, AVPacket* pkt) {
    MediaCodecEncContext* ctx = avctx->priv_data;

    int buff_idx = -1;
    int ret = 0;

    do {
        if (ctx->saw_output_eos) {
            ret = AVERROR_EOF;
            hi_loge(avctx, LOG_TAG, "%s %d ", __FUNCTION__, __LINE__);
            break;
        }

        bool config_frame = false;
        bool key_frame = false;
        int64_t pts = 0;
        uint8_t* data = NULL;
        uint32_t data_size = 0;

        AMediaCodecBufferInfo bufferInfo;
        buff_idx = AMediaCodec_dequeueOutputBuffer(ctx->codec, &bufferInfo, TIMEOUT_USEC);
        if (buff_idx < 0) {
            if (buff_idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                hi_logi(avctx, LOG_TAG, "%s %d AMediaCodec_dequeueOutputBuffer idx: %d", __FUNCTION__, __LINE__, buff_idx);
            }

            ret = AVERROR(EAGAIN);
            break;
        }

        if (bufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            ctx->saw_output_eos = true;
            ret = AVERROR_EOF;
            hi_loge(avctx, LOG_TAG, "%s %d Got EOS at output", __FUNCTION__, __LINE__);
            break;
        }

        size_t buff_size = 0;
        uint8_t* buffer = AMediaCodec_getOutputBuffer(ctx->codec, buff_idx, &buff_size);
        if (!buffer) {
            ret = AVERROR(EIO);
            hi_loge(avctx, LOG_TAG, "%s %d AMediaCodec_getOutputBuffer failed, flags: %d status: %d size: %u", __FUNCTION__, __LINE__, bufferInfo.flags, buff_idx, buff_size);
            break;
        }

        config_frame = bufferInfo.flags & LOCAL_BUFFER_FLAG_CODECCONFIG ? true : false;
        key_frame = bufferInfo.flags & LOCAL_BUFFER_FLAG_SYNCFRAME ? true : false;

        data = buffer;
        data_size = bufferInfo.size;
        pts = av_rescale_q(bufferInfo.presentationTimeUs, AV_TIME_BASE_Q, avctx->time_base);

        hi_logd(avctx, LOG_TAG, "%s %d AMediaCodec OutputBuffer status: %d outsize: %u flags: %d offset: %d size: %d pts: [%lld %lld] nalu: [%x %x %x %x %x %x]", 
            __FUNCTION__, __LINE__, buff_idx, buff_size, bufferInfo.flags, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, pts, data[0], data[1], data[2], data[3], data[4], data[5]);

        if ((ret = ff_alloc_packet2(avctx, pkt, data_size, data_size) < 0)) {
            hi_loge(avctx, LOG_TAG, "%s %d Failed to allocate packet: %d", __FUNCTION__, __LINE__, ret);
            ret = AVERROR(EIO);
            break;
        }

        memcpy(pkt->data, data, data_size);

        pkt->dts = pkt->pts = pts;

        if (config_frame || key_frame) {
            pkt->flags |= AV_PKT_FLAG_KEY;
        }
    } while (false);

    if (buff_idx >= 0) {
        AMediaCodec_releaseOutputBuffer(ctx->codec, buff_idx, false);
    }

    return ret;
}

static av_cold int mediacodec_encode_close(AVCodecContext *avctx)
{
    hi_logi(avctx, LOG_TAG, "mediacodec_encode_close");

    MediaCodecEncContext* ctx = avctx->priv_data;

    if (ctx->format) {
        AMediaFormat_delete(ctx->format);
        ctx->format = NULL;
    }

    if (ctx->codec) {
        AMediaCodec_stop(ctx->codec);
        AMediaCodec_delete(ctx->codec);
        ctx->codec = NULL;
    }

    if (avctx->extradata) {
        av_freep(&avctx->extradata);
        avctx->extradata = NULL;
        avctx->extradata_size = 0;
    }

    return 0;
}

static const AVClass mediacodec_class = {
    .class_name = "h264_mediacodec_class",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_mediacodec_encoder = {
    .name = "h264_mediacodec",
    .long_name = NULL_IF_CONFIG_SMALL("h264 (MediaCodec NDK)"),
    .type = AVMEDIA_TYPE_VIDEO,
    .id = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(MediaCodecEncContext),
    .init = mediacodec_encode_init,
    .send_frame = mediacodec_encode_send_frame,
    .receive_packet = mediacodec_encode_receive_packet,
    .close = mediacodec_encode_close,
    .capabilities = AV_CODEC_CAP_DELAY,
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .priv_class = &mediacodec_class,
    .pix_fmts = (const enum AVPixelFormat[]){AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
    },
};
