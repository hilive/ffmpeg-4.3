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
