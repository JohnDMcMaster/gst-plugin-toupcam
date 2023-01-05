/* GStreamer ToupCam Plugin
 * Copyright (C) 2022 Labsmore LLC
 *
 * Author John McMaster <johndmcmaster@gmail.com>
 * Author Kishore Arepalli <kishore.arepalli@gmail.com>
 */

#ifndef _GST_TOUPCAM_SRC_H_
#define _GST_TOUPCAM_SRC_H_

#include <gst/base/gstpushsrc.h>

/*
ToupTek Photonics SDK gets rebranded to a few other things
Ease integration with other variants


Ex:
TOUPCAM_API(HToupcam) Toupcam_OpenByIndex(unsigned index);
NNCAM_API(HNncam) Nncam_OpenByIndex(unsigned index);
etc

TODO: add tucsen rebrand

*/

// XXX: SDK_BRANDING
#define CAMSDK_TOUPTEK
// Amscope
//#define CAMSDK_AMCAM
//? "MIView", Came with 25 MP camera
//#define CAMSDK_NNCAM
// Swift
//#define CAMSDK_SWIFTCAM

/*
Ex: toupcamsdk.h: Version: 53.21522.20221011 => 53

In general we support only the latest version
But some backwards compatibility is ok when easy / convenient
Ex: we have a non-public SDK release but also want to support latest public
release
*/
#define CAMSDK_VERSION 53

#if defined(CAMSDK_TOUPTEK)
#include <toupcam.h>
#define camsdk(x) Toupcam##x
#define camsdk_(x) Toupcam_##x
#define CAMSDK_(x) TOUPCAM_##x
#define CAMSDK_HANDLE HToupcam
#elif defined(CAMSDK_AMCAM)
#include <amcam.h>
#define camsdk(x) Amcam##x
#define camsdk_(x) Amcam_##x
#define CAMSDK_(x) AMCAM_##x
#elif defined(CAMSDK_NNCAM)
#include <nncam.h>
#define camsdk(x) Nncam##x
#define camsdk_(x) Nncam_##x
#define CAMSDK_(x) NNCAM_##x
#define CAMSDK_HANDLE HNncam
#elif defined(CAMSDK_SWIFTCAM)
#include <swiftcam.h>
#define camsdk(x) Swiftcam##x
#define camsdk_(x) Swiftcam_##x
#define CAMSDK_(x) SWIFTCAM_##x
#else
#error Need SDK brand
#endif

G_BEGIN_DECLS
#define GST_TYPE_TOUPCAM_SRC (gst_toupcam_src_get_type())
#define GST_TOUPCAM_SRC(obj)                                                   \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_TOUPCAM_SRC, GstToupCamSrc))
#define GST_TOUPCAM_SRC_CLASS(klass)                                           \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_TOUPCAM_SRC, GstToupCamSrcClass))
#define GST_IS_TOUPCAM_SRC(obj)                                                \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_TOUPCAM_SRC))
#define GST_IS_TOUPCAM_SRC_CLASS(obj)                                          \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_TOUPCAM_SRC))
typedef struct _GstToupCamSrc GstToupCamSrc;
typedef struct _GstToupCamSrcClass GstToupCamSrcClass;

struct _GstToupCamSrc {
    GstPushSrc base_toupcam_src;

    // device
    /*
       hmm...code diverged?
       Version: 50.19728.20211022
       typedef struct ToupcamT { int unused; } *HToupcam, *HToupCam;
       53.21522.20221011
       typedef struct Nncam_t { int unused; } *HNncam;
     */
    CAMSDK_HANDLE hCam;         // device handle
    gboolean raw;
    gboolean x16;
    gint esize;
    gint nWidth;
    gint nHeight;
    gint image_bytes_in;
    gint bytes_per_pix_in;
    gint bits_per_pix_out;
    gint bytes_per_pix_out;
    gint image_bytes_out;
    gint m_total;
    gint gst_stride;            // Stride/pitch for the GStreamer buffer

    unsigned char *frame_buff;

    // gst properties
    gdouble framerate;
    gdouble maxframerate;
    // library based properties
    // bool
    int hflip;
    // bool
    int vflip;
    // bool
    int auto_exposure;
    // unsigned
    unsigned expotime;
    // ints
    int hue;
    int saturation;
    int brightness;
    int contrast;
    int gamma;
    unsigned short black_balance[3];
    int white_balance[3];
    int awb_rgb;
    int awb_tt;

    // stream
    gboolean acq_started;
    gint n_frames;
    gint total_timeouts;
    GstClockTime duration;
    GstClockTime last_frame_time;
    gint imagesAvailable;
    gint imagesPulled;
    GMutex mutex;
    GCond cond;
};

struct _GstToupCamSrcClass {
    GstPushSrcClass base_toupcam_src_class;
};

GType gst_toupcam_src_get_type(void);

G_END_DECLS
#endif
