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

int raw = 0;
int x16 = 0;

// pad template
static GstStaticPadTemplate gst_toupcam_src_template_x8 =
GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                        GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ RGB }")));

static GstStaticPadTemplate gst_toupcam_src_template_x16 =
GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                        GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE
                                        ("{ ARGB64 }")));

/* class initialisation */

G_DEFINE_TYPE(GstToupCamSrc, gst_toupcam_src, GST_TYPE_PUSH_SRC);

static void gst_toupcam_src_class_init(GstToupCamSrcClass * klass)
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

    if (raw || x16) {
        GST_DEBUG("select x16 template");
        gst_element_class_add_pad_template(gstelement_class,
                                           gst_static_pad_template_get
                                           (&gst_toupcam_src_template_x16));
    } else {
        gst_element_class_add_pad_template(gstelement_class,
                                           gst_static_pad_template_get
                                           (&gst_toupcam_src_template_x8));
    }

    gst_element_class_set_static_metadata(gstelement_class,
                                          "ToupCam Video Source",
                                          "Source/Video",
                                          "ToupCam Camera video source",
                                          "John McMaster <johndmcmaster@gmail.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_toupcam_src_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_toupcam_src_stop);
    gstbasesrc_class->get_caps =
        GST_DEBUG_FUNCPTR(gst_toupcam_src_get_caps);
    gstbasesrc_class->set_caps =
        GST_DEBUG_FUNCPTR(gst_toupcam_src_set_caps);

    gstpushsrc_class->alloc = GST_DEBUG_FUNCPTR(gst_toupcam_src_alloc);
    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_toupcam_src_fill);
    GST_DEBUG("Using gst_toupcam_src_fill");

    // Install GObject properties
    // Camera Present property
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
                                    g_param_spec_boolean("auto_exposure",
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
                                                     "ExpoAGain as percentage", "...",
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
                                    g_param_spec_int("bb_r", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_BB_G,
                                    g_param_spec_int("bb_g", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_BB_B,
                                    g_param_spec_int("bb_b", "...", "...",
                                                     0, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_WB_R,
                                    g_param_spec_int("wb_r", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_WB_G,
                                    g_param_spec_int("wb_g", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_WB_B,
                                    g_param_spec_int("wb_b", "...", "...",
                                                     -255, 255, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_AWB_RGB,
                                    g_param_spec_boolean("awb_rgb",
                                                         "Trigger AWB",
                                                         "Requests a single red/green/blue AWB and clears when complete",
                                                         0,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));
    g_object_class_install_property(gobject_class, PROP_AWB_TT,
                                    g_param_spec_boolean("awb_tt",
                                                         "Trigger AWB",
                                                         "Requests a single temp/tint AWB and clears when complete",
                                                         0,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));



}

static void gst_toupcam_src_init(GstToupCamSrc * src)
{
    src->raw = raw;
    src->x16 = x16;
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

static void gst_toupcam_src_reset(GstToupCamSrc * src)
{
    src->hCam = 0;
    src->imagesAvailable = 0;
    src->imagesPulled = 0;
    src->total_timeouts = 0;
    src->last_frame_time = 0;
    src->m_total = 0;
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

void gst_toupcam_src_set_property(GObject * object, guint property_id,
                                  const GValue * value, GParamSpec * pspec)
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

static void try_get_black_balance(GstToupCamSrc * src)
{
    if (src->hCam) {
        camsdk_(get_BlackBalance) (src->hCam, src->black_balance);
    }
}

static void try_get_white_balance(GstToupCamSrc * src)
{
    if (src->hCam) {
        camsdk_(get_WhiteBalanceGain) (src->hCam, src->white_balance);
    }
}

void gst_toupcam_src_get_property(GObject * object, guint property_id,
                                  GValue * value, GParamSpec * pspec)
{
    GstToupCamSrc *src;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    switch (property_id) {
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

void gst_toupcam_src_dispose(GObject * object)
{
    GstToupCamSrc *src;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    GST_DEBUG_OBJECT(src, "dispose");

    // clean up as possible.  may be called multiple times

    G_OBJECT_CLASS(gst_toupcam_src_parent_class)->dispose(object);
}

void gst_toupcam_src_finalize(GObject * object)
{
    GstToupCamSrc *src;

    g_return_if_fail(GST_IS_TOUPCAM_SRC(object));
    src = GST_TOUPCAM_SRC(object);

    GST_DEBUG_OBJECT(src, "finalize");

    /* clean up object here */
    G_OBJECT_CLASS(gst_toupcam_src_parent_class)->finalize(object);
}

static void sdk_callback_PullMode(unsigned nEvent, void *pCallbackCtx)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(pCallbackCtx);
    //Note: lots of 1 => EVENT_EXPOSURE
    GST_DEBUG_OBJECT(src, "sdk_callback_PullMode(nEvent=%d) begin, want %d", nEvent, CAMSDK_(EVENT_IMAGE));
    if (CAMSDK_(EVENT_IMAGE) == nEvent) {
        g_mutex_lock(&src->mutex);
        src->imagesAvailable++;
        g_cond_signal(&src->cond);
        g_mutex_unlock(&src->mutex);
    }
    GST_DEBUG_OBJECT(src, "sdk_callback_PullMode(nEvent=%d) end, images now %u", nEvent, src->imagesAvailable);
}

void gst_toupcam_pdebug(GstToupCamSrc * src)
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
                printf("          StillResolution(): %iw x %ih\n", width, height);
            }
            float pixx, pixy;
            if (!FAILED
                (camsdk_(get_PixelSize) (src->hCam, resi, &pixx, &pixy))) {
                printf("          PixelSize(): %0.1fw x %0.1fh um\n", pixx, pixy);
            }
        }
    }

    {
        unsigned nMin, nMax, nDef;
        if (!FAILED(camsdk_(get_ExpTimeRange) (src->hCam, &nMin, &nMax, &nDef))) {
            printf("  ExpTimeRange(): min %d, max %d, def %d\n", nMin, nMax, nDef);
        }
    }
    {
        unsigned short nMin, nMax, nDef;
        if (!FAILED(camsdk_(get_ExpoAGainRange) (src->hCam, &nMin, &nMax, &nDef))) {
            printf("  ExpoAGainRange(): min %d, max %d, def %d\n", nMin, nMax, nDef);
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

    const camsdk(ModelV2) *model = camsdk_(query_Model) (src->hCam);
    if (model) {
        printf("  ToupcamModelV2():\n");
        printf("    name: %s\n", model->name);
        printf("    flag: 0x%08llX\n", model->flag);
        printf("    maxspeed: %u\n", model->maxspeed);
        printf("    preview: %u\n", model->preview);
        printf("    still: %u\n", model->still);
    }

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
    printf("    RawFormat(): FourCC %c%c%c%c, BitsPerPixel %u\n", nFourCC[0], nFourCC[1],
           nFourCC[2], nFourCC[3], bitsperpixel);
}

static gboolean gst_toupcam_src_start(GstBaseSrc * bsrc)
{
    camsdk(DeviceV2) arr[CAMSDK_(MAX)];

    unsigned cnt = 0;
    char at_id[128];

    // Start will open the device but not start it, set_caps starts it, stop
    // should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);

    GST_DEBUG_OBJECT (src, "gst_toupcam_src_start(): begin");

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

    if (getenv("GST_TOUPCAMSRC_INFO")) {
        gst_toupcam_pdebug(src);
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

    if (src->raw) {
        // can set raw8 and raw12, but not raw16
        // default raw8
        GST_DEBUG_OBJECT(src, "setup image mode: raw");
        if (1) {
            hr = camsdk_(put_Option) (src->hCam,
                                      CAMSDK_(OPTION_PIXEL_FORMAT),
                                      CAMSDK_(PIXELFORMAT_RAW12));
            if (FAILED(hr)) {
                GST_ERROR_OBJECT(src,
                                 "failed to set pixel format, hr = %08x",
                                 hr);
                goto fail;
            }
        }
        // no output when this is enabled...why?
        if (1) {
            // enable raw
            hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_RAW), 1);
            if (FAILED(hr)) {
                GST_ERROR_OBJECT(src, "failed to enable raw, hr = %08x",
                                 hr);
                goto fail;
            }
        }
        if (1) {
            // 16 bit output
            hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_BITDEPTH),
                                      1);
            if (FAILED(hr)) {
                GST_ERROR_OBJECT(src, "failed to enable 16 bit, hr = %08x",
                                 hr);
                goto fail;
            }
        }
    } else if (src->x16) {
        // can set raw8 and raw12, but not raw16
        // default raw8
        GST_DEBUG_OBJECT(src, "setup image mode: x16");

        // 16 bit output
        hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_BITDEPTH), 1);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to enable 16 bit, hr = %08x",
                             hr);
            goto fail;
        }

        // RGB48
        hr = camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_RGB), 1);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to enable raw, hr = %08x", hr);
            goto fail;
        }
    } else {
        GST_DEBUG_OBJECT(src, "setup image mode: regular");
        camsdk_(put_Option) (src->hCam, CAMSDK_(OPTION_BYTEORDER),
                             GST_TOUPCAM_OPTION_BYTEORDER_RGB);
        camsdk_(put_Hue) (src->hCam, src->hue);
        camsdk_(put_Saturation) (src->hCam, src->saturation);
        camsdk_(put_Brightness) (src->hCam, src->brightness);
        camsdk_(put_Contrast) (src->hCam, src->contrast);
        camsdk_(put_Gamma) (src->hCam, src->gamma);
    }

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

    // BGR 24-bit is primarily supported
    // Some attempts at 16 bit
    if (src->raw || src->x16) {
        src->bits_per_pix_out = 64;
        src->bytes_per_pix_out = 8;
        src->bytes_per_pix_in = 6;
    } else {
        src->bits_per_pix_out = 24;
        src->bytes_per_pix_out = 3;
        src->bytes_per_pix_in = 3;
    }

    unsigned nFrame, nTime, nTotalFrame;
    camsdk_(get_FrameRate) (src->hCam, &nFrame, &nTime, &nTotalFrame);
    src->framerate = nFrame * 1000.0 / nTime;
    src->duration = 1000000000.0 / src->framerate;

    src->image_bytes_in =
        src->nWidth * src->nHeight * src->bytes_per_pix_in;
    src->image_bytes_out =
        src->nWidth * src->nHeight * src->bytes_per_pix_out;
    // GST_DEBUG_OBJECT (src, "Image is %d x %d, pitch %d, bpp %d, Bpp %d",
    // src->nWidth, src->nHeight, src->bits_per_pix_out, src->bytes_per_pix_out);
    GST_DEBUG_OBJECT(src,
                     "Image %d w x %d h, in %d bytes / pix => %d bytes (%0.1f "
                     "MB), out %d bytes / pix => %d bytes (%0.1f MB)",
                     src->nWidth, src->nHeight, src->bytes_per_pix_in,
                     src->image_bytes_in, src->image_bytes_in / 1e6,
                     src->bytes_per_pix_out, src->image_bytes_out,
                     src->image_bytes_out / 1e6);

    // TODO: move from static buff to frame_buff
    src->frame_buff = NULL;

    hr = camsdk_(StartPullModeWithCallback) (src->hCam, sdk_callback_PullMode,
                                             src);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to start camera, hr = %08x", hr);
        goto fail;
    }


    GST_DEBUG_OBJECT (src, "gst_toupcam_src_start(): ok");
    return TRUE;

  fail:
    if (src->hCam) {
        src->hCam = NULL;
    }

    return FALSE;
}

