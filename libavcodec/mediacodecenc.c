#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/pixfmt.h"
#include "internal.h"

#include "mediacodec.h"
#include "mediacodec_wrapper.h"
#include "mediacodecenc_common.h"

#define OFFSET(x) offsetof(MediaCodecEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

typedef struct MediaCodecEncContext {
    AVClass*  avclass;
    AMediaCodec* codec;
    AVFrame  frame;
    bool     saw_output_eos;
    int rc_mode;
    int width;
    int height;
} MediaCodecEncContext;


static const AVOption options[] = {
    { "rc-mode", "The bitrate mode to use", OFFSET(rc_mode), AV_OPT_TYPE_INT, { .i64 = MEDIACODEC_BITRATE_MODE_VBR }, MEDIACODEC_BITRATE_MODE_VBR, MEDIACODEC_BITRATE_MODE_CBR, VE, "rc_mode"},
//    { "cq", "Constant quality", 0, AV_OPT_TYPE_CONST, {.i64 = RC_MODE_CQ}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "vbr", "Variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = MEDIACODEC_BITRATE_MODE_VBR}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "cbr", "Constant bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = MEDIACODEC_BITRATE_MODE_VBR}, INT_MIN, INT_MAX, VE, "rc_mode" },
    { "mediacodec_output_size", "Temporary hack to support scaling on output", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.i64 = 0} , 48, 3840, AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static av_cold int mediacodec_encode_init(AVCodecContext* avctx) {
    int ret = AVERROR_BUG;
    media_status_t status = AMEDIA_OK;
    AMediaFormat* format = AMediaFormat_new();
    const char* mime = NULL;
    const char* format_str = NULL;
    MediaCodecEncContext* ctx = avctx->priv_data;
    ctx->saw_output_eos = false;

    do {
        hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d init start globalHdr: [%d %s] rc_mode: %d", __FUNCTION__, __LINE__,
            avctx->flags, (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? "yes" : "no", ctx->rc_mode);

        if ((ret = mediacodec_encode_fill_format(avctx, format)) != 0) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d mediacodec_encode_fill_format failed (%d)!", __FUNCTION__, __LINE__, ret);
            break;
        }

        if ((ret = mediacodec_encode_header(avctx, format)) != 0) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d mediacodec_encode_header failed (%d)!", __FUNCTION__, __LINE__, ret);
            break;
        }

        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaFormat_getString mime failed!", __FUNCTION__, __LINE__);
            break;
        }

        if (!(ctx->codec = AMediaCodec_createEncoderByType(mime))) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_createEncoderByType failed!", __FUNCTION__, __LINE__);
            break;
        }

        format_str = AMediaFormat_toString(format);
        hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_configure format %s!", __FUNCTION__, __LINE__, format_str ? format_str : "");

        if ((status = AMediaCodec_configure(ctx->codec, format, NULL, 0, AMEDIACODEC_CONFIGURE_FLAG_ENCODE))) {
            ret = AVERROR(EINVAL);
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_configure failed (%d) !", __FUNCTION__, __LINE__, status);
            break;
        }

        if ((status = AMediaCodec_start(ctx->codec))) {
            ret = AVERROR(EIO);
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_start failed (%d) !", __FUNCTION__, __LINE__, status);
            break;
        }

        ret = 0;
    } while (0);

    if (format) {
        AMediaFormat_delete(format);
        format = NULL;
    }

    hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d init ret (%d)", __FUNCTION__, __LINE__, ret);
    return ret;
}

