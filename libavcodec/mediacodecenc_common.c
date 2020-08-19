#include "mediacodecenc_common.h"
#include <dlfcn.h>

#include "libavutil/avassert.h"
#include "libavutil/thread.h"

static const struct {
    enum AVPixelFormat pix_fmt;
    enum FFMediaCodecColorFormat clort_fmt;
} pix_fmt_map[] = {
    { AV_PIX_FMT_NV12,     COLOR_FormatYUV420SemiPlanar },
    { AV_PIX_FMT_YUV420P,  COLOR_FormatYUV420Planar },
    { AV_PIX_FMT_YUV422P,  COLOR_FormatYUV422Flexible },
    { AV_PIX_FMT_YUV444P,  COLOR_FormatYUV444Flexible },
    { AV_PIX_FMT_RGB8,     COLOR_FormatRGBFlexible },
    { AV_PIX_FMT_BGR24,    COLOR_Format24bitBGR888 },
    { AV_PIX_FMT_ABGR,     COLOR_Format32bitABGR8888 },
    { AV_PIX_FMT_RGBA,     COLOR_FormatRGBAFlexible },
    { AV_PIX_FMT_RGB565BE, COLOR_Format16bitRGB565 },
    { AV_PIX_FMT_NONE,     COLOR_FormatSurface },
};

int ff_mediacodec_get_color_format(enum AVPixelFormat lav)
{
    unsigned i;
    for (i = 0; pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (pix_fmt_map[i].pix_fmt == lav)
            return pix_fmt_map[i].clort_fmt;
    }

    return COLOR_FormatSurface;
}

enum AVPixelFormat ff_mediacodec_get_pix_fmt(enum FFMediaCodecColorFormat ndk)
{
    unsigned i;
    for (i = 0; pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (pix_fmt_map[i].clort_fmt == ndk)
            return pix_fmt_map[i].pix_fmt;
    }
    return AV_PIX_FMT_NONE;
}

int mediacodec_encode_fill_format(AVCodecContext* avctx, AMediaFormat* format) {
    int ret = AVERROR_BUG;

    do {
        if (!avctx || !format) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d parmas error!", __FUNCTION__, __LINE__);
            break;
        }

        if (avctx->codec_id == AV_CODEC_ID_H264) {//暂时只开放 avc, 后续验证 hevc
            AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
        } else {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
            break;
        }

        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, avctx->height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, avctx->width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, avctx->bit_rate);
        AMediaFormat_setFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, av_q2d(avctx->framerate));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, ff_mediacodec_get_color_format(avctx->pix_fmt));
        AMediaFormat_setInt32(format, "bitrate-mode", MEDIACODEC_BITRATE_MODE_VBR);
        ret = 0;
    } while (false);

    return ret;
}

int mediacodec_encode_header(AVCodecContext* avctx, AMediaFormat* format) {
    AMediaCodec* codec = NULL;
    int ret = AVERROR_BUG;
    media_status_t status = AMEDIA_OK;
    const char* mime = NULL;
    const char* format_str = NULL;

    do {
        if (!avctx || !format) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d parmas error!", __FUNCTION__, __LINE__);
            break;
        }

        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d get mime failed!", __FUNCTION__, __LINE__);
            break;
        }

        if (!(codec = AMediaCodec_createEncoderByType(mime))) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_createEncoderByType (%s) failed!", __FUNCTION__, __LINE__, mime);
            break;
        }

        format_str = AMediaFormat_toString(format);

        hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d format %s", __FUNCTION__, __LINE__, format_str ? format_str : "");

        if ((status = AMediaCodec_configure(codec, format, NULL, 0, AMEDIACODEC_CONFIGURE_FLAG_ENCODE)) != AMEDIA_OK) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_configure failed (%d)!", __FUNCTION__, __LINE__, status);
            break;
        }

        if ((status = AMediaCodec_start(codec))) {
            hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_start failed (%d)!", __FUNCTION__, __LINE__, status);
            break;
        }

        int try_times = 5;
        bool got_config = false;
        while (!got_config && try_times --) {
            {//input buff
                hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d try_times: %d", __FUNCTION__, __LINE__, try_times);

                ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(codec, MEDIACODEC_TIMEOUT_USEC);
                if (bufferIndex < 0) {
                    hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_dequeueInputBuffer failed (%d)!", __FUNCTION__, __LINE__, bufferIndex);
                    break;
                }

                size_t bufferSize = 0;
                uint8_t* buffer = AMediaCodec_getInputBuffer(codec, bufferIndex, &bufferSize);
                if (!buffer) {
                    hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_getInputBuffer failed!", __FUNCTION__, __LINE__);
                    break;
                }

                int status = AMediaCodec_queueInputBuffer(codec, bufferIndex, 0, bufferSize, 0, 0);
                hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_queueInputBuffer failed (%d)!", __FUNCTION__, __LINE__, status);
            }

            {//output buff
                int get_times = 3;
                while (get_times --) {
                    hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d get_times: %d", __FUNCTION__, __LINE__, get_times);

                    AMediaCodecBufferInfo bufferInfo;
                    int bufferIndex = AMediaCodec_dequeueOutputBuffer(codec, &bufferInfo, MEDIACODEC_TIMEOUT_USEC);
                    if (bufferIndex < 0) {
                        hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_dequeueOutputBuffer failed (%d)!", __FUNCTION__, __LINE__, bufferIndex);
                        continue;
                    }

                    size_t outSize = 0;
                    uint8_t* outBuffer = AMediaCodec_getOutputBuffer(codec, bufferIndex, &outSize);
                    if (!outBuffer) {
                        hi_loge(avctx, MEDIACODEC_LOG_TAG, "%s %d AMediaCodec_getOutputBuffer failed!", __FUNCTION__, __LINE__);
                        AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
                        break;
                    }

                    uint8_t* data = outBuffer;
                    uint32_t dataSize = bufferInfo.size;

                    hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d idx: %d outsize: %u flags: %d offset: %d size: %d pts: %lld nalu: [%x %x %x %x %x %x]", 
                        __FUNCTION__, __LINE__, bufferIndex, outSize, bufferInfo.flags, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, 
                        data[0], data[1], data[2], data[3], data[4], data[5]);

                    if (bufferInfo.flags & MEDIACODEC_BUFFER_FLAG_CODECCONFIG) {
                        hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d Got extradata of size %d ", __FUNCTION__, __LINE__, dataSize);

                        if (!avctx->extradata) {
                            avctx->extradata = av_mallocz(bufferInfo.size + AV_INPUT_BUFFER_PADDING_SIZE);
                            avctx->extradata_size = bufferInfo.size;    
                            memcpy(avctx->extradata, data, avctx->extradata_size);
                        }

                        got_config = true;
                        AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
                        break;
                    }

                    AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
                }
            }
        }

        if (!got_config) {
            break;
        }

        ret = 0;
    } while (false);

    if (codec) {
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
    }

    hi_logi(avctx, MEDIACODEC_LOG_TAG, "%s %d ret: %d", __FUNCTION__, __LINE__, ret);
    return ret;
}
