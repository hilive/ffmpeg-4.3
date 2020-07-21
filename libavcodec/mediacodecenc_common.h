

#ifndef AVCODEC_MEDIACODEC_ENC_COMMON_H
#define AVCODEC_MEDIACODEC_ENC_COMMON_H

#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"
#include "avcodec.h"
#include "mediacodec.h"
#include "mediacodec_wrapper.h"


int ff_mediacodec_get_color_format(enum AVPixelFormat lav);
enum AVPixelFormat ff_mediacodec_get_pix_fmt(enum FFMediaCodecColorFormat ndk);

#endif