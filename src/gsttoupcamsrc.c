/* GStreamer ToupCam Plugin
 * Copyright (C) 2020 
 *
 * Author John McMaster <johndmcmaster@gmail.com>
 * Author Kishore Arepalli <kishore.arepalli@gmail.com>
 */
/**
 * SECTION:element-gsttoupcamsrc
 *
 * The toupcamsrc element is a source for a USB 3 camera supported by the ToupCam SDK.
 * A live source, operating in push mode.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v toupcamsrc ! autovideosink
 * ]|
 * </refsect2>
 */

#include <unistd.h> // for usleep
#include <string.h> // for memcpy

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include "toupcam.h"

#include "gsttoupcamsrc.h"

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_toupcam_src_debug);
#define GST_CAT_DEFAULT gst_toupcam_src_debug

/* prototypes */
static void gst_toupcam_src_set_property (GObject * object,
        guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_toupcam_src_get_property (GObject * object,
        guint property_id, GValue * value, GParamSpec * pspec);
static void gst_toupcam_src_dispose (GObject * object);
static void gst_toupcam_src_finalize (GObject * object);

static gboolean gst_toupcam_src_start (GstBaseSrc * src);
static gboolean gst_toupcam_src_stop (GstBaseSrc * src);
static GstCaps *gst_toupcam_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_toupcam_src_set_caps (GstBaseSrc * src, GstCaps * caps);

static GstFlowReturn gst_toupcam_src_fill (GstPushSrc * src, GstBuffer * buf);

//static GstCaps *gst_toupcam_src_create_caps (GstToupCamSrc * src);
static void gst_toupcam_src_reset (GstToupCamSrc * src);
enum
{
    PROP_0,
    PROP_CAMERAPRESENT,
    PROP_ESIZE,
    PROP_HFLIP,
    PROP_VFLIP,
    PROP_AUTO_EXPOSURE,
    PROP_EXPOTIME,
    PROP_HUE,
    PROP_SATURATION,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
    PROP_GAMMA,
};

#define PROP_CAMERAPRESENT       FALSE

// Put matching type text in the pad template below

#define TOUPCAM_OPTION_BYTEORDER_RGB    0
#define TOUPCAM_OPTION_BYTEORDER_BGR    1

#define DEFAULT_PROP_AUTO_EXPOSURE		TRUE
#define DEFAULT_PROP_EXPOTIME		    0
#define MIN_PROP_EXPOTIME		        0
//FIXME: GUI max is 15. However we will time out after 5 sec
#define MAX_PROP_EXPOTIME		        5000000
#define DEFAULT_PROP_HFLIP		        FALSE
#define DEFAULT_PROP_VFLIP		        FALSE
#define DEFAULT_PROP_HUE                TOUPCAM_HUE_DEF
#define DEFAULT_PROP_SATURATION         TOUPCAM_SATURATION_DEF
#define DEFAULT_PROP_BRIGHTNESS         TOUPCAM_BRIGHTNESS_DEF
#define DEFAULT_PROP_CONTRAST           TOUPCAM_CONTRAST_DEF
#define DEFAULT_PROP_GAMMA              TOUPCAM_GAMMA_DEF


// pad template
static GstStaticPadTemplate gst_toupcam_src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                GST_PAD_SRC,
                GST_PAD_ALWAYS,
                GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
                        ("{ RGB }"))
        );

/* class initialisation */

G_DEFINE_TYPE (GstToupCamSrc, gst_toupcam_src, GST_TYPE_PUSH_SRC);

