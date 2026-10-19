/* GStreamer ToupCam Plugin
 * Copyright (C) 2022 Labsmore LLC
 *
 * Author John McMaster <johndmcmaster@gmail.com>
 * Author Kishore Arepalli <kishore.arepalli@gmail.com>
 */
/**
 * SECTION:element-gsttoupcamsrc
 *
 * The toupcamsrc element is a source for a USB 3 camera supported by the
 * ToupCam SDK. A live source, operating in push mode.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v toupcamsrc ! autovideosink
 * ]|
 * </refsect2>
 */

#include <string.h>             // for memcpy
#include <unistd.h>             // for usleep

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdint.h>

#include <stdlib.h>

#include "gsttoupcamsrc.h"

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC(gst_toupcam_src_debug);
#define GST_CAT_DEFAULT gst_toupcam_src_debug

/* prototypes */
static void gst_toupcam_src_set_property(GObject * object,
                                         guint property_id,
                                         const GValue * value,
                                         GParamSpec * pspec);
static void gst_toupcam_src_get_property(GObject * object,
                                         guint property_id, GValue * value,
                                         GParamSpec * pspec);
static void gst_toupcam_src_dispose(GObject * object);
static void gst_toupcam_src_finalize(GObject * object);

static gboolean gst_toupcam_src_start(GstBaseSrc * src);
static gboolean gst_toupcam_src_stop(GstBaseSrc * src);
static GstCaps *gst_toupcam_src_get_caps(GstBaseSrc * src,
                                         GstCaps * filter);
static GstCaps *gst_toupcam_src_fixate(GstBaseSrc * src, GstCaps * caps);
static gboolean gst_toupcam_src_set_caps(GstBaseSrc * src, GstCaps * caps);

static GstFlowReturn gst_toupcam_src_fill(GstPushSrc * src,
                                          GstBuffer * buf);
static GstFlowReturn gst_toupcam_src_alloc(GstPushSrc * psrc,
                                           GstBuffer ** buf);
void gst_toupcam_pdebug(GstToupCamSrc * src);

// static GstCaps *gst_toupcam_src_create_caps (GstToupCamSrc * src);
static void gst_toupcam_src_reset(GstToupCamSrc * src);
enum {
    PROP_0,

    //Camera agnostic properties
    PROP_SDK_BRAND,
    PROP_SDK_VERSION,

    //Fixed camera metadata
    PROP_SERIAL_NUMBER,
    PROP_FW_VERSION,
    PROP_HW_VERSION,
    PROP_PRODUCTION_DATE,
    PROP_FPGA_VERSION,

#if CAMSDK_VERSION >= 53
    //ToupcamModelV2
    PROP_MODEL_NAME,
    PROP_MODEL_FLAG,
    PROP_MODEL_MAXSPEED,
    PROP_MODEL_PREVIEW,
    PROP_MODEL_STILL,
    PROP_MODEL_MAXFANSPEED,
    PROP_MODEL_XPIXSZ,
    PROP_MODEL_YPIXSZ,
#endif

    PROP_REVISION,
    PROP_MAX_BIT_DEPTH,
    //Is this a monochrome sensor?
    PROP_MONO,
    //Bayer pattern as FourCC (ex: "RGGB"). Empty for mono
    PROP_RAW_FORMAT,
    //Significant bits per sample in the currently negotiated format
    PROP_BIT_DEPTH,
    //Exposure time in us
    PROP_EXPOSURE_TIME_MIN,
    PROP_EXPOSURE_TIME_MAX,
    PROP_EXPOSURE_TIME_DEFAULT,
    //Gain as whole number precent
    //ex: 100 => 1.0x, 500 => 5.0x
    PROP_EXPOSURE_AGAIN_MIN,
    PROP_EXPOSURE_AGAIN_MAX,
    PROP_EXPOSURE_AGAIN_DEFAULT,

    PROP_CAMERAPRESENT,
    PROP_ESIZE,
    PROP_HFLIP,
    PROP_VFLIP,
    PROP_AUTO_EXPOSURE,
    PROP_EXPOTIME,
    PROP_EXPOAGAIN,
    PROP_HUE,
    PROP_SATURATION,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
    PROP_GAMMA,

    PROP_BB_R,
    PROP_BB_G,
    PROP_BB_B,

    PROP_WB_R,
    PROP_WB_G,
    PROP_WB_B,

    PROP_AWB_RGB,
    PROP_AWB_TT,


};

#define DEFAULT_PROP_AUTO_EXPOSURE TRUE
#define DEFAULT_PROP_EXPOTIME 0
#define DEFAULT_PROP_EXPOAGAIN 100
#define MIN_PROP_EXPOTIME 0
// FIXME: GUI max is 15. However we will time out after 5 sec
#define MAX_PROP_EXPOTIME 5000000
/*
Around roughly 10,000 it starts to break
Not sure where the limit comes from
*/
#define MIN_PROP_EXPOAGAIN 100
#define MAX_PROP_EXPOAGAIN 16000
#define DEFAULT_PROP_HFLIP FALSE
#define DEFAULT_PROP_VFLIP FALSE
#define DEFAULT_PROP_HUE CAMSDK_(HUE_DEF)
#define DEFAULT_PROP_SATURATION CAMSDK_(SATURATION_DEF)
#define DEFAULT_PROP_BRIGHTNESS CAMSDK_(BRIGHTNESS_DEF)
#define DEFAULT_PROP_CONTRAST CAMSDK_(CONTRAST_DEF)
#define DEFAULT_PROP_GAMMA CAMSDK_(GAMMA_DEF)

// These don't have defined enums for some reason
#define GST_TOUPCAM_OPTION_BYTEORDER_RGB 0
#define GST_TOUPCAM_OPTION_BYTEORDER_BGR 1
// OPTION_RGB values
#define GST_TOUPCAM_OPTION_RGB_RGB24 0
#define GST_TOUPCAM_OPTION_RGB_RGB48 1
#define GST_TOUPCAM_OPTION_RGB_RGB32 2
#define GST_TOUPCAM_OPTION_RGB_GREY8 3
#define GST_TOUPCAM_OPTION_RGB_GREY16 4
#define GST_TOUPCAM_OPTION_RGB_RGB64 5

/*
Output pixel formats

The camera is configured based on the negotiated caps format
Keep RGB first: its the default when downstream doesn't care (see fixate)

RGB: 8 bit demosaiced RGB from the camera ISP (hue, gamma, etc applied)
ARGB64: 16 bit demosaiced RGB (RGB48 from SDK, alpha unused)
    Samples are left aligned (ex: 12 bit sensor => shifted left 4)
GRAY16_LE: sensor raw. Bayer mosaic for color cameras (see raw-format property)
    Samples are left aligned, see bit-depth property for actual depth
    No demosaic / white balance / etc is applied: do that downstream
GRAY8: 8 bit sensor raw / mono
*/
#define GST_TOUPCAM_FORMATS "{ RGB, ARGB64, GRAY16_LE, GRAY8 }"

// pad template
static GstStaticPadTemplate gst_toupcam_src_template =
GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                        GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE
                                        (GST_TOUPCAM_FORMATS)));

/* class initialisation */

G_DEFINE_TYPE(GstToupCamSrc, gst_toupcam_src, GST_TYPE_PUSH_SRC);

