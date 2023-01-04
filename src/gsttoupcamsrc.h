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

//#define CAMSDK_TOUPTEK
#define CAMSDK_NNCAM
// Ex: toupcamsdk.h: Version: 53.21522.20221011
#define CAMSDK_VERSION 53

#if defined(CAMSDK_TOUPTEK)
#include  <toupcam.h>
#define camsdk(x) Toupcam ## x
#define camsdk_(x) Toupcam_ ## x
#define CAMSDK_(x) TOUPCAM_ ## x
#elif defined(CAMSDK_NNCAM)
#include <nncam.h>
#define camsdk(x) Nncam ## x
#define camsdk_(x) Nncam_ ## x
#define CAMSDK_(x) NNCAM_ ## x
#else
#error Need SDK brand
#endif



G_BEGIN_DECLS

#define GST_TYPE_TOUPCAM_SRC   (gst_toupcam_src_get_type())
#define GST_TOUPCAM_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TOUPCAM_SRC,GstToupCamSrc))
#define GST_TOUPCAM_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TOUPCAM_SRC,GstToupCamSrcClass))
#define GST_IS_TOUPCAM_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TOUPCAM_SRC))
#define GST_IS_TOUPCAM_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TOUPCAM_SRC))

typedef struct _GstToupCamSrc GstToupCamSrc;
typedef struct _GstToupCamSrcClass GstToupCamSrcClass;

struct _GstToupCamSrc
{
    GstPushSrc base_toupcam_src;

    // device
    /*
    hmm...code diverged?
    Version: 50.19728.20211022
    typedef struct ToupcamT { int unused; } *HToupcam, *HToupCam;
    53.21522.20221011
    typedef struct Nncam_t { int unused; } *HNncam;
    */
#if CAMSDK_VERSION >= 53
    HNncam hCam;  // device handle
#else
    HToupcam hCam;
#endif
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
    gint gst_stride;  // Stride/pitch for the GStreamer buffer

    unsigned char *frame_buff;

    // gst properties
    gdouble framerate;
    gdouble maxframerate;
    // library based properties
    //bool
    int hflip;
    //bool
    int vflip;
    //bool
    int auto_exposure;
    //unsigned
    unsigned expotime;
    //ints
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

struct _GstToupCamSrcClass
{
  GstPushSrcClass base_toupcam_src_class;
};

GType gst_toupcam_src_get_type (void);

G_END_DECLS

#endif
