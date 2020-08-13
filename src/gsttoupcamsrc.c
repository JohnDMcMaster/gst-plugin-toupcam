/* GStreamer ToupCam Plugin
 * Copyright (C) 2020 
 *
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
    PROP_CAMERAPRESENT
};


#define PROP_CAMERAPRESENT       FALSE

#define DEFAULT_TOUPCAM_VIDEO_FORMAT GST_VIDEO_FORMAT_BGR
// Put matching type text in the pad template below

// pad template
static GstStaticPadTemplate gst_toupcam_src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                GST_PAD_SRC,
                GST_PAD_ALWAYS,
                GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
                        ("{ BGR }"))
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
            "ToupCam Camera video source", "Kishore Arepalli <kishore.arepalli@gmail.com>");

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
}

static void
gst_toupcam_src_init (GstToupCamSrc * src)
{
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

    // Turn on automatic timestamping, if so we do not need to do it manually, BUT there is some evidence that automatic timestamping is laggy
//    gst_base_src_set_do_timestamp(bsrc, TRUE);

    // read libversion (for informational purposes only)
    GST_INFO_OBJECT (src, "ToupCam Library Ver %s", Toupcam_Version());

    // open first usable device
    GST_DEBUG_OBJECT (src, "Toupcam_Open");
    src->hCam = Toupcam_Open(NULL);
    if (NULL == src->hCam)
    {
        GST_ERROR_OBJECT(src, "No ToupCam device found or open failed");
        goto fail;
    }

    src->cameraPresent = TRUE;

    HRESULT hr = Toupcam_get_Size(src->hCam, &src->nWidth, &src->nHeight);
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

    vinfo.fps_n = 0;  vinfo.fps_d = 1;  // Frames per second fraction n/d, 0/1 indicates a frame rate may vary
    vinfo.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

    vinfo.finfo = gst_video_format_get_info (DEFAULT_TOUPCAM_VIDEO_FORMAT);

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

    // lock next (raw) image for read access, convert it to the desired
    // format and unlock it again, so that grabbing can go on

    // Wait for the next image to be ready
        int timeout = 5;
        unsigned startImages = src->imagesAvailable;
        while (startImages >= src->imagesAvailable) {
                gint64 end_time;
                g_mutex_lock(&src->mutex);
                end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
                if (!g_cond_wait_until(&src->cond, &src->mutex, end_time)) {
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
    printf("pull new image\n");
    HRESULT hr = Toupcam_PullImageV2(src->hCam, minfo.data, 24, &info);
    if (FAILED(hr)) {
        GST_ERROR_OBJECT (src, "failed to pull image, hr = %08x", hr);
        gst_buffer_unmap (buf, &minfo);
        return GST_FLOW_ERROR;
    }
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