static void install_properties(GObjectClass *gobject_class)
{
    g_object_class_install_property(gobject_class, PROP_SDK_BRAND,
                                    g_param_spec_string("sdk-brand", NULL,
                                                        NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_SDK_VERSION,
                                    g_param_spec_string("sdk-version",
                                                        NULL, NULL, NULL,
                                                        G_PARAM_READABLE));

    g_object_class_install_property(gobject_class, PROP_SERIAL_NUMBER,
                                    g_param_spec_string("serial-number",
                                                        NULL, NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_FW_VERSION,
                                    g_param_spec_string("fw-version", NULL,
                                                        NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_HW_VERSION,
                                    g_param_spec_string("hw-version", NULL,
                                                        NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_PRODUCTION_DATE,
                                    g_param_spec_string("production-date",
                                                        NULL, NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_FPGA_VERSION,
                                    g_param_spec_string("fpga-version",
                                                        NULL, NULL, NULL,
                                                        G_PARAM_READABLE));

#if CAMSDK_VERSION >= 53
    g_object_class_install_property(gobject_class, PROP_MODEL_NAME,
                                    g_param_spec_string("model-name", NULL,
                                                        NULL, NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_FLAG,
                                    g_param_spec_uint64("model-flag", NULL,
                                                        NULL, 0,
                                                        0xFFFFFFFFFFFFFFFFLL,
                                                        0,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_MAXSPEED,
                                    g_param_spec_uint("model-maxspeed",
                                                      NULL, NULL, 0,
                                                      16777215, 0,
                                                      G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_PREVIEW,
                                    g_param_spec_uint("model-preview",
                                                      NULL, NULL, 0,
                                                      16777215, 0,
                                                      G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_STILL,
                                    g_param_spec_uint("model-still", NULL,
                                                      NULL, 0, 16777215, 0,
                                                      G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_MAXFANSPEED,
                                    g_param_spec_uint("model-maxfanspeed",
                                                      NULL, NULL, 0,
                                                      16777215, 0,
                                                      G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_XPIXSZ,
                                    g_param_spec_float("model-xpixsz",
                                                       NULL, NULL, 0,
                                                       16777215, 0,
                                                       G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MODEL_YPIXSZ,
                                    g_param_spec_float("model-ypixsz",
                                                       NULL, NULL, 0,
                                                       16777215, 0,
                                                       G_PARAM_READABLE));
#endif

    g_object_class_install_property(gobject_class, PROP_REVISION,
                                    g_param_spec_int("revision", NULL,
                                                     NULL, 0, 16777215, 0,
                                                     G_PARAM_READABLE));

    g_object_class_install_property(gobject_class, PROP_MAX_BIT_DEPTH,
                                    g_param_spec_int("max-bit-depth", NULL,
                                                     NULL, 0, 16, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_MONO,
                                    g_param_spec_boolean("mono",
                                                         "Monochrome sensor",
                                                         "Sensor has no color filter array",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_RAW_FORMAT,
                                    g_param_spec_string("raw-format",
                                                        "Raw format FourCC",
                                                        "Bayer pattern of sensor raw data (ex: RGGB)",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_BIT_DEPTH,
                                    g_param_spec_int("bit-depth",
                                                     "Current bit depth",
                                                     "Significant bits per sample in the negotiated output format. 0 if not yet negotiated",
                                                     0, 16, 0,
                                                     G_PARAM_READABLE));


    g_object_class_install_property(gobject_class, PROP_EXPOSURE_TIME_MIN,
                                    g_param_spec_int("exposure-time-min",
                                                     NULL, NULL, 0,
                                                     16777215, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_EXPOSURE_TIME_MAX,
                                    g_param_spec_int("exposure-time-max",
                                                     NULL, NULL, 0,
                                                     16777215, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(gobject_class,
                                    PROP_EXPOSURE_TIME_DEFAULT,
                                    g_param_spec_int
                                    ("exposure-time-default", NULL, NULL,
                                     0, 16777215, 0, G_PARAM_READABLE));

    g_object_class_install_property(gobject_class, PROP_EXPOSURE_AGAIN_MIN,
                                    g_param_spec_int("exposure-again-min",
                                                     NULL, NULL, 0,
                                                     16777215, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_EXPOSURE_AGAIN_MAX,
                                    g_param_spec_int("exposure-again-max",
                                                     NULL, NULL, 0,
                                                     16777215, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(gobject_class,
                                    PROP_EXPOSURE_AGAIN_DEFAULT,
                                    g_param_spec_int
                                    ("exposure-again-default", NULL, NULL,
                                     0, 16777215, 0, G_PARAM_READABLE));



    g_object_class_install_property(gobject_class, PROP_CAMERAPRESENT,
                                    g_param_spec_boolean("devicepresent",
                                                         "Camera Device Present",
                                                         "Is the camera present and connected OK?",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(gobject_class, PROP_ESIZE,
                                    g_param_spec_int("esize",
                                                     "Camera size enumeration",
                                                     "...", 0, 2, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_HFLIP,
                                    g_param_spec_boolean("hflip",
                                                         "Horizontal flip",
                                                         "Horizontal flip",
                                                         DEFAULT_PROP_HFLIP,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_VFLIP,
                                    g_param_spec_boolean("vflip",
                                                         "Vertical flip",
                                                         "Vertical flip",
                                                         DEFAULT_PROP_VFLIP,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_AUTO_EXPOSURE,
                                    g_param_spec_boolean("auto-exposure",
                                                         "Auto exposure",
                                                         "Auto exposure",
                                                         DEFAULT_PROP_AUTO_EXPOSURE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_EXPOTIME,
                                    g_param_spec_int("expotime",
                                                     "Exposure us", "...",
                                                     MIN_PROP_EXPOTIME,
                                                     MAX_PROP_EXPOTIME,
                                                     DEFAULT_PROP_EXPOTIME,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_EXPOAGAIN,
                                    g_param_spec_int("expoagain",
                                                     "ExpoAGain as percentage",
                                                     "...",
                                                     MIN_PROP_EXPOAGAIN,
                                                     MAX_PROP_EXPOAGAIN,
                                                     DEFAULT_PROP_EXPOAGAIN,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_HUE,
                                    g_param_spec_int("hue", "...", "...",
                                                     CAMSDK_(HUE_MIN),
                                                     CAMSDK_(HUE_MAX),
                                                     CAMSDK_(HUE_DEF),
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_SATURATION,
                                    g_param_spec_int("saturation", "...",
                                                     "...",
                                                     CAMSDK_
                                                     (SATURATION_MIN),
                                                     CAMSDK_
                                                     (SATURATION_MAX),
                                                     CAMSDK_
                                                     (SATURATION_DEF),
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_BRIGHTNESS,
                                    g_param_spec_int("brightness", "...",
                                                     "...",
                                                     CAMSDK_
                                                     (BRIGHTNESS_MIN),
                                                     CAMSDK_
                                                     (BRIGHTNESS_MAX),
                                                     CAMSDK_
                                                     (BRIGHTNESS_DEF),
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_CONTRAST,
                                    g_param_spec_int("contrast", "...",
                                                     "...",
                                                     CAMSDK_(CONTRAST_MIN),
                                                     CAMSDK_(CONTRAST_MAX),
                                                     CAMSDK_(CONTRAST_DEF),
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_GAMMA,
                                    g_param_spec_int("gamma", "...", "...",
                                                     CAMSDK_(GAMMA_MIN),
                                                     CAMSDK_(GAMMA_MAX),
                                                     CAMSDK_(GAMMA_DEF),
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    /*
       0: normal, 255 turn channel off
       ie setting to 255/255/255 turns image black
     */
    g_object_class_install_property(gobject_class, PROP_BB_R,
                                    g_param_spec_int("bb-r", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_BB_G,
                                    g_param_spec_int("bb-g", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_BB_B,
                                    g_param_spec_int("bb-b", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_WB_R,
                                    g_param_spec_int("wb-r", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_WB_G,
                                    g_param_spec_int("wb-g", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_WB_B,
                                    g_param_spec_int("wb-b", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_AWB_RGB,
                                    g_param_spec_boolean("awb-rgb",
                                                         "Trigger AWB",
                                                         "Requests a single red/green/blue AWB and clears when complete",
                                                         0,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_AWB_TT,
                                    g_param_spec_boolean("awb-tt",
                                                         "Trigger AWB",
                                                         "Requests a single temp/tint AWB and clears when complete",
                                                         0,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
}

static void gst_toupcam_src_class_init(GstToupCamSrcClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "toupcamsrc", 0,
                            "ToupCam Camera source");

    gobject_class->set_property = gst_toupcam_src_set_property;
    gobject_class->get_property = gst_toupcam_src_get_property;
    gobject_class->dispose = gst_toupcam_src_dispose;
    gobject_class->finalize = gst_toupcam_src_finalize;

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get
                                       (&gst_toupcam_src_template));

    gst_element_class_set_static_metadata(gstelement_class,
                                          "ToupCam Video Source",
                                          "Source/Video",
                                          "ToupCam Camera video source",
                                          "John McMaster <johndmcmaster@gmail.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_toupcam_src_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_toupcam_src_stop);
    gstbasesrc_class->get_caps =
        GST_DEBUG_FUNCPTR(gst_toupcam_src_get_caps);
    gstbasesrc_class->fixate = GST_DEBUG_FUNCPTR(gst_toupcam_src_fixate);
    gstbasesrc_class->set_caps =
        GST_DEBUG_FUNCPTR(gst_toupcam_src_set_caps);

    gstpushsrc_class->alloc = GST_DEBUG_FUNCPTR(gst_toupcam_src_alloc);
    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_toupcam_src_fill);
    GST_DEBUG("Using gst_toupcam_src_fill");


    install_properties(gobject_class);
}

static void gst_toupcam_src_init(GstToupCamSrc *src)
{
    src->auto_exposure = DEFAULT_PROP_AUTO_EXPOSURE;
    src->expotime = DEFAULT_PROP_EXPOTIME;
    src->expoagain = DEFAULT_PROP_EXPOAGAIN;
    src->vflip = DEFAULT_PROP_VFLIP;
    src->hflip = DEFAULT_PROP_HFLIP;
    src->hue = DEFAULT_PROP_HUE;
    src->saturation = DEFAULT_PROP_SATURATION;
    src->brightness = DEFAULT_PROP_BRIGHTNESS;
    src->contrast = DEFAULT_PROP_CONTRAST;
    src->gamma = DEFAULT_PROP_GAMMA;
    src->black_balance[0] = 0;
    src->black_balance[1] = 0;
    src->black_balance[2] = 0;

    src->white_balance[0] = 0;
    src->white_balance[1] = 0;
    src->white_balance[2] = 0;

    src->awb_rgb = 0;
    src->awb_tt = 0;

    /* set source as live (no preroll) */
    gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);

    g_mutex_init(&src->mutex);
    g_cond_init(&src->cond);
    gst_toupcam_src_reset(src);
}

static void gst_toupcam_src_reset(GstToupCamSrc *src)
{
    src->hCam = 0;
    src->streaming = FALSE;
    gst_video_info_init(&src->vinfo);
    src->max_bit_depth = 0;
    src->mono = FALSE;
    src->sensor_bits = 0;
    src->sdk_bits = 0;
    src->shift16 = 0;
    src->bytes_per_pix_in = 0;
    g_free(src->scratch);
    src->scratch = NULL;
    src->scratch_size = 0;
    src->imagesAvailable = 0;
    src->imagesPulled = 0;
    src->total_timeouts = 0;
    src->last_frame_time = 0;
    src->m_total = 0;
}

static gboolean gst_toupcam_src_get_raw_fourcc(GstToupCamSrc *src,
                                               char fourcc[5])
{
    unsigned nFourCC = 0;
    unsigned bitsperpixel = 0;

    fourcc[0] = 0;
    if (!src->hCam) {
        return FALSE;
    }
    if (FAILED
        (camsdk_(get_RawFormat) (src->hCam, &nFourCC, &bitsperpixel))) {
        return FALSE;
    }
    memcpy(fourcc, &nFourCC, 4);
    fourcc[4] = 0;
    return TRUE;
}

static void my_rgb_cb(const int aGain[3], void *pCtx)
{
    GstToupCamSrc *src = (GstToupCamSrc *) pCtx;
    //printf("gain %u %u %u\n", aGain[0], aGain[1], aGain[2]);
    src->awb_rgb = 0;
}

static void my_tt_cb(const int nTemp, const int nTint, void *pCtx)
{
    GstToupCamSrc *src = (GstToupCamSrc *) pCtx;
    //printf("awb cb %d %d %p\n", nTemp, nTint, pCtx);
    src->awb_tt = 0;
}

void gst_toupcam_src_set_property(GObject *object, guint property_id,
                                  const GValue *value, GParamSpec *pspec)
{
    GstToupCamSrc *src;

    src = GST_TOUPCAM_SRC(object);

    switch (property_id) {
    case PROP_ESIZE:
        // Only set before start
        src->esize = g_value_get_int(value);
        break;
    case PROP_HFLIP:
        src->hflip = g_value_get_boolean(value);
        if (src->hCam) {
            camsdk_(put_HFlip) (src->hCam, src->hflip);
        }
        break;
    case PROP_VFLIP:
        src->vflip = g_value_get_boolean(value);
        if (src->hCam) {
            camsdk_(put_VFlip) (src->hCam, src->vflip);
        }
        break;
    case PROP_AUTO_EXPOSURE:
        src->auto_exposure = g_value_get_boolean(value);
        if (src->hCam) {
            camsdk_(put_AutoExpoEnable) (src->hCam, src->auto_exposure);
        }
        break;
    case PROP_EXPOTIME:
        src->expotime = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_ExpoTime) (src->hCam, src->expotime);
        }
        break;
    case PROP_EXPOAGAIN:
        src->expoagain = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_ExpoAGain) (src->hCam, src->expoagain);
        }
        break;
    case PROP_HUE:
        src->hue = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_Hue) (src->hCam, src->hue);
        }
        break;
    case PROP_SATURATION:
        src->saturation = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_Saturation) (src->hCam, src->saturation);
        }
        break;
    case PROP_BRIGHTNESS:
        src->brightness = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_Brightness) (src->hCam, src->brightness);
        }
        break;
    case PROP_CONTRAST:
        src->contrast = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_Contrast) (src->hCam, src->contrast);
        }
        break;
    case PROP_GAMMA:
        src->gamma = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_Gamma) (src->hCam, src->gamma);
        }
        break;

    case PROP_BB_R:
        src->black_balance[0] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_BlackBalance) (src->hCam, src->black_balance);
        }
        break;
    case PROP_BB_G:
        src->black_balance[1] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_BlackBalance) (src->hCam, src->black_balance);
        }
        break;
    case PROP_BB_B:
        src->black_balance[2] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_BlackBalance) (src->hCam, src->black_balance);
        }
        break;

    case PROP_WB_R:
        src->white_balance[0] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_WhiteBalanceGain) (src->hCam, src->white_balance);
        }
        break;
    case PROP_WB_G:
        src->white_balance[1] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_WhiteBalanceGain) (src->hCam, src->white_balance);
        }
        break;
    case PROP_WB_B:
        src->white_balance[2] = g_value_get_int(value);
        if (src->hCam) {
            camsdk_(put_WhiteBalanceGain) (src->hCam, src->white_balance);
        }
        break;

    case PROP_AWB_RGB:
        if (!(src->awb_rgb || src->awb_tt)) {
            // fail...
            if (FAILED(camsdk_(AwbInit) (src->hCam, my_rgb_cb, src))) {
                GST_ERROR_OBJECT(src, "failed to awb rgb");
                src->awb_rgb = 0;
            } else {
                src->awb_rgb = 1;
            }
        }
        break;

    case PROP_AWB_TT:
        if (!(src->awb_rgb || src->awb_tt)) {
            // ok
            if (FAILED(camsdk_(AwbOnce) (src->hCam, my_tt_cb, src))) {
                GST_ERROR_OBJECT(src, "failed to awb tt");
                src->awb_tt = 0;
            } else {
                src->awb_tt = 1;
            }
        }
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void try_get_black_balance(GstToupCamSrc *src)
{
    if (src->hCam) {
        camsdk_(get_BlackBalance) (src->hCam, src->black_balance);
    }
}

static void try_get_white_balance(GstToupCamSrc *src)
{
    if (src->hCam) {
        camsdk_(get_WhiteBalanceGain) (src->hCam, src->white_balance);
    }
}

void gst_toupcam_src_get_property(GObject *object, guint property_id,
                                  GValue *value, GParamSpec *pspec)
{
    GstToupCamSrc *src;
    char buff[64] = "";
    int tempi = 0;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    switch (property_id) {
    case PROP_SDK_BRAND:
        g_value_set_string(value, CAMDSK_BRAND);
        break;
    case PROP_SDK_VERSION:
        g_value_set_string(value, camsdk_(Version) ());
        break;

    case PROP_SERIAL_NUMBER:
        if (src->hCam) {
            camsdk_(get_SerialNumber) (src->hCam, buff);
        }
        g_value_set_string(value, buff);
        break;
    case PROP_FW_VERSION:
        if (src->hCam) {
            camsdk_(get_FwVersion) (src->hCam, buff);
        }
        g_value_set_string(value, buff);
        break;
    case PROP_HW_VERSION:
        if (src->hCam) {
            camsdk_(get_HwVersion) (src->hCam, buff);
        }
        g_value_set_string(value, buff);
        break;
    case PROP_PRODUCTION_DATE:
        if (src->hCam) {
            camsdk_(get_ProductionDate) (src->hCam, buff);
        }
        g_value_set_string(value, buff);
        break;
    case PROP_FPGA_VERSION:
        if (src->hCam) {
            camsdk_(get_FpgaVersion) (src->hCam, buff);
        }
        g_value_set_string(value, buff);
        break;

#if CAMSDK_VERSION >= 53
    case PROP_MODEL_NAME:
    case PROP_MODEL_FLAG:
    case PROP_MODEL_MAXSPEED:
    case PROP_MODEL_PREVIEW:
    case PROP_MODEL_STILL:
    case PROP_MODEL_MAXFANSPEED:
    case PROP_MODEL_XPIXSZ:
    case PROP_MODEL_YPIXSZ:
        if (src->hCam) {
            //maybe just move to struct?
            const camsdk(ModelV2) * model =
                camsdk_(query_Model) (src->hCam);
            if (model) {
                switch (property_id) {
                case PROP_MODEL_NAME:
                    g_value_set_string(value, model->name);
                    break;
                case PROP_MODEL_FLAG:
                    g_value_set_uint64(value, model->flag);
                    break;
                case PROP_MODEL_MAXSPEED:
                    g_value_set_uint(value, model->maxspeed);
                    break;
                case PROP_MODEL_PREVIEW:
                    g_value_set_uint(value, model->preview);
                    break;
                case PROP_MODEL_STILL:
                    g_value_set_uint(value, model->still);
                    break;
                case PROP_MODEL_MAXFANSPEED:
                    g_value_set_uint(value, model->maxfanspeed);
                    break;
                case PROP_MODEL_XPIXSZ:
                    g_value_set_float(value, model->xpixsz);
                    break;
                case PROP_MODEL_YPIXSZ:
                    g_value_set_float(value, model->ypixsz);
                    break;
                }
            }
        }
        break;
#endif

    case PROP_REVISION:
        if (src->hCam) {
            unsigned short ustmp;
            if (!FAILED(camsdk_(get_Revision) (src->hCam, &ustmp))) {
                tempi = ustmp;
            }
        }
        g_value_set_int(value, tempi);
        break;

    case PROP_MAX_BIT_DEPTH:
        if (src->hCam) {
            tempi = camsdk_(get_MaxBitDepth) (src->hCam);
        }
        g_value_set_int(value, tempi);
        break;
    case PROP_MONO:
        g_value_set_boolean(value, src->mono);
        break;
    case PROP_RAW_FORMAT:{
            char fourcc[5] = "";
            if (!src->mono) {
                gst_toupcam_src_get_raw_fourcc(src, fourcc);
            }
            g_value_set_string(value, fourcc);
            break;
        }
    case PROP_BIT_DEPTH:
        g_value_set_int(value, src->sensor_bits);
        break;

    case PROP_EXPOSURE_TIME_MIN:
        if (src->hCam) {
            unsigned nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpTimeRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nMin;
            }
        }
        g_value_set_int(value, tempi);
        break;
    case PROP_EXPOSURE_TIME_MAX:
        if (src->hCam) {
            unsigned nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpTimeRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nMax;
            }
        }
        g_value_set_int(value, tempi);
        break;
    case PROP_EXPOSURE_TIME_DEFAULT:
        if (src->hCam) {
            unsigned nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpTimeRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nDef;
            }
        }
        g_value_set_int(value, tempi);
        break;

    case PROP_EXPOSURE_AGAIN_MIN:
        if (src->hCam) {
            unsigned short nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpoAGainRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nMin;
            }
        }
        g_value_set_int(value, tempi);
        break;
    case PROP_EXPOSURE_AGAIN_MAX:
        if (src->hCam) {
            unsigned short nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpoAGainRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nMax;
            }
        }
        g_value_set_int(value, tempi);
        break;
    case PROP_EXPOSURE_AGAIN_DEFAULT:
        if (src->hCam) {
            unsigned short nMin, nMax, nDef;
            if (!FAILED(camsdk_(get_ExpoAGainRange)
                        (src->hCam, &nMin, &nMax, &nDef))) {
                tempi = nDef;
            }
        }
        g_value_set_int(value, tempi);
        break;

    case PROP_CAMERAPRESENT:
        g_value_set_boolean(value, src->hCam != NULL ? TRUE : FALSE);
        break;
    case PROP_ESIZE:
        g_value_set_int(value, src->esize);
        break;
    case PROP_HFLIP:
        if (src->hCam) {
            camsdk_(get_HFlip) (src->hCam, &src->hflip);
        }
        g_value_set_boolean(value, src->hflip);
        break;
    case PROP_VFLIP:
        if (src->hCam) {
            camsdk_(get_VFlip) (src->hCam, &src->vflip);
        }
        g_value_set_boolean(value, src->vflip);
        break;
    case PROP_AUTO_EXPOSURE:
        if (src->hCam) {
            camsdk_(get_AutoExpoEnable) (src->hCam, &src->auto_exposure);
        }
        g_value_set_boolean(value, src->auto_exposure);
        break;
    case PROP_EXPOTIME:
        if (src->hCam) {
            camsdk_(get_ExpoTime) (src->hCam, &src->expotime);
        }
        g_value_set_int(value, src->expotime);
        break;
    case PROP_EXPOAGAIN:
        if (src->hCam) {
            camsdk_(get_ExpoAGain) (src->hCam, &src->expoagain);
        }
        g_value_set_int(value, src->expoagain);
        break;
    case PROP_HUE:
        if (src->hCam) {
            camsdk_(get_Hue) (src->hCam, &src->hue);
        }
        g_value_set_int(value, src->hue);
        break;
    case PROP_SATURATION:
        if (src->hCam) {
            camsdk_(get_Saturation) (src->hCam, &src->saturation);
        }
        g_value_set_int(value, src->saturation);
        break;
    case PROP_BRIGHTNESS:
        if (src->hCam) {
            camsdk_(get_Brightness) (src->hCam, &src->brightness);
        }
        g_value_set_int(value, src->brightness);
        break;
    case PROP_CONTRAST:
        if (src->hCam) {
            camsdk_(get_Contrast) (src->hCam, &src->contrast);
        }
        g_value_set_int(value, src->contrast);
        break;
    case PROP_GAMMA:
        if (src->hCam) {
            camsdk_(get_Gamma) (src->hCam, &src->gamma);
        }
        g_value_set_int(value, src->gamma);
        break;

    case PROP_BB_R:
        try_get_black_balance(src);
        g_value_set_int(value, src->black_balance[0]);
        break;
    case PROP_BB_G:
        try_get_black_balance(src);
        g_value_set_int(value, src->black_balance[1]);
        break;
    case PROP_BB_B:
        try_get_black_balance(src);
        g_value_set_int(value, src->black_balance[2]);
        break;

    case PROP_WB_R:
        try_get_white_balance(src);
        g_value_set_int(value, src->white_balance[0]);
        break;
    case PROP_WB_G:
        try_get_white_balance(src);
        g_value_set_int(value, src->white_balance[1]);
        break;
    case PROP_WB_B:
        try_get_white_balance(src);
        g_value_set_int(value, src->white_balance[2]);
        break;

    case PROP_AWB_RGB:
        g_value_set_boolean(value, src->awb_rgb);
        break;

    case PROP_AWB_TT:
        g_value_set_boolean(value, src->awb_tt);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_toupcam_src_dispose(GObject *object)
{
    GstToupCamSrc *src;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    GST_DEBUG_OBJECT(src, "dispose");

    // clean up as possible.  may be called multiple times

    G_OBJECT_CLASS(gst_toupcam_src_parent_class)->dispose(object);
}

void gst_toupcam_src_finalize(GObject *object)
{
    GstToupCamSrc *src;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    GST_DEBUG_OBJECT(src, "finalize");

    /* clean up object here */
    g_free(src->scratch);
    src->scratch = NULL;
    g_mutex_clear(&src->mutex);
    g_cond_clear(&src->cond);
    G_OBJECT_CLASS(gst_toupcam_src_parent_class)->finalize(object);
}

static void sdk_callback_PullMode(unsigned nEvent, void *pCallbackCtx)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(pCallbackCtx);
    //Note: lots of 1 => EVENT_EXPOSURE
    GST_DEBUG_OBJECT(src,
                     "sdk_callback_PullMode(nEvent=%d) begin, want %d",
                     nEvent, CAMSDK_(EVENT_IMAGE));
    if (CAMSDK_(EVENT_IMAGE) == nEvent) {
        g_mutex_lock(&src->mutex);
        src->imagesAvailable++;
        g_cond_signal(&src->cond);
        g_mutex_unlock(&src->mutex);
    }
    GST_DEBUG_OBJECT(src,
                     "sdk_callback_PullMode(nEvent=%d) end, images now %u",
                     nEvent, src->imagesAvailable);
}

void gst_toupcam_pdebug(GstToupCamSrc *src)
{
    int itmp;
    short stmp;
    unsigned short ustmp;
    char buff[64];

    printf("toupcam debug info for Version(): %s\n", camsdk_(Version) ());
    printf("  SDK_Brand: " CAMDSK_BRAND "\n");
    printf("  MaxBitDepth(): %d\n", camsdk_(get_MaxBitDepth) (src->hCam));
    printf("  FanMaxSpeed(): %d\n", camsdk_(get_FanMaxSpeed) (src->hCam));
    //Max frame rate
    printf("  MaxSpeed(): %d\n", camsdk_(get_MaxSpeed) (src->hCam));
    printf("  MonoMode(): %d\n", camsdk_(get_MonoMode) (src->hCam));
    int resn = camsdk_(get_StillResolutionNumber) (src->hCam);
    printf("  StillResolutionNumber(): %d\n", resn);
    if (resn < 0) {
        printf("    Failed :(\n");
    } else {
        for (int resi = 0; resi < resn; ++resi) {
            printf("    eSize=%d\n", resi);
            int width, height;
            if (!FAILED(camsdk_(get_StillResolution)
                        (src->hCam, resi, &width, &height))) {
                printf("          StillResolution(): %iw x %ih\n", width,
                       height);
            }
            float pixx, pixy;
            if (!FAILED
                (camsdk_(get_PixelSize) (src->hCam, resi, &pixx, &pixy))) {
                printf("          PixelSize(): %0.1fw x %0.1fh um\n", pixx,
                       pixy);
            }
        }
    }

    {
        unsigned nMin, nMax, nDef;
        if (!FAILED
            (camsdk_(get_ExpTimeRange) (src->hCam, &nMin, &nMax, &nDef))) {
            printf("  ExpTimeRange(): min %d, max %d, def %d\n", nMin,
                   nMax, nDef);
        }
    }
    {
        unsigned short nMin, nMax, nDef;
        if (!FAILED
            (camsdk_(get_ExpoAGainRange) (src->hCam, &nMin, &nMax, &nDef)))
        {
            printf("  ExpoAGainRange(): min %d, max %d, def %d\n", nMin,
                   nMax, nDef);
        }
    }

    if (!FAILED(camsdk_(get_Negative) (src->hCam, &itmp))) {
        printf("  Negative(): %d\n", itmp);
    }
    if (!FAILED(camsdk_(get_Chrome) (src->hCam, &itmp))) {
        printf("  Chrome(): %d\n", itmp);
    }
    if (!FAILED(camsdk_(get_HZ) (src->hCam, &itmp))) {
        printf("  HZ(): %d\n", itmp);
    }
    if (!FAILED(camsdk_(get_Mode) (src->hCam, &itmp))) {
        printf("  Mode(): %d\n", itmp);
    }
    if (!FAILED(camsdk_(get_RealTime) (src->hCam, &itmp))) {
        printf("  RealTime(): %d\n", itmp);
    }

    if (!FAILED(camsdk_(get_Temperature) (src->hCam, &stmp))) {
        printf("  Temperature(): %d\n", stmp);
    }
    if (!FAILED(camsdk_(get_Revision) (src->hCam, &ustmp))) {
        printf("  Revision(): %d\n", ustmp);
    }

    if (!FAILED(camsdk_(get_SerialNumber) (src->hCam, buff))) {
        printf("  SerialNumber(): %s\n", buff);
    }
    if (!FAILED(camsdk_(get_FwVersion) (src->hCam, buff))) {
        printf("  FwVersion(): %s\n", buff);
    }
    if (!FAILED(camsdk_(get_HwVersion) (src->hCam, buff))) {
        printf("  HwVersion(): %s\n", buff);
    }
    if (!FAILED(camsdk_(get_ProductionDate) (src->hCam, buff))) {
        printf("  ProductionDate(): %s\n", buff);
    }
    if (!FAILED(camsdk_(get_FpgaVersion) (src->hCam, buff))) {
        printf("  FpgaVersion(): %s\n", buff);
    }

    //Not supported in version 50
#if CAMSDK_VERSION >= 53
    const camsdk(ModelV2) * model = camsdk_(query_Model) (src->hCam);
    if (model) {
        printf("  ToupcamModelV2():\n");
        printf("    name: %s\n", model->name);
        printf("    flag: 0x%08llX\n", model->flag);
        printf("    maxspeed: %u\n", model->maxspeed);
        printf("    preview: %u\n", model->preview);
        printf("    still: %u\n", model->still);
    }
#endif

    /*
       if (!FAILED(camsdk_(get_Name)(id, buff))) {
       printf("  name: %s\n", buff);
       }
     */
    // 0 => #define TOUPCAM_PIXELFORMAT_RAW8             0x00
    camsdk_(get_Option) (src->hCam, CAMSDK_(OPTION_PIXEL_FORMAT), &itmp);
    printf("  Option(PIXEL_FORMAT): %i\n", itmp);

    char nFourCC[4];
    unsigned bitsperpixel;
    camsdk_(get_RawFormat) (src->hCam, (unsigned *) &nFourCC,
                            &bitsperpixel);
    // raw code GBRG, bpp 8
    // needs this to get the full 12 bit
    // camsdk_(put_Option)(src->hCam, CAMSDK_(OPTION_PIXEL_FORMAT),
    // CAMSDK_(PIXELFORMAT_RAW12)); raw code GBRG, bpp 12
    printf("    RawFormat(): FourCC %c%c%c%c, BitsPerPixel %u\n",
           nFourCC[0], nFourCC[1], nFourCC[2], nFourCC[3], bitsperpixel);
    // gstreamer caps formats this camera can be negotiated to
    // NOTE: keep this after RawFormat (pyuscope parses sequentially)
    {
        GstCaps *caps = gst_toupcam_src_get_caps(GST_BASE_SRC(src), NULL);
        const GValue *formats =
            gst_structure_get_value(gst_caps_get_structure(caps, 0),
                                    "format");
        gchar *s = gst_value_serialize(formats);
        printf("  Formats(): %s\n", s);
        g_free(s);
        gst_caps_unref(caps);
    }
}


static gboolean gst_toupcam_src_start(GstBaseSrc *bsrc)
{
    camsdk(DeviceV2) arr[CAMSDK_(MAX)];

    unsigned cnt = 0;
    char at_id[128];

    // Start will open the device but not start it, set_caps starts it, stop
    // should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);

    GST_DEBUG_OBJECT(src, "gst_toupcam_src_start(): begin");

    // Turn on automatic timestamping, if so we do not need to do it manually, BUT
    // there is some evidence that automatic timestamping is laggy
    //    gst_base_src_set_do_timestamp(bsrc, TRUE);

    // read libversion (for informational purposes only)
    GST_INFO_OBJECT(src, "ToupCam Library Ver %s", camsdk_(Version) ());

    // enumerate devices (needed to get device id in order to prepend with "@" to
    // enable RGB gain functions)
    cnt = camsdk_(EnumV2) (arr);
    GST_INFO_OBJECT(src, "Found %d devices", cnt);
    if (cnt < 1) {
        GST_ERROR_OBJECT(src, "No ToupCam devices found");
        goto fail;
    }

    GST_DEBUG_OBJECT(src, "Toupcam_Open()");
    // open first usable device id preprended with "@"
    snprintf(at_id, sizeof(at_id), "@%s", arr[0].id);
    src->hCam = camsdk_(Open) (at_id);
    if (NULL == src->hCam) {
        GST_ERROR_OBJECT(src, "open failed");
        goto fail;
    }

    HRESULT hr;

    hr = camsdk_(put_eSize) (src->hCam, src->esize);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to set size, hr = %08x", hr);
        goto fail;
    }
    hr = camsdk_(get_Size) (src->hCam, &src->nWidth, &src->nHeight);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to get size, hr = %08x", hr);
        goto fail;
    }

    // Camera capabilities used to restrict negotiable formats
    src->max_bit_depth = camsdk_(get_MaxBitDepth) (src->hCam);
    if (src->max_bit_depth < 8) {
        GST_WARNING_OBJECT(src, "bad max bit depth %d, assuming 8",
                           src->max_bit_depth);
        src->max_bit_depth = 8;
    }
    // S_OK (0): mono, S_FALSE (1): color
    src->mono = camsdk_(get_MonoMode) (src->hCam) == 0;
    GST_INFO_OBJECT(src, "max bit depth %d, mono %d", src->max_bit_depth,
                    src->mono);

    if (getenv("GST_TOUPCAMSRC_INFO")) {
        gst_toupcam_pdebug(src);
    }

    // Format independent settings
    // Format specific settings (bit depth, raw, etc) are applied in set_caps
    camsdk_(put_HFlip) (src->hCam, src->hflip);
    camsdk_(put_VFlip) (src->hCam, src->vflip);
    hr = camsdk_(put_AutoExpoEnable) (src->hCam, src->auto_exposure);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to auto exposure, hr = %08x", hr);
        goto fail;
    }
    if (!src->auto_exposure) {
        // setting this severely interferes with auto exposure
        camsdk_(put_ExpoTime) (src->hCam, src->expotime);
    }

    unsigned nFrame, nTime, nTotalFrame;
    camsdk_(get_FrameRate) (src->hCam, &nFrame, &nTime, &nTotalFrame);
    src->framerate = nFrame * 1000.0 / nTime;
    src->duration = 1000000000.0 / src->framerate;

    GST_DEBUG_OBJECT(src, "gst_toupcam_src_start(): ok (%d w x %d h)",
                     src->nWidth, src->nHeight);
    return TRUE;

  fail:
    if (src->hCam) {
        camsdk_(Close) (src->hCam);
        src->hCam = NULL;
    }

    return FALSE;
}

/*
Configure the SDK for the given gstreamer output format and (re)start streaming
Must be called with streaming stopped (OPTION_RAW can only be set before Start)
*/
static gboolean gst_toupcam_src_configure_format(GstToupCamSrc *src,
                                                 GstVideoFormat format)
{
    HRESULT hr;
    gboolean raw = FALSE;
    gboolean x16 = FALSE;
    int rgb_option = GST_TOUPCAM_OPTION_RGB_RGB24;

    switch (format) {
    case GST_VIDEO_FORMAT_RGB:
        GST_DEBUG_OBJECT(src, "setup image mode: RGB");
        src->sdk_bits = 24;
        src->bytes_per_pix_in = 3;
        break;
    case GST_VIDEO_FORMAT_ARGB64:
        GST_DEBUG_OBJECT(src, "setup image mode: ARGB64 (via RGB48)");
        x16 = TRUE;
        rgb_option = GST_TOUPCAM_OPTION_RGB_RGB48;
        src->sdk_bits = 48;
        src->bytes_per_pix_in = 6;
        break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
        GST_DEBUG_OBJECT(src, "setup image mode: GRAY16_LE (raw)");
        x16 = TRUE;
        if (src->mono) {
            rgb_option = GST_TOUPCAM_OPTION_RGB_GREY16;
            src->sdk_bits = 16;
        } else {
            raw = TRUE;
            // 0 => native raw data
            src->sdk_bits = 0;
        }
        src->bytes_per_pix_in = 2;
        break;
    case GST_VIDEO_FORMAT_GRAY8:
        GST_DEBUG_OBJECT(src, "setup image mode: GRAY8 (raw)");
        if (src->mono) {
            rgb_option = GST_TOUPCAM_OPTION_RGB_GREY8;
            src->sdk_bits = 8;
        } else {
            raw = TRUE;
            src->sdk_bits = 0;
        }
        src->bytes_per_pix_in = 1;
        break;
    default:
        GST_ERROR_OBJECT(src, "unsupported format %s",
                         gst_video_format_to_string(format));
        return FALSE;
    }

    if (x16 && src->max_bit_depth <= 8) {
        GST_ERROR_OBJECT(src,
                         "16 bit format requested but camera max bit depth is %d",
                         src->max_bit_depth);
        return FALSE;
    }

    if (raw) {
        // Select the deepest raw pixel format the sensor supports
        // OPTION_BITDEPTH is documented as a subset of this
        // so not fatal if the SDK rejects it
        if (x16) {
            int pixel_format = CAMSDK_(PIXELFORMAT_RAW12);
            switch (src->max_bit_depth) {
            case 10:
                pixel_format = CAMSDK_(PIXELFORMAT_RAW10);
                break;
            case 12:
                pixel_format = CAMSDK_(PIXELFORMAT_RAW12);
                break;
            case 14:
                pixel_format = CAMSDK_(PIXELFORMAT_RAW14);
                break;
            case 16:
                pixel_format = CAMSDK_(PIXELFORMAT_RAW16);
                break;
            default:
                GST_WARNING_OBJECT(src,
                                   "unexpected max bit depth %d, trying RAW12",
                                   src->max_bit_depth);
            }
            hr = camsdk_(put_Option) (src->hCam,
                                      CAMSDK_(OPTION_PIXEL_FORMAT),
                                      pixel_format);
            if (FAILED(hr)) {
                GST_WARNING_OBJECT(src,
                                   "failed to set pixel format %d, hr = %08x",
                                   pixel_format, hr);
            }
        } else {
            hr = camsdk_(put_Option) (src->hCam,
                                      CAMSDK_(OPTION_PIXEL_FORMAT),
                                      CAMSDK_(PIXELFORMAT_RAW8));
            if (FAILED(hr)) {
                GST_WARNING_OBJECT(src,
                                   "failed to set pixel format RAW8, hr = %08x",
                                   hr);
            }
        }
    }

    /*
       Restoring an SDK default (0) is allowed to fail: some cameras reject
       options they don't implement and we don't want to break the plain RGB
       path on them. Requesting a non-default is fatal if rejected
     */
    hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_RAW), raw ? 1 : 0);
    if (FAILED(hr)) {
        if (raw) {
            GST_ERROR_OBJECT(src, "failed to set raw=%d, hr = %08x", raw,
                             hr);
            return FALSE;
        }
        GST_WARNING_OBJECT(src, "failed to set raw=0, hr = %08x", hr);
    }
    hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_BITDEPTH),
                              x16 ? 1 : 0);
    if (FAILED(hr)) {
        if (x16) {
            GST_ERROR_OBJECT(src, "failed to set bitdepth=%d, hr = %08x",
                             x16, hr);
            return FALSE;
        }
        GST_WARNING_OBJECT(src, "failed to set bitdepth=0, hr = %08x", hr);
    }
    if (!raw) {
        hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_RGB),
                                  rgb_option);
        if (FAILED(hr)) {
            if (rgb_option != GST_TOUPCAM_OPTION_RGB_RGB24) {
                GST_ERROR_OBJECT(src, "failed to set rgb=%d, hr = %08x",
                                 rgb_option, hr);
                return FALSE;
            }
            GST_WARNING_OBJECT(src, "failed to set rgb=0, hr = %08x", hr);
        }
        camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_BYTEORDER),
                             GST_TOUPCAM_OPTION_BYTEORDER_RGB);
        // ISP settings. Not applicable to raw
        camsdk_(put_Hue) (src->hCam, src->hue);
        camsdk_(put_Saturation) (src->hCam, src->saturation);
        camsdk_(put_Brightness) (src->hCam, src->brightness);
        camsdk_(put_Contrast) (src->hCam, src->contrast);
        camsdk_(put_Gamma) (src->hCam, src->gamma);
    }

    // Figure out how many significant bits we are getting
    if (x16) {
        unsigned nFourCC = 0;
        unsigned bitsperpixel = 0;
        src->sensor_bits = src->max_bit_depth;
        // In raw mode this reflects the selected pixel format
        if (raw && !FAILED(camsdk_(get_RawFormat)
                           (src->hCam, &nFourCC, &bitsperpixel))
            && bitsperpixel > 8 && bitsperpixel <= 16) {
            src->sensor_bits = bitsperpixel;
        }
        src->shift16 = 16 - src->sensor_bits;
    } else {
        src->sensor_bits = 8;
        src->shift16 = 0;
    }
    GST_INFO_OBJECT(src,
                    "format %s: raw %d, x16 %d, sdk bits %d, sensor bits %d, shift %d",
                    gst_video_format_to_string(format), raw, x16,
                    src->sdk_bits, src->sensor_bits, src->shift16);
    return TRUE;
}

static gboolean gst_toupcam_src_stream_stop(GstToupCamSrc *src)
{
    if (!src->streaming) {
        return TRUE;
    }
    GST_DEBUG_OBJECT(src, "stopping stream");
    HRESULT hr = camsdk_(Stop) (src->hCam);
    src->streaming = FALSE;
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to stop camera, hr = %08x", hr);
        return FALSE;
    }
    return TRUE;
}

static gboolean gst_toupcam_src_stream_start(GstToupCamSrc *src)
{
    g_assert(!src->streaming);
    // Discard any bookkeeping from the previous stream
    g_mutex_lock(&src->mutex);
    src->imagesAvailable = 0;
    src->imagesPulled = 0;
    g_mutex_unlock(&src->mutex);

    GST_DEBUG_OBJECT(src, "starting stream");
    HRESULT hr = camsdk_(StartPullModeWithCallback) (src->hCam,
                                                     sdk_callback_PullMode,
                                                     src);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to start camera, hr = %08x", hr);
        return FALSE;
    }
    src->streaming = TRUE;
    return TRUE;
}

static gboolean gst_toupcam_src_stop(GstBaseSrc *bsrc)
{
    // Start will open the device but not start it, set_caps starts it, stop
    // should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);

    GST_DEBUG_OBJECT(src, "gst_toupcam_src_stop()");
    if (src->hCam) {
        gst_toupcam_src_stream_stop(src);
        camsdk_(Close) (src->hCam);
    }

    gst_toupcam_src_reset(src);

    return TRUE;
}

static GstCaps *gst_toupcam_src_get_caps(GstBaseSrc *bsrc, GstCaps *filter)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);
    GstCaps *caps;

    if (src->hCam == 0) {
        caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));
    } else {
        // Formats this camera can actually produce
        // 16 bit formats require a > 8 bit sensor
        GValue formats = G_VALUE_INIT;
        GValue fmt = G_VALUE_INIT;
        const char *all[] = { "RGB", "ARGB64", "GRAY16_LE", "GRAY8" };
        g_value_init(&formats, GST_TYPE_LIST);
        g_value_init(&fmt, G_TYPE_STRING);
        for (unsigned i = 0; i < G_N_ELEMENTS(all); ++i) {
            GstVideoFormat f = gst_video_format_from_string(all[i]);
            if (src->max_bit_depth <= 8
                && (f == GST_VIDEO_FORMAT_ARGB64
                    || f == GST_VIDEO_FORMAT_GRAY16_LE)) {
                continue;
            }
            g_value_set_string(&fmt, all[i]);
            gst_value_list_append_value(&formats, &fmt);
        }
        g_value_unset(&fmt);

        // Frames per second fraction n/d, 0/1 indicates a frame rate may vary
        caps = gst_caps_new_simple("video/x-raw",
                                   "width", G_TYPE_INT, src->nWidth,
                                   "height", G_TYPE_INT, src->nHeight,
                                   "framerate", GST_TYPE_FRACTION, 0, 1,
                                   "interlace-mode", G_TYPE_STRING,
                                   "progressive",
                                   "pixel-aspect-ratio",
                                   GST_TYPE_FRACTION, 1, 1, NULL);
        gst_caps_set_value(caps, "format", &formats);
        g_value_unset(&formats);
    }

    //this func is called a lot and spams the output
    //GST_INFO_OBJECT(src, "The caps are %" GST_PTR_FORMAT, caps);

    if (filter) {
        GstCaps *tmp = gst_caps_intersect(caps, filter);
        gst_caps_unref(caps);
        caps = tmp;

        GST_INFO_OBJECT(src,
                        "The caps after filtering are %" GST_PTR_FORMAT,
                        caps);
    }

    return caps;
}

