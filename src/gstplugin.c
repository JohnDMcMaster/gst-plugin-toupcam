#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttoupcamsrc.h"

#define GST_CAT_DEFAULT gst_gsttoupcam_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#include <stdio.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
printf("registering...\n");
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "toupcamsrc", 0,
      "debug category for ToupCam elements");

  return gst_element_register (plugin, "toupcamsrc", GST_RANK_NONE,
          GST_TYPE_TOUPCAM_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    toupcamsrc,
    "ToupCam camera source element.",
    plugin_init, VERSION, "BSD", PACKAGE_NAME, "http://www.touptek.com")
