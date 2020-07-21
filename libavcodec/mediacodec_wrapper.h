/*
 * Android MediaCodec Wrapper
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_MEDIACODEC_WRAPPER_H
#define AVCODEC_MEDIACODEC_WRAPPER_H

#include <stdint.h>
#include <sys/types.h>
#include "avcodec.h"

/**
 * The following API around MediaCodec and MediaFormat is based on the
 * NDK one provided by Google since Android 5.0.
 *
 * Differences from the NDK API:
 *
 * Buffers returned by ff_AMediaFormat_toString and ff_AMediaFormat_getString
 * are newly allocated buffer and must be freed by the user after use.
 *
 * The MediaCrypto API is not implemented.
 *
 * ff_AMediaCodec_infoTryAgainLater, ff_AMediaCodec_infoOutputBuffersChanged,
 * ff_AMediaCodec_infoOutputFormatChanged, ff_AMediaCodec_cleanOutputBuffers
 * ff_AMediaCodec_getName and ff_AMediaCodec_getBufferFlagEndOfStream are not
 * part of the original NDK API and are convenience functions to hide JNI
 * implementation.
 *
 * The API around MediaCodecList is not part of the NDK (and is lacking as
 * we still need to retrieve the codec name to work around faulty decoders
 * and encoders).
 *
 * For documentation, please refers to NdkMediaCodec.h NdkMediaFormat.h and
 * http://developer.android.com/reference/android/media/MediaCodec.html.
 *
 */

int ff_AMediaCodecProfile_getProfileFromAVCodecContext(AVCodecContext *avctx);

char *ff_AMediaCodecList_getCodecNameByType(const char *mime, int profile, int encoder, void *log_ctx);

struct FFAMediaFormat;
typedef struct FFAMediaFormat FFAMediaFormat;

FFAMediaFormat *ff_AMediaFormat_new(void);
int ff_AMediaFormat_delete(FFAMediaFormat* format);

char* ff_AMediaFormat_toString(FFAMediaFormat* format);

int ff_AMediaFormat_getInt32(FFAMediaFormat* format, const char *name, int32_t *out);
int ff_AMediaFormat_getInt64(FFAMediaFormat* format, const char *name, int64_t *out);
int ff_AMediaFormat_getFloat(FFAMediaFormat* format, const char *name, float *out);
int ff_AMediaFormat_getBuffer(FFAMediaFormat* format, const char *name, void** data, size_t *size);
int ff_AMediaFormat_getString(FFAMediaFormat* format, const char *name, const char **out);

void ff_AMediaFormat_setInt32(FFAMediaFormat* format, const char* name, int32_t value);
void ff_AMediaFormat_setInt64(FFAMediaFormat* format, const char* name, int64_t value);
void ff_AMediaFormat_setFloat(FFAMediaFormat* format, const char* name, float value);
void ff_AMediaFormat_setString(FFAMediaFormat* format, const char* name, const char* value);
void ff_AMediaFormat_setBuffer(FFAMediaFormat* format, const char* name, void* data, size_t size);

struct FFAMediaCodec;
typedef struct FFAMediaCodec FFAMediaCodec;
typedef struct FFAMediaCodecCryptoInfo FFAMediaCodecCryptoInfo;

struct FFAMediaCodecBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
};
typedef struct FFAMediaCodecBufferInfo FFAMediaCodecBufferInfo;

char *ff_AMediaCodec_getName(FFAMediaCodec *codec);

FFAMediaCodec* ff_AMediaCodec_createCodecByName(const char *name);
FFAMediaCodec* ff_AMediaCodec_createDecoderByType(const char *mime_type);
FFAMediaCodec* ff_AMediaCodec_createEncoderByType(const char *mime_type);

int ff_AMediaCodec_configure(FFAMediaCodec* codec, const FFAMediaFormat* format, void* surface, void *crypto, uint32_t flags);
int ff_AMediaCodec_start(FFAMediaCodec* codec);
int ff_AMediaCodec_stop(FFAMediaCodec* codec);
int ff_AMediaCodec_flush(FFAMediaCodec* codec);
int ff_AMediaCodec_delete(FFAMediaCodec* codec);

uint8_t* ff_AMediaCodec_getInputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size);
uint8_t* ff_AMediaCodec_getOutputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size);

ssize_t ff_AMediaCodec_dequeueInputBuffer(FFAMediaCodec* codec, int64_t timeoutUs);
int ff_AMediaCodec_queueInputBuffer(FFAMediaCodec* codec, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags);

ssize_t ff_AMediaCodec_dequeueOutputBuffer(FFAMediaCodec* codec, FFAMediaCodecBufferInfo *info, int64_t timeoutUs);
FFAMediaFormat* ff_AMediaCodec_getOutputFormat(FFAMediaCodec* codec);