static GstCaps *gst_toupcam_src_fixate(GstBaseSrc *bsrc, GstCaps *caps)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);
    GstStructure *s;

    // Default to 8 bit RGB if downstream doesn't care
    // ie plain "toupcamsrc ! videoconvert" behaves like it always has
    caps = gst_caps_make_writable(caps);
    s = gst_caps_get_structure(caps, 0);
    gst_structure_fixate_field_string(s, "format", "RGB");
    GST_DEBUG_OBJECT(src, "fixated to %" GST_PTR_FORMAT, caps);

    return GST_BASE_SRC_CLASS(gst_toupcam_src_parent_class)->fixate(bsrc,
                                                                    caps);
}

static gboolean gst_toupcam_src_set_caps(GstBaseSrc *bsrc, GstCaps *caps)
{
    // Start opens the device but does not start it
    // set_caps configures the pixel format and starts it
    // stop stops and closes it (as v4l2src)
    // Caps renegotiation (ex: downstream capsfilter changes format)
    // stops the camera, reconfigures, and restarts

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);
    GstVideoInfo vinfo;
    GstVideoFormat format;

    GST_INFO_OBJECT(src, "The caps being set are %" GST_PTR_FORMAT, caps);

    if (!gst_video_info_from_caps(&vinfo, caps)) {
        goto unsupported_caps;
    }
    format = GST_VIDEO_INFO_FORMAT(&vinfo);
    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        goto unsupported_caps;
    }
    g_assert(src->hCam != 0);

    if (vinfo.width != src->nWidth || vinfo.height != src->nHeight) {
        GST_ERROR_OBJECT(src,
                         "caps %d w x %d h don't match camera %d w x %d h",
                         vinfo.width, vinfo.height, src->nWidth,
                         src->nHeight);
        goto unsupported_caps;
    }

    if (src->streaming && format == GST_VIDEO_INFO_FORMAT(&src->vinfo)) {
        GST_DEBUG_OBJECT(src, "format unchanged");
        src->vinfo = vinfo;
        return TRUE;
    }

    if (!gst_toupcam_src_stream_stop(src)) {
        return FALSE;
    }
    if (!gst_toupcam_src_configure_format(src, format)) {
        goto unsupported_caps;
    }

    // Scratch buffer for formats needing conversion
    g_free(src->scratch);
    src->scratch = NULL;
    src->scratch_size = 0;
    src->scratch_pitch = 0;
    if (format == GST_VIDEO_FORMAT_ARGB64) {
        // SDK default pitch: rows padded to 4 bytes
        src->scratch_pitch =
            ((src->nWidth * src->bytes_per_pix_in * 8 + 31) & ~31) / 8;
        src->scratch_size = (gsize) src->scratch_pitch * src->nHeight;
        src->scratch = g_malloc(src->scratch_size);
    }

    src->vinfo = vinfo;
    GST_DEBUG_OBJECT(src,
                     "Image %d w x %d h, in %d bytes / pix (%0.1f MB), out %"
                     G_GSIZE_FORMAT " bytes (%0.1f MB), stride %d",
                     src->nWidth, src->nHeight, src->bytes_per_pix_in,
                     (double) src->nWidth * src->nHeight *
                     src->bytes_per_pix_in / 1e6, vinfo.size,
                     vinfo.size / 1e6, GST_VIDEO_INFO_PLANE_STRIDE(&vinfo,
                                                                   0));

    if (!gst_toupcam_src_stream_start(src)) {
        return FALSE;
    }

    return TRUE;

  unsupported_caps:
    GST_ERROR_OBJECT(src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

/*
Left align samples in a 16 bit container
ex: 12 bit raw => shift left 4 so full scale is 0xFFF0
Operates in place on a single row
*/
static inline void shift16_row(uint16_t *row, unsigned n, unsigned shift)
{
    for (unsigned x = 0; x < n; ++x) {
        row[x] <<= shift;
    }
}

// SDK RGB48 (packed, 16 bits / channel) to gstreamer ARGB64 (host endian)
static void RGB48_to_ARGB64(GstToupCamSrc *src, const unsigned char *bufin,
                            unsigned in_stride, unsigned char *bufout,
                            unsigned out_stride)
{
    const unsigned shift = src->shift16;
    for (unsigned y = 0; y < (unsigned) src->nHeight; ++y) {
        const uint16_t *in = (const uint16_t *) (bufin + (gsize) y * in_stride);
        uint16_t *out = (uint16_t *) (bufout + (gsize) y * out_stride);
        for (unsigned x = 0; x < (unsigned) src->nWidth; ++x) {
            // A R G B
            out[0] = 0xFFFF;
            out[1] = in[0] << shift;
            out[2] = in[1] << shift;
            out[3] = in[2] << shift;
            in += 3;
            out += 4;
        }
    }
}

static GstFlowReturn wait_new_frame(GstToupCamSrc *src)
{
    //printf("Waiting for new frame...\n");
    // Wait for the next image to be ready
    int timeout = 5;
    while (src->imagesAvailable <= src->imagesPulled) {
        gint64 end_time;
        g_mutex_lock(&src->mutex);
        end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until(&src->cond, &src->mutex, end_time)) {
            GST_DEBUG_OBJECT(src,
                             "timed out waiting for image, timeout=%u",
                             timeout);
            // timeout has passed.
            // g_mutex_unlock (&src->mutex); // return here if needed
            // return NULL;
        }
        g_mutex_unlock(&src->mutex);
        timeout--;
        if (timeout <= 0) {
            // did not return an image. why?
            // ----------------------------------------------------------
            GST_ERROR_OBJECT(src, "WaitEvent timed out.");
            return GST_FLOW_ERROR;
        }
    }
    return GST_FLOW_OK;
}

