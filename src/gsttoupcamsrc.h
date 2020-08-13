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
  gboolean cameraPresent;
  gboolean hflip;
  gboolean vflip;
  gint nWidth;
  gint nHeight;
  gint nBitsPerPixel;
  gint nBytesPerPixel;
  gint nPitch;   // Stride in bytes between lines
  gint nImageSize;  // Image size in bytes
  gint m_total;
  gint gst_stride;  // Stride/pitch for the GStreamer buffer

  // gst properties
  gdouble framerate;
  gdouble maxframerate;

  // stream
  gboolean acq_started;
  gint n_frames;
  gint total_timeouts;
  GstClockTime duration;
  GstClockTime last_frame_time;
  gint imagesAvailable;
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
