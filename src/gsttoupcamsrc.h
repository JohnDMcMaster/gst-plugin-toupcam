/* GStreamer ToupCam Plugin
 * Copyright (C) 2020 
 *
 * Author Kishore Arepalli <kishore.arepalli@gmail.com>
 */

#ifndef _GST_TOUPCAM_SRC_H_
#define _GST_TOUPCAM_SRC_H_

#include <gst/base/gstpushsrc.h>

#include  <toupcam.h>

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
    HToupcam hCam;  // device handle
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