/*
Pull a frame from the SDK into dst with the given row pitch
The SDK's default pitch is rows padded to 4 bytes, same as gstreamer's
for the formats we use, so normally the plain PullImageV2 is used
(byte identical to historical behavior). Fall back to the explicit
row pitch variant if they ever differ
*/
static HRESULT pull_image(GstToupCamSrc *src, void *dst, int bits,
                          unsigned pitch, camsdk(FrameInfoV2) * info)
{
    unsigned default_pitch =
        ((src->nWidth * src->bytes_per_pix_in * 8 + 31) & ~31) / 8;
    if (pitch == default_pitch) {
        return camsdk_(PullImageV2) (src->hCam, dst, bits, info);
    }
    GST_DEBUG_OBJECT(src, "non-default pitch %u (default %u)", pitch,
                     default_pitch);
    return camsdk_(PullImageWithRowPitchV2) (src->hCam, dst, bits, pitch,
                                             info);
}

static GstFlowReturn pull_decode_frame(GstToupCamSrc *src, GstBuffer *buf)
{
    GstMapInfo minfo;
    GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&src->vinfo);
    const unsigned stride = GST_VIDEO_INFO_PLANE_STRIDE(&src->vinfo, 0);
    HRESULT hr;

    if (format == GST_VIDEO_FORMAT_UNKNOWN || !src->streaming) {
        GST_ERROR_OBJECT(src, "not negotiated");
        return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_buffer_map(buf, &minfo, GST_MAP_WRITE);
    GST_DEBUG_OBJECT(src,
                     "minfo size %" G_GSIZE_FORMAT ", maxsize %"
                     G_GSIZE_FORMAT ", flags 0x%08X", minfo.size,
                     minfo.maxsize, minfo.flags);
    if (minfo.size < src->vinfo.size) {
        gst_buffer_unmap(buf, &minfo);
        GST_ERROR_OBJECT(src,
                         "bad minfo size. Expect %" G_GSIZE_FORMAT
                         ", got %" G_GSIZE_FORMAT, src->vinfo.size,
                         minfo.size);
        return GST_FLOW_ERROR;
    }

    camsdk(FrameInfoV2) info = { 0 };
    switch (format) {
    case GST_VIDEO_FORMAT_ARGB64:
        /*
           SDK gives packed RGB48
           Expand to ARGB64 w/ left aligned samples
         */
        GST_DEBUG_OBJECT(src, "pulling RGB48 image");
        hr = pull_image(src, src->scratch, src->sdk_bits,
                        src->scratch_pitch, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT(src, "decoding image");
        RGB48_to_ARGB64(src, src->scratch, src->scratch_pitch, minfo.data,
                        stride);
        break;

    case GST_VIDEO_FORMAT_GRAY16_LE:
        /*
           Sensor raw (bayer mosaic) or mono, 2 bytes / pixel
           Pull directly into the gst buffer, then left align in place
         */
        GST_DEBUG_OBJECT(src, "pulling raw16 image");
        hr = pull_image(src, minfo.data, src->sdk_bits, stride, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }
        if (src->shift16) {
            for (unsigned y = 0; y < (unsigned) src->nHeight; ++y) {
                shift16_row((uint16_t *) (minfo.data + (gsize) y * stride),
                            src->nWidth, src->shift16);
            }
        }
        break;

    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_RGB:
        // Direct pull, no conversion
        GST_DEBUG_OBJECT(src, "pulling %s image",
                         gst_video_format_to_string(format));
        hr = pull_image(src, minfo.data, src->sdk_bits, stride, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }
        break;

    default:
        gst_buffer_unmap(buf, &minfo);
        GST_ERROR_OBJECT(src, "unsupported format %s",
                         gst_video_format_to_string(format));
        return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_buffer_unmap(buf, &minfo);

    if (info.width && info.height
        && (info.width != (unsigned) src->nWidth
            || info.height != (unsigned) src->nHeight)) {
        GST_WARNING_OBJECT(src,
                           "frame %u x %u doesn't match negotiated %d x %d",
                           info.width, info.height, src->nWidth,
                           src->nHeight);
    }

    src->imagesPulled += 1;
    /* After we get the image data, we can do anything for the data we want to do
     */
    GST_DEBUG_OBJECT(src,
                     "pull image ok, total = %u, resolution = %u x %u",
                     ++src->m_total, info.width, info.height);
    GST_DEBUG_OBJECT(src, "flag %u, seq %u, us %llu", info.flag, info.seq,
                     info.timestamp);

    return GST_FLOW_OK;
}

static GstFlowReturn gst_toupcam_src_alloc(GstPushSrc *psrc,
                                           GstBuffer **buf)
{
    GstFlowReturn ret;

    GstToupCamSrc *src = GST_TOUPCAM_SRC(psrc);

    if (GST_VIDEO_INFO_FORMAT(&src->vinfo) == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_ERROR_OBJECT(src, "alloc before negotiation");
        return GST_FLOW_NOT_NEGOTIATED;
    }

    *buf = gst_buffer_new_allocate(NULL, src->vinfo.size, NULL);
    if (G_UNLIKELY(*buf == NULL)) {
        GST_ERROR_OBJECT(src,
                         "Failed to allocate %" G_GSIZE_FORMAT " bytes",
                         src->vinfo.size);
        ret = GST_FLOW_ERROR;
    } else {
        ret = GST_FLOW_OK;
    }

    return ret;
}

// Override the push class fill fn, using the default create and alloc fns.
// buf is the buffer to fill, it may be allocated in alloc or from a downstream
// element. Other functions such as deinterlace do not work with this type of
// buffer.
static GstFlowReturn gst_toupcam_src_fill(GstPushSrc *psrc, GstBuffer *buf)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(psrc);

    GST_DEBUG_OBJECT(src, "gst_toupcam_src_fill()");

    // printf("Want %d buffers have %d\n", psrc->parent.num_buffers,
    // src->n_frames);
    // If we were asked for a specific number of buffers, stop when complete
    if (psrc->parent.num_buffers > 0) {
        if (G_UNLIKELY(src->n_frames >= psrc->parent.num_buffers)) {
            GST_DEBUG_OBJECT(src, "EOS");
            return GST_FLOW_EOS;
        }
    }

    GST_DEBUG_OBJECT(src, " ");
    GST_DEBUG_OBJECT(src, "waiting for new image");

    if (wait_new_frame(src) != GST_FLOW_OK) {
        GST_ERROR_OBJECT(src, "Failed to get next frame");
        return GST_FLOW_ERROR;
    }
    if (pull_decode_frame(src, buf) != GST_FLOW_OK) {
        GST_ERROR_OBJECT(src, "Failed to decode frame");
        return GST_FLOW_ERROR;
    }

    /*
       // If we do not use gst_base_src_set_do_timestamp() we need to add timestamps
       manually src->last_frame_time += src->duration;   // Get the timestamp for
       this frame if (!gst_base_src_get_do_timestamp(GST_BASE_SRC(psrc))){
       GST_BUFFER_PTS(buf) = src->last_frame_time;  // convert ms to ns
       GST_BUFFER_DTS(buf) = src->last_frame_time;  // convert ms to ns
       }
       GST_BUFFER_DURATION(buf) = src->duration;
       GST_DEBUG_OBJECT (src, "pts, dts: %" GST_TIME_FORMAT ", duration: %ld ms",
       GST_TIME_ARGS (src->last_frame_time), GST_TIME_AS_MSECONDS(src->duration));
     */
    GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;

    // count frames, and send EOS when required frame number is reached
    GST_BUFFER_OFFSET(buf) = src->n_frames;     // from videotestsrc
    src->n_frames++;

    return GST_FLOW_OK;
}