static void
gst_toupcam_src_class_init (GstToupCamSrcClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "toupcamsrc", 0,
            "ToupCam Camera source");

    gobject_class->set_property = gst_toupcam_src_set_property;
    gobject_class->get_property = gst_toupcam_src_get_property;
    gobject_class->dispose = gst_toupcam_src_dispose;
    gobject_class->finalize = gst_toupcam_src_finalize;

    gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&gst_toupcam_src_template));

    gst_element_class_set_static_metadata (gstelement_class,
            "ToupCam Video Source", "Source/Video",
            "ToupCam Camera video source", "John McMaster <johndmcmaster@gmail.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_toupcam_src_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_toupcam_src_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_toupcam_src_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_toupcam_src_set_caps);

    gstpushsrc_class->fill   = GST_DEBUG_FUNCPTR (gst_toupcam_src_fill);
    GST_DEBUG ("Using gst_toupcam_src_fill.");

    // Install GObject properties
    // Camera Present property
    g_object_class_install_property (gobject_class, PROP_CAMERAPRESENT,
            g_param_spec_boolean ("devicepresent", "Camera Device Present", "Is the camera present and connected OK?",
                    FALSE, G_PARAM_READABLE));
    g_object_class_install_property (gobject_class, PROP_ESIZE,
            g_param_spec_int ("esize", "Camera size enumeration", "...",
                    0, 2, 0, G_PARAM_READABLE | G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_HFLIP,
            g_param_spec_boolean ("hflip", "Horizontal flip", "Horizontal flip",
                    DEFAULT_PROP_HFLIP, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_VFLIP,
            g_param_spec_boolean ("vflip", "Vertical flip", "Vertical flip",
                    DEFAULT_PROP_VFLIP, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_AUTO_EXPOSURE,
            g_param_spec_boolean ("auto_exposure", "Auto exposure", "Auto exposure",
                    DEFAULT_PROP_AUTO_EXPOSURE, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_EXPOTIME,
            g_param_spec_int ("expotime", "Exposure us", "...",
                    MIN_PROP_EXPOTIME, MAX_PROP_EXPOTIME, DEFAULT_PROP_EXPOTIME, G_PARAM_READABLE | G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_HUE,
            g_param_spec_int ("hue", "...", "...",
                    TOUPCAM_HUE_MIN, TOUPCAM_HUE_MAX, TOUPCAM_HUE_DEF, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_SATURATION,
            g_param_spec_int ("saturation", "...", "...",
                    TOUPCAM_SATURATION_MIN, TOUPCAM_SATURATION_MAX, TOUPCAM_SATURATION_DEF, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
            g_param_spec_int ("brightness", "...", "...",
                    TOUPCAM_BRIGHTNESS_MIN, TOUPCAM_BRIGHTNESS_MAX, TOUPCAM_BRIGHTNESS_DEF, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_CONTRAST,
            g_param_spec_int ("contrast", "...", "...",
                    TOUPCAM_CONTRAST_MIN, TOUPCAM_CONTRAST_MAX, TOUPCAM_CONTRAST_DEF, G_PARAM_READABLE | G_PARAM_WRITABLE));
    g_object_class_install_property (gobject_class, PROP_GAMMA,
            g_param_spec_int ("gamma", "...", "...",
                    TOUPCAM_GAMMA_MIN, TOUPCAM_GAMMA_MAX, TOUPCAM_GAMMA_DEF, G_PARAM_READABLE | G_PARAM_WRITABLE));
}

static void
gst_toupcam_src_init (GstToupCamSrc * src)
{
    src->auto_exposure = DEFAULT_PROP_AUTO_EXPOSURE;
    src->expotime = DEFAULT_PROP_EXPOTIME;
    src->vflip = DEFAULT_PROP_VFLIP;
    src->hflip = DEFAULT_PROP_HFLIP;
    src->hue = DEFAULT_PROP_HUE;
    src->saturation = DEFAULT_PROP_SATURATION;
    src->brightness = DEFAULT_PROP_BRIGHTNESS;
    src->contrast = DEFAULT_PROP_CONTRAST;
    src->gamma = DEFAULT_PROP_GAMMA;

    /* set source as live (no preroll) */
    gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

    g_mutex_init(&src->mutex);
    g_cond_init(&src->cond);
    gst_toupcam_src_reset (src);
}

static void
gst_toupcam_src_reset (GstToupCamSrc * src)
{
    src->hCam = 0;
    src->cameraPresent = FALSE;
    src->imagesAvailable = 0;
    src->imagesPulled = 0;
    src->total_timeouts = 0;
    src->last_frame_time = 0;
    src->m_total = 0;
}

void
gst_toupcam_src_set_property (GObject * object, guint property_id,
        const GValue * value, GParamSpec * pspec)
{
    GstToupCamSrc *src;

    src = GST_TOUPCAM_SRC (object);

    switch (property_id) {
    case PROP_CAMERAPRESENT:
        src->cameraPresent = g_value_get_boolean (value);
        break;
    case PROP_ESIZE:
        src->esize = g_value_get_int (value);
        break;
    case PROP_HFLIP:
        src->hflip = g_value_get_boolean (value);
        break;
    case PROP_VFLIP:
        src->vflip = g_value_get_boolean (value);
        break;
    case PROP_AUTO_EXPOSURE:
        src->auto_exposure = g_value_get_boolean (value);
        break;
    case PROP_EXPOTIME:
        src->expotime = g_value_get_int (value);
        break;
    case PROP_HUE:
        src->hue = g_value_get_int (value);
        break;
    case PROP_SATURATION:
        src->saturation = g_value_get_int (value);
        break;
    case PROP_BRIGHTNESS:
        src->brightness = g_value_get_int (value);
        break;
    case PROP_CONTRAST:
        src->contrast = g_value_get_int (value);
        break;
    case PROP_GAMMA:
        src->gamma = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_toupcam_src_get_property (GObject * object, guint property_id,
        GValue * value, GParamSpec * pspec)
{
    GstToupCamSrc *src;

    g_return_if_fail (GST_IS_TOUPCAM_SRC (object));
    src = GST_TOUPCAM_SRC (object);

    switch (property_id) {
    case PROP_CAMERAPRESENT:
        g_value_set_boolean (value, src->cameraPresent);
        break;
    case PROP_ESIZE:
        g_value_set_int (value, src->esize);
        break;
    case PROP_HFLIP: 
        g_value_set_boolean (value, src->hflip);
        break;
    case PROP_VFLIP:
        g_value_set_boolean (value, src->vflip);
        break;
    case PROP_AUTO_EXPOSURE:
        g_value_set_boolean (value, src->auto_exposure);
        break;
    case PROP_EXPOTIME:
        g_value_set_int (value, src->expotime);
        break;

    case PROP_HUE:
        g_value_set_int (value, src->hue);
        break;
    case PROP_SATURATION:
        g_value_set_int (value, src->saturation);
        break;
    case PROP_BRIGHTNESS:
        g_value_set_int (value, src->brightness);
        break;
    case PROP_CONTRAST:
        g_value_set_int (value, src->contrast);
        break;
    case PROP_GAMMA:
        g_value_set_int (value, src->gamma);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_toupcam_src_dispose (GObject * object)
{
    GstToupCamSrc *src;

    g_return_if_fail (GST_IS_TOUPCAM_SRC (object));
    src = GST_TOUPCAM_SRC (object);

    GST_DEBUG_OBJECT (src, "dispose");

    // clean up as possible.  may be called multiple times

    G_OBJECT_CLASS (gst_toupcam_src_parent_class)->dispose (object);
}

void
gst_toupcam_src_finalize (GObject * object)
{
    GstToupCamSrc *src;

    g_return_if_fail (GST_IS_TOUPCAM_SRC (object));
    src = GST_TOUPCAM_SRC (object);

    GST_DEBUG_OBJECT (src, "finalize");

    /* clean up object here */
    G_OBJECT_CLASS (gst_toupcam_src_parent_class)->finalize (object);
}


static void EventCallback(unsigned nEvent, void* pCallbackCtx)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC (pCallbackCtx);
    GST_DEBUG_OBJECT (src, "event callback: %d\n", nEvent);
    if (TOUPCAM_EVENT_IMAGE == nEvent)
    {
        g_mutex_lock(&src->mutex);
        src->imagesAvailable++;
        g_cond_signal(&src->cond);
        g_mutex_unlock(&src->mutex);
    }
    else
    {
        GST_DEBUG_OBJECT (src, "event callback: %d\n", nEvent);
    }
    printf("calllback %u (want %u), images now %u\n", nEvent, TOUPCAM_EVENT_IMAGE, src->imagesAvailable);
}

static gboolean
gst_toupcam_src_start (GstBaseSrc * bsrc)
{
    // Start will open the device but not start it, set_caps starts it, stop should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC (bsrc);

    GST_DEBUG_OBJECT (src, "start");
    printf("start\n");

    // Turn on automatic timestamping, if so we do not need to do it manually, BUT there is some evidence that automatic timestamping is laggy
//    gst_base_src_set_do_timestamp(bsrc, TRUE);

    // read libversion (for informational purposes only)
    GST_INFO_OBJECT (src, "ToupCam Library Ver %s", Toupcam_Version());

    // open first usable device
    GST_DEBUG_OBJECT (src, "Toupcam_Open");
    src->hCam = Toupcam_Open(NULL);
    if (NULL == src->hCam)
    {
        printf("failed open\n");
        GST_ERROR_OBJECT(src, "No ToupCam device found or open failed");
        goto fail;
    }

    if (1) {
        int itmp;
        short stmp;
        unsigned short ustmp;
        char buff[64];

        printf("Camera info\n");
        printf("  max bit depth: %d\n", Toupcam_get_MaxBitDepth(src->hCam));
        printf("  max fan speed: %d\n", Toupcam_get_FanMaxSpeed(src->hCam));
        printf("  max frame speed: %d\n", Toupcam_get_MaxSpeed(src->hCam));
        printf("  mono mode: %d\n", Toupcam_get_MonoMode(src->hCam));
        int resn = Toupcam_get_StillResolutionNumber(src->hCam);
        printf("  still resolution number: %d\n", resn);
        for (int resi = 0; resi < resn; ++resi) {
            int width, height;
            if (!FAILED(Toupcam_get_StillResolution(src->hCam, resi, &width, &height))) {
                printf("    %u: %i x %i\n", resi, width, height);
            }
            float pixx, pixy;
            if (!FAILED(Toupcam_get_PixelSize(src->hCam, resi, &pixx, &pixy))) {
                printf("    %u: %0.1f x %0.1f um\n", resi, pixx, pixy);
            }
        }

        if (!FAILED(Toupcam_get_Negative(src->hCam, &itmp))) {
            printf("  negative: %d\n", itmp);
        }
        if (!FAILED(Toupcam_get_Chrome(src->hCam, &itmp))) {
            printf("  chrome: %d\n", itmp);
        }
        if (!FAILED(Toupcam_get_HZ(src->hCam, &itmp))) {
            printf("  hz: %d\n", itmp);
        }
        if (!FAILED(Toupcam_get_Mode(src->hCam, &itmp))) {
            printf("  mode: %d\n", itmp);
        }
        if (!FAILED(Toupcam_get_RealTime(src->hCam, &itmp))) {
            printf("  real time: %d\n", itmp);
        }

        if (!FAILED(Toupcam_get_Temperature(src->hCam, &stmp))) {
            printf("  temperature: %d\n", stmp);
        }
        if (!FAILED(Toupcam_get_Revision(src->hCam, &ustmp))) {
            printf("  revision: %d\n", ustmp);
        }

        if (!FAILED(Toupcam_get_SerialNumber(src->hCam, buff))) {
            printf("  serial number: %s\n", buff);
        }
        if (!FAILED(Toupcam_get_FwVersion(src->hCam, buff))) {
            printf("  fw version: %s\n", buff);
        }
        if (!FAILED(Toupcam_get_HwVersion(src->hCam, buff))) {
            printf("  hw version: %s\n", buff);
        }
        if (!FAILED(Toupcam_get_ProductionDate(src->hCam, buff))) {
            printf("  production date: %s\n", buff);
        }
        if (!FAILED(Toupcam_get_FpgaVersion(src->hCam, buff))) {
            printf("  fpga version: %s\n", buff);
        }
        /*
        if (!FAILED(Toupcam_get_Name(id, buff))) {
            printf("  name: %s\n", buff);
        }
        */
        //0 => #define TOUPCAM_PIXELFORMAT_RAW8             0x00
        Toupcam_get_Option(src->hCam, TOUPCAM_OPTION_PIXEL_FORMAT, &itmp);
            printf("  pixel format: %i\n", itmp);


        char nFourCC[4];
        unsigned bitsperpixel;
        Toupcam_get_RawFormat(src->hCam, (unsigned *)&nFourCC, &bitsperpixel);
        //raw code GBRG, bpp 8
        //needs this to get the full 12 bit
        //Toupcam_put_Option(src->hCam, TOUPCAM_OPTION_PIXEL_FORMAT, TOUPCAM_PIXELFORMAT_RAW12);
        //raw code GBRG, bpp 12
        printf("  raw code %c%c%c%c, bpp %u\n", nFourCC[0], nFourCC[1], nFourCC[2], nFourCC[3], bitsperpixel);
    }


    src->cameraPresent = TRUE;

    HRESULT hr;
    hr = Toupcam_put_eSize(src->hCam, src->esize);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to set size, hr = %08x", hr);
        goto fail;
    }
    hr = Toupcam_get_Size(src->hCam, &src->nWidth, &src->nHeight);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(src, "failed to get size, hr = %08x", hr);
        goto fail;
    }

    hr = Toupcam_StartPullModeWithCallback(src->hCam, EventCallback, src);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT (src, "failed to start camera, hr = %08x", hr);
        goto fail;
    }

    // Colour format


    /*
    //can set raw8 and raw12, but not raw16
    //default raw8
    hr = Toupcam_put_Option(src->hCam, TOUPCAM_OPTION_PIXEL_FORMAT, TOUPCAM_PIXELFORMAT_RAW12);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT (src, "failed to set pixel format, hr = %08x", hr);
        goto fail;
    }
    */

    Toupcam_put_Option(src->hCam, TOUPCAM_OPTION_BYTEORDER, TOUPCAM_OPTION_BYTEORDER_RGB);
    Toupcam_put_HFlip(src->hCam, src->hflip);
    Toupcam_put_VFlip(src->hCam, src->vflip);
    hr = Toupcam_put_AutoExpoEnable(src->hCam, src->auto_exposure);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT (src, "failed to auto exposure, hr = %08x", hr);
        goto fail;
    }
    if (!src->auto_exposure) {
        //setting this severely interferes with auto exposure
        Toupcam_put_ExpoTime(src->hCam, src->expotime);
    }

    Toupcam_put_Hue(src->hCam, src->hue);
    Toupcam_put_Saturation(src->hCam, src->saturation);
    Toupcam_put_Brightness(src->hCam, src->brightness);
    Toupcam_put_Contrast(src->hCam, src->contrast);
    Toupcam_put_Gamma(src->hCam, src->gamma);
    
    //real time
    //Toupcam_put_RealTime(src->hCam, 1);

    /*
    //maybe rgb gain better, but not well documented
    int aGain = {10, 10, 10};
    Toupcam_put_WhiteBalanceGain(src->hCam, aGain);

    unsigned us = 50000;
    for (unsigned rgb = 1; rgb < 4; ++rgb) {
        Toupcam_put_Option(src->hCam, TOUPCAM_OPTION_SEQUENCER_EXPOTIME | rgb, us);
    }
    */



    // We support just colour of one type, BGR 24-bit, I am not attempting to support all camera types
    src->nBitsPerPixel = 24;
    unsigned nFrame, nTime, nTotalFrame;
    Toupcam_get_FrameRate(src->hCam, &nFrame, &nTime, &nTotalFrame);
    src->framerate = nFrame * 1000.0 / nTime;
    src->duration = 1000000000.0/src->framerate;

    src->nBytesPerPixel = (src->nBitsPerPixel+1)/8;
    src->nImageSize = src->nWidth * src->nHeight * src->nBytesPerPixel;
    GST_DEBUG_OBJECT (src, "Image is %d x %d, pitch %d, bpp %d, Bpp %d", src->nWidth, src->nHeight, src->nPitch, src->nBitsPerPixel, src->nBytesPerPixel);

    return TRUE;

fail:
    if (src->hCam) {
        src->hCam = 0;
    }

    return FALSE;
}

static gboolean
gst_toupcam_src_stop (GstBaseSrc * bsrc)
{
    // Start will open the device but not start it, set_caps starts it, stop should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC (bsrc);

    GST_DEBUG_OBJECT (src, "stop");
    Toupcam_Close(src->hCam);

    gst_toupcam_src_reset (src);

    return TRUE;
}

static GstCaps *
gst_toupcam_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
    GstToupCamSrc *src = GST_TOUPCAM_SRC (bsrc);
    GstCaps *caps;

    if (src->hCam == 0) {
        caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));
    } else {
        GstVideoInfo vinfo;

        // Create video info 
        gst_video_info_init (&vinfo);

        vinfo.width = src->nWidth;
        vinfo.height = src->nHeight;

        // Frames per second fraction n/d, 0/1 indicates a frame rate may vary
        vinfo.fps_n = 0;
        vinfo.fps_d = 1;
        vinfo.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

        vinfo.finfo = gst_video_format_get_info (GST_VIDEO_FORMAT_RGB);

        // cannot do this for variable frame rate
        //src->duration = gst_util_uint64_scale_int (GST_SECOND, vinfo.fps_d, vinfo.fps_n); // NB n and d are wrong way round to invert the fps into a duration.

        caps = gst_video_info_to_caps (&vinfo);
    }

    GST_INFO_OBJECT (src, "The caps are %" GST_PTR_FORMAT, caps);

    if (filter) {
        GstCaps *tmp = gst_caps_intersect (caps, filter);
        gst_caps_unref (caps);
        caps = tmp;

        GST_INFO_OBJECT (src, "The caps after filtering are %" GST_PTR_FORMAT, caps);
    }

    return caps;
}

static gboolean
gst_toupcam_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
    // Start will open the device but not start it, set_caps starts it, stop should stop and close it (as v4l2src)

    GstToupCamSrc *src = GST_TOUPCAM_SRC (bsrc);
    GstVideoInfo vinfo;
    //GstStructure *s = gst_caps_get_structure (caps, 0);

    GST_INFO_OBJECT (src, "The caps being set are %" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps (&vinfo, caps);

    if (GST_VIDEO_INFO_FORMAT (&vinfo) != GST_VIDEO_FORMAT_UNKNOWN) {
        g_assert (src->hCam != 0);
        //  src->vrm_stride = get_pitch (src->device);  // wait for image to arrive for this
        src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 0);
        src->nHeight = vinfo.height;
    } else {
        goto unsupported_caps;
    }

    // start freerun/continuous capture
    src->acq_started = TRUE;

    return TRUE;

unsupported_caps:
    GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

// Override the push class fill fn, using the default create and alloc fns.
// buf is the buffer to fill, it may be allocated in alloc or from a downstream element.
// Other functions such as deinterlace do not work with this type of buffer.
static GstFlowReturn
gst_toupcam_src_fill (GstPushSrc * psrc, GstBuffer * buf)
{
    printf("\n");
    GstToupCamSrc *src = GST_TOUPCAM_SRC (psrc);
    GstMapInfo minfo;

    printf("waiting for new image\n");

    // lock next (raw) image for read access, convert it to the desired
    // format and unlock it again, so that grabbing can go on

    // Wait for the next image to be ready
    int timeout = 5;
    while (src->imagesAvailable <= src->imagesPulled) {
            gint64 end_time;
            g_mutex_lock(&src->mutex);
            end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
            if (!g_cond_wait_until(&src->cond, &src->mutex, end_time)) {
                printf("timed out waiting for image, timeout=%u\n", timeout);
                // timeout has passed.
                //g_mutex_unlock (&src->mutex); // return here if needed
                //return NULL;
            }
            g_mutex_unlock (&src->mutex);
            timeout--;
            if (timeout <= 0) {
                // did not return an image. why?
                // ----------------------------------------------------------
                GST_ERROR_OBJECT(src, "WaitEvent timed out.");
                return GST_FLOW_ERROR;
            }
    }

    //  successfully returned an image
    // ----------------------------------------------------------

    // Copy image to buffer in the right way

    gst_buffer_map (buf, &minfo, GST_MAP_WRITE);

    // From the grabber source we get 1 progressive frame
    ToupcamFrameInfoV2 info = { 0 };
    printf("pulling new image\n");
    HRESULT hr = Toupcam_PullImageV2(src->hCam, minfo.data, 24, &info);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT (src, "failed to pull image, hr = %08x", hr);
        gst_buffer_unmap (buf, &minfo);
        return GST_FLOW_ERROR;
    }
    src->imagesPulled += 1;
    /* After we get the image data, we can do anything for the data we want to do */
    printf("pull image ok, total = %u, resolution = %u x %u\n", ++src->m_total, info.width, info.height);
    printf("flag %u, seq %u, us %u\n", info.flag, info.seq, info.timestamp);

    gst_buffer_unmap (buf, &minfo);

    /*
    // If we do not use gst_base_src_set_do_timestamp() we need to add timestamps manually
    src->last_frame_time += src->duration;   // Get the timestamp for this frame
    if (!gst_base_src_get_do_timestamp(GST_BASE_SRC(psrc))){
        GST_BUFFER_PTS(buf) = src->last_frame_time;  // convert ms to ns
        GST_BUFFER_DTS(buf) = src->last_frame_time;  // convert ms to ns
    }
    GST_BUFFER_DURATION(buf) = src->duration;
    printf("pts, dts: %" GST_TIME_FORMAT ", duration: %ld ms\n", GST_TIME_ARGS (src->last_frame_time), GST_TIME_AS_MSECONDS(src->duration));
    */
    GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;
    

    // count frames, and send EOS when required frame number is reached
    GST_BUFFER_OFFSET(buf) = src->n_frames;  // from videotestsrc
    src->n_frames++;
    GST_BUFFER_OFFSET_END(buf) = src->n_frames;  // from videotestsrc
    if (psrc->parent.num_buffers>0)  // If we were asked for a specific number of buffers, stop when complete
        if (G_UNLIKELY(src->n_frames >= psrc->parent.num_buffers)) {
            printf("EOS\n");
            return GST_FLOW_EOS;
        }

    // see, if we had to drop some frames due to data transfer stalls. if so,
    // output a message

    return GST_FLOW_OK;
}