int ff_AMediaCodec_releaseOutputBuffer(FFAMediaCodec* codec, size_t idx, int render);
int ff_AMediaCodec_releaseOutputBufferAtTime(FFAMediaCodec *codec, size_t idx, int64_t timestampNs);

int ff_AMediaCodec_infoTryAgainLater(FFAMediaCodec *codec, ssize_t idx);
int ff_AMediaCodec_infoOutputBuffersChanged(FFAMediaCodec *codec, ssize_t idx);
int ff_AMediaCodec_infoOutputFormatChanged(FFAMediaCodec *codec, ssize_t indx);

int ff_AMediaCodec_getBufferFlagCodecConfig (FFAMediaCodec *codec);
int ff_AMediaCodec_getBufferFlagEndOfStream(FFAMediaCodec *codec);
int ff_AMediaCodec_getBufferFlagKeyFrame(FFAMediaCodec *codec);

int ff_AMediaCodec_getConfigureFlagEncode(FFAMediaCodec *codec);

int ff_AMediaCodec_cleanOutputBuffers(FFAMediaCodec *codec);

int ff_Build_SDK_INT(AVCodecContext *avctx);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>


typedef enum OMX_COLOR_FORMATTYPE {
	OMX_COLOR_FormatUnused,
	OMX_COLOR_FormatMonochrome,
	OMX_COLOR_Format8bitRGB332,
	OMX_COLOR_Format12bitRGB444,
	OMX_COLOR_Format16bitARGB4444,
	OMX_COLOR_Format16bitARGB1555,
	OMX_COLOR_Format16bitRGB565,
	OMX_COLOR_Format16bitBGR565,
	OMX_COLOR_Format18bitRGB666,
	OMX_COLOR_Format18bitARGB1665,
	OMX_COLOR_Format19bitARGB1666,
	OMX_COLOR_Format24bitRGB888,
	OMX_COLOR_Format24bitBGR888,
	OMX_COLOR_Format24bitARGB1887,
	OMX_COLOR_Format25bitARGB1888,
	OMX_COLOR_Format32bitBGRA8888,
	OMX_COLOR_Format32bitARGB8888,
	OMX_COLOR_FormatYUV411Planar,
	OMX_COLOR_FormatYUV411PackedPlanar,
	OMX_COLOR_FormatYUV420Planar,
	OMX_COLOR_FormatYUV420PackedPlanar,
	OMX_COLOR_FormatYUV420SemiPlanar,
	OMX_COLOR_FormatYUV422Planar,
	OMX_COLOR_FormatYUV422PackedPlanar,
	OMX_COLOR_FormatYUV422SemiPlanar,
	OMX_COLOR_FormatYCbYCr,
	OMX_COLOR_FormatYCrYCb,
	OMX_COLOR_FormatCbYCrY,
	OMX_COLOR_FormatCrYCbY,
	OMX_COLOR_FormatYUV444Interleaved,
	OMX_COLOR_FormatRawBayer8bit,
	OMX_COLOR_FormatRawBayer10bit,
	OMX_COLOR_FormatRawBayer8bitcompressed,
	OMX_COLOR_FormatL2,
	OMX_COLOR_FormatL4,
	OMX_COLOR_FormatL8,
	OMX_COLOR_FormatL16,
	OMX_COLOR_FormatL24,
	OMX_COLOR_FormatL32,
	OMX_COLOR_FormatYUV420PackedSemiPlanar,
	OMX_COLOR_FormatYUV422PackedSemiPlanar,
	OMX_COLOR_Format18BitBGR666,
	OMX_COLOR_Format24BitARGB6666,
	OMX_COLOR_Format24BitABGR6666,
	OMX_COLOR_FormatKhronosExtensions = 0x6F000000, 
	OMX_COLOR_FormatVendorStartUnused = 0x7F000000, 
	OMX_COLOR_TI_FormatYUV420PackedSemiPlanar = 0x7f000100,
	OMX_COLOR_QCOM_FormatYUV420SemiPlanar = 0x7fa30c00,
	OMX_COLOR_FormatMax = 0x7FFFFFFF
} OMX_COLOR_FORMATTYPE;