static int mediacodec_encode_send_frame(AVCodecContext* avctx, const AVFrame* frame) {
    MediaCodecEncContext* ctx = avctx->priv_data;

    if (ctx->saw_output_eos) {
        return AVERROR_EOF;
    }

    ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(ctx->codec, MEDIACODEC_TIMEOUT_USEC);
    if (bufferIndex < 0) {
        hi_logd(avctx, MEDIACODEC_LOG_TAG, "%s %d No input buffers available (%d)", __FUNCTION__, __LINE__, bufferIndex);
        return AVERROR(EIO);
    }

    if (!frame) {
        AMediaCodec_queueInputBuffer(ctx->codec, bufferIndex, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM", __FUNCTION__, __LINE__);
        return 0;
    }

    size_t bufferSize = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(ctx->codec, bufferIndex, &bufferSize);
    if (!buffer) {
        hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_getInputBuffer failed idx: %d !", __FUNCTION__, __LINE__, bufferIndex);
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
        flags |= MEDIACODEC_BUFFER_FLAG_SYNCFRAME;
    }

    media_status_t status = AMediaCodec_queueInputBuffer(ctx->codec, bufferIndex, 0, copy_size, pts, flags);

    hi_logt(avctx, MEDIACODEC_LOG_TAG, "%s %d (%d %d), buffInfo (idx: %u size: %u flags: %d) frameInfo (width: %d height: %d format: %d pts: [%lld %lld] type: %d)", __FUNCTION__, __LINE__,
        ret, status, bufferIndex, bufferSize, flags, frame->width, frame->height, frame->format, frame->pts, pts, frame->pict_type);

    if (status != 0) {
        hi_loge(avctx, MEDIACODEC_LOG_TAG, "AMediaCodec_queueInputBuffer failed (%d)", status);
        return AVERROR(EAGAIN);
    }

    return 0;
}

static int mediacodec_encode_receive_packet(AVCodecContext* avctx, AVPacket* pkt) {
    MediaCodecEncContext* ctx = avctx->priv_data;

    int bufferIndex = -1;
    int ret = 0;

    do {
        if (ctx->saw_output_eos) {
            ret = AVERROR_EOF;
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d ", __FUNCTION__, __LINE__);
            break;
        }

        bool config_frame = false;
        bool key_frame = false;
        int64_t pts = 0;
        uint8_t* data = NULL;
        uint32_t data_size = 0;

        AMediaCodecBufferInfo bufferInfo;
        bufferIndex = AMediaCodec_dequeueOutputBuffer(ctx->codec, &bufferInfo, MEDIACODEC_TIMEOUT_USEC);
        if (bufferIndex < 0) {
            hi_logd(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_dequeueOutputBuffer idx: %d", __FUNCTION__, __LINE__, bufferIndex);
            ret = AVERROR(EAGAIN);
            break;
        }

        if (bufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            ctx->saw_output_eos = true;
            ret = AVERROR_EOF;
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d Got EOS at output", __FUNCTION__, __LINE__);
            break;
        }

        size_t buff_size = 0;
        uint8_t* buffer = AMediaCodec_getOutputBuffer(ctx->codec, bufferIndex, &buff_size);
        if (!buffer) {
            ret = AVERROR(EIO);
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_getOutputBuffer failed, flags: %d status: %d size: %u", __FUNCTION__, __LINE__, bufferInfo.flags, bufferIndex, buff_size);
            break;
        }

        config_frame = bufferInfo.flags & MEDIACODEC_BUFFER_FLAG_CODECCONFIG ? true : false;
        key_frame = bufferInfo.flags & MEDIACODEC_BUFFER_FLAG_SYNCFRAME ? true : false;

        data = buffer;
        data_size = bufferInfo.size;
        pts = av_rescale_q(bufferInfo.presentationTimeUs, AV_TIME_BASE_Q, avctx->time_base);

        hi_logd(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec OutputBuffer status: %d outsize: %u flags: %d offset: %d size: %d pts: [%lld %lld] nalu: [%x %x %x %x %x %x]", 
            __FUNCTION__, __LINE__, bufferIndex, buff_size, bufferInfo.flags, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, pts, data[0], data[1], data[2], data[3], data[4], data[5]);

        if ((ret = ff_alloc_packet2(avctx, pkt, data_size, data_size) < 0)) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d Failed to allocate packet: %d", __FUNCTION__, __LINE__, ret);
            ret = AVERROR(EIO);
            break;
        }

        memcpy(pkt->data, data, data_size);

        pkt->dts = pkt->pts = pts;

        if (config_frame || key_frame) {
            pkt->flags |= AV_PKT_FLAG_KEY;
        }
    } while (false);

    if (bufferIndex >= 0) {
        AMediaCodec_releaseOutputBuffer(ctx->codec, bufferIndex, false);
    }

    return ret;
}

static av_cold int mediacodec_encode_close(AVCodecContext *avctx) {
    MediaCodecEncContext* ctx = avctx->priv_data;

    hi_logi(avctx, MEDIACODEC_LOG_TAG, "mediacodec_encode_close");

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