static gboolean gst_toupcam_src_stop(GstBaseSrc * bsrc)
{
    // Start will open the device but not start it, set_caps starts it, stop
    // should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);

    GST_DEBUG_OBJECT(src, "gst_toupcam_src_stop()");
    camsdk_(Close) (src->hCam);

    gst_toupcam_src_reset(src);

    return TRUE;
}

static GstCaps *gst_toupcam_src_get_caps(GstBaseSrc * bsrc,
                                         GstCaps * filter)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);
    GstCaps *caps;

    if (src->hCam == 0) {
        caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));
    } else {
        GstVideoInfo vinfo;

        // Create video info
        gst_video_info_init(&vinfo);

        vinfo.width = src->nWidth;
        vinfo.height = src->nHeight;

        // Frames per second fraction n/d, 0/1 indicates a frame rate may vary
        vinfo.fps_n = 0;
        vinfo.fps_d = 1;
        vinfo.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

        if (src->raw || src->x16) {
            vinfo.finfo =
                gst_video_format_get_info(GST_VIDEO_FORMAT_ARGB64);
        } else {
            vinfo.finfo = gst_video_format_get_info(GST_VIDEO_FORMAT_RGB);
        }

        // cannot do this for variable frame rate
        // src->duration = gst_util_uint64_scale_int (GST_SECOND, vinfo.fps_d,
        // vinfo.fps_n); // NB n and d are wrong way round to invert the fps into a
        // duration.

        caps = gst_video_info_to_caps(&vinfo);
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

static gboolean gst_toupcam_src_set_caps(GstBaseSrc * bsrc, GstCaps * caps)
{
    // Start will open the device but not start it, set_caps starts it, stop
    // should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC(bsrc);
    GstVideoInfo vinfo;
    // GstStructure *s = gst_caps_get_structure (caps, 0);

    GST_INFO_OBJECT(src, "The caps being set are %" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps(&vinfo, caps);

    if (GST_VIDEO_INFO_FORMAT(&vinfo) != GST_VIDEO_FORMAT_UNKNOWN) {
        g_assert(src->hCam != 0);
        //  src->vrm_stride = get_pitch (src->device);  // wait for image to arrive
        //  for this
        src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE(&vinfo, 0);
        src->nHeight = vinfo.height;
    } else {
        goto unsupported_caps;
    }

    // start freerun/continuous capture
    src->acq_started = TRUE;

    return TRUE;

  unsupported_caps:
    GST_ERROR_OBJECT(src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

// raw to common format
void GBRG12_to_ARGB64_x4(GstToupCamSrc * src, const unsigned char *bufin,
                         unsigned char *bufout)
{
    for (unsigned y = 0; y < src->nHeight; ++y) {
        for (unsigned x = 0; x < src->nWidth; ++x) {
            uint16_t pix16 = (bufin[1] << 12) | (bufin[0] << 4);
            /*
               if (y == 0 && x < 16) {
               //0x91 0x0E
               GST_DEBUG_OBJECT (src, "0x%02X 0x%02X", bufin[0], bufin[1]);
               }
             */
            uint8_t pix1 = pix16 >> 8;
            uint8_t pix0 = pix16 & 0xFF;
            unsigned colori = x % 4;
            // blue
            if (colori == 1) {
                bufout[6] = pix0;
                bufout[7] = pix1;
                // red
            } else if (colori == 3) {
                bufout[2] = pix0;
                bufout[3] = pix1;
                // green
            } else {
                bufout[4] = pix0;
                bufout[5] = pix1;
            }
            bufin += 2;
            bufout += 8;
        }
    }
}

// high def to common format
void RGB48_to_ARGB64_x4(GstToupCamSrc * src, const unsigned char *bufin,
                        unsigned char *bufout)
{
    for (unsigned y = 0; y < src->nHeight; ++y) {
        for (unsigned x = 0; x < src->nWidth; ++x) {
            uint16_t rpix16 = (bufin[1] << 12) | (bufin[0] << 4);
            bufin += 2;
            uint16_t gpix16 = (bufin[1] << 12) | (bufin[0] << 4);
            bufin += 2;
            uint16_t bpix16 = (bufin[1] << 12) | (bufin[0] << 4);
            bufin += 2;
            /*
               if (y == 0 && x < 16) {
               //0x91 0x0E
               GST_DEBUG_OBJECT (src, "0x%02X 0x%02X", bufin[0], bufin[1]);
               }
             */
            uint8_t rpix1 = rpix16 >> 8;
            uint8_t rpix0 = rpix16 & 0xFF;
            uint8_t gpix1 = gpix16 >> 8;
            uint8_t gpix0 = gpix16 & 0xFF;
            uint8_t bpix1 = bpix16 >> 8;
            uint8_t bpix0 = bpix16 & 0xFF;
            // red
            bufout[2] = rpix0;
            bufout[3] = rpix1;
            // green
            bufout[4] = gpix0;
            bufout[5] = gpix1;
            // blue
            bufout[6] = bpix0;
            bufout[7] = bpix1;
            bufout += 8;
        }
    }
}

static GstFlowReturn wait_new_frame(GstToupCamSrc * src)
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

static GstFlowReturn pull_decode_frame(GstToupCamSrc * src,
                                       GstBuffer * buf)
{
    /*
    Max size is RGBA 16 bit => 8 bytes per pixel
    FIXME: size this dynamically
    For now just make it really big
    Largest supported camera is E3ISPM25000KPA @ 25MP
    */
    static unsigned char raw_buff[4928 * 4928 * 4 * 2];
    // Copy image to buffer in the right way
    GstMapInfo minfo;

    // minfo size 4096, maxsize 4103, flags 0x00000002
    gst_buffer_map(buf, &minfo, GST_MAP_WRITE);
    GST_DEBUG_OBJECT(src,
                     "minfo size %" G_GSIZE_FORMAT ", maxsize %"
                     G_GSIZE_FORMAT ", flags 0x%08X", minfo.size,
                     minfo.maxsize, minfo.flags);
    // XXX: debugging crash
    if (minfo.size != src->image_bytes_out) {
        gst_buffer_unmap(buf, &minfo);
        GST_ERROR_OBJECT(src,
                         "bad minfo size. Expect %d, got %" G_GSIZE_FORMAT,
                         src->image_bytes_out, minfo.size);
        return GST_FLOW_ERROR;
    }

    camsdk(FrameInfoV2) info = { 0 };
    if (src->raw) {
        /*
           RGBA, 16 bit => 4 * 2 => 64 bit
           Source data raw => densely packed into 16 bit areas
         */

        if (sizeof(raw_buff) < src->image_bytes_in) {
            gst_buffer_unmap(buf, &minfo);
            GST_ERROR_OBJECT(src,
                             "insufficient frame buffer size. Need %d, got %d",
                             src->image_bytes_in, src->image_bytes_in);
            return GST_FLOW_ERROR;
        }

        // From the grabber source we get 1 progressive frame
        GST_DEBUG_OBJECT(src, "pulling raw image");
        HRESULT hr = camsdk_(PullImageV2) (src->hCam, &raw_buff, 0, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT(src, "decoding image");
        GBRG12_to_ARGB64_x4(src, raw_buff, minfo.data);
        // memset(minfo.data, 0x80, src->nWidth * src->nHeight * 7);
#if 0
        {
            FILE *fp;

            fp = fopen("raw.bin", "wb");
            fwrite(minfo.data, 1, src->image_bytes_out, fp);
            fclose(fp);
        }
#endif
    } else if (src->x16) {
        if (sizeof(raw_buff) < src->image_bytes_in) {
            GST_ERROR_OBJECT(src,
                             "insufficient frame buffer size. Need %d, got %d",
                             src->image_bytes_in, src->image_bytes_in);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT(src, "pulling x16 image");
        HRESULT hr =
            camsdk_(PullImageV2) (src->hCam, &raw_buff, 48, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT(src, "decoding image");
        RGB48_to_ARGB64_x4(src, raw_buff, minfo.data);
    } else {
        GST_DEBUG_OBJECT(src, "pulling x8 image");
        HRESULT hr =
            camsdk_(PullImageV2) (src->hCam, minfo.data, 24, &info);
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(src, "failed to pull image, hr = %08x", hr);
            gst_buffer_unmap(buf, &minfo);
            return GST_FLOW_ERROR;
        }
    }

    gst_buffer_unmap(buf, &minfo);

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

static GstFlowReturn gst_toupcam_src_alloc(GstPushSrc * psrc,
                                           GstBuffer ** buf)
{
    GstFlowReturn ret;

    GstToupCamSrc *src = GST_TOUPCAM_SRC(psrc);

    *buf = gst_buffer_new_allocate(NULL, src->image_bytes_out, NULL);
    if (G_UNLIKELY(*buf == NULL)) {
        GST_DEBUG_OBJECT(src, "Failed to allocate %u bytes",
                         src->image_bytes_out);
        ret = GST_FLOW_ERROR;
    }
    ret = GST_FLOW_OK;

    return ret;
}

// Override the push class fill fn, using the default create and alloc fns.
// buf is the buffer to fill, it may be allocated in alloc or from a downstream
// element. Other functions such as deinterlace do not work with this type of
// buffer.
static GstFlowReturn gst_toupcam_src_fill(GstPushSrc * psrc,
                                          GstBuffer * buf)
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