enum FFMediaCodecColorFormat {
    COLOR_FormatYUV420Planar                              = 0x13,
    COLOR_FormatYUV420PackedPlanar                        = 0x14,
    COLOR_FormatYUV420SemiPlanar                          = 0x15,
    COLOR_FormatYCbYCr                                    = 0x19,
    COLOR_FormatYUV420PackedSemiPlanar                    = 0x27,
    COLOR_FormatAndroidOpaque                             = 0x7F000789,
    COLOR_FormatYUV422Flexible                            = 0x7f422888,
    COLOR_FormatYUV444Flexible                            = 0x7f444888,
    COLOR_QCOM_FormatYUV420SemiPlanar                     = 0x7fa30c00,
    COLOR_QCOM_FormatYUV420SemiPlanar32m                  = 0x7fa30c04,
    COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7fa30c03,
    COLOR_TI_FormatYUV420PackedSemiPlanar                 = 0x7f000100,
    COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced       = 0x7f000001,
    COLOR_FormatRGBFlexible                               = 0x7f36b888,
    COLOR_Format24bitBGR888 = 0x0000000c,
    COLOR_Format32bitABGR8888 = 0x7f00a000,
    COLOR_FormatRGBAFlexible = 0x7f36a888,
    COLOR_Format16bitRGB565 = 0x00000006,
    COLOR_FormatSurface = 0x7f000789,
};

/*
enum {
	AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4,
	AMEDIACODEC_CONFIGURE_FLAG_ENCODE = 1,
	AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3,
	AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2,
	AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1
};

typedef enum {
	AMEDIA_OK = 0,

	AMEDIA_ERROR_BASE = -10000,
	AMEDIA_ERROR_UNKNOWN = AMEDIA_ERROR_BASE,
	AMEDIA_ERROR_MALFORMED = AMEDIA_ERROR_BASE - 1,
	AMEDIA_ERROR_UNSUPPORTED = AMEDIA_ERROR_BASE - 2,
	AMEDIA_ERROR_INVALID_OBJECT = AMEDIA_ERROR_BASE - 3,
	AMEDIA_ERROR_INVALID_PARAMETER = AMEDIA_ERROR_BASE - 4,

	AMEDIA_DRM_ERROR_BASE = -20000,
	AMEDIA_DRM_NOT_PROVISIONED = AMEDIA_DRM_ERROR_BASE - 1,
	AMEDIA_DRM_RESOURCE_BUSY = AMEDIA_DRM_ERROR_BASE - 2,
	AMEDIA_DRM_DEVICE_REVOKED = AMEDIA_DRM_ERROR_BASE - 3,
	AMEDIA_DRM_SHORT_BUFFER = AMEDIA_DRM_ERROR_BASE - 4,
	AMEDIA_DRM_SESSION_NOT_OPENED = AMEDIA_DRM_ERROR_BASE - 5,
	AMEDIA_DRM_TAMPER_DETECTED = AMEDIA_DRM_ERROR_BASE - 6,
	AMEDIA_DRM_VERIFY_FAILED = AMEDIA_DRM_ERROR_BASE - 7,
	AMEDIA_DRM_NEED_KEY = AMEDIA_DRM_ERROR_BASE - 8,
	AMEDIA_DRM_LICENSE_EXPIRED = AMEDIA_DRM_ERROR_BASE - 9
} media_status_t;

extern const char* AMEDIAFORMAT_KEY_AAC_PROFILE;
extern const char* AMEDIAFORMAT_KEY_BIT_RATE;
extern const char* AMEDIAFORMAT_KEY_CHANNEL_COUNT;
extern const char* AMEDIAFORMAT_KEY_CHANNEL_MASK;
extern const char* AMEDIAFORMAT_KEY_COLOR_FORMAT;
extern const char* AMEDIAFORMAT_KEY_DURATION;
extern const char* AMEDIAFORMAT_KEY_FLAC_COMPRESSION_LEVEL;
extern const char* AMEDIAFORMAT_KEY_FRAME_RATE;
extern const char* AMEDIAFORMAT_KEY_HEIGHT;
extern const char* AMEDIAFORMAT_KEY_IS_ADTS;
extern const char* AMEDIAFORMAT_KEY_IS_AUTOSELECT;
extern const char* AMEDIAFORMAT_KEY_IS_DEFAULT;
extern const char* AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE;
extern const char* AMEDIAFORMAT_KEY_I_FRAME_INTERVAL;
extern const char* AMEDIAFORMAT_KEY_LANGUAGE;
extern const char* AMEDIAFORMAT_KEY_MAX_HEIGHT;
extern const char* AMEDIAFORMAT_KEY_MAX_INPUT_SIZE;
extern const char* AMEDIAFORMAT_KEY_MAX_WIDTH;
extern const char* AMEDIAFORMAT_KEY_MIME;
extern const char* AMEDIAFORMAT_KEY_PUSH_BLANK_BUFFERS_ON_STOP;
extern const char* AMEDIAFORMAT_KEY_REPEAT_PREVIOUS_FRAME_AFTER;
extern const char* AMEDIAFORMAT_KEY_SAMPLE_RATE;
extern const char* AMEDIAFORMAT_KEY_WIDTH;
extern const char* AMEDIAFORMAT_KEY_STRIDE;
*/
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif /* AVCODEC_MEDIACODEC_WRAPPER_H */
