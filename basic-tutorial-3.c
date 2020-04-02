#include <gst/gst.h>

typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *audio_convert;
  GstElement *audio_resample;
  GstElement *audio_sink;
  GstElement *video_convert;
  GstElement *video_sink;
} CustomData;


static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int
main (int argc, char *argv[])
{
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  gboolean terminate = FALSE;

  gst_init (&argc, &argv);

  data.source = gst_element_factory_make ("uridecodebin", "source");
  data.audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
  data.audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
  data.audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");

  data.video_convert = gst_element_factory_make ("videoconvert", "video_convert");
  data.video_sink = gst_element_factory_make ("autovideosink", "video_sink");

  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.source || !data.audio_convert || !data.audio_resample
      || !data.audio_sink || !data.video_convert || !data.video_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.audio_convert,
      data.audio_resample, data.audio_sink, data.video_convert, data.video_sink, NULL);

  if (!gst_element_link_many (data.audio_convert, data.audio_resample, data.audio_sink, NULL)) {
    g_printerr ("Audio elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  if (!gst_element_link_many (data.video_convert, data.video_sink, NULL)) {
    g_printerr ("Video elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  g_object_set (data.source, "uri",
      "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm",
      NULL);

  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);

  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  bus = gst_element_get_bus (data.pipeline);

  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug_info);
        g_printerr ("Error received from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        terminate = TRUE;
        break;
      case GST_MESSAGE_EOS:
        g_print ("EOS reached.\n");
        terminate = TRUE;
        break;
      case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
          GstState old_state, new_state, pending_state;
          gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
          g_print ("Pipeline state changed from %s to %s:\n",
              gst_element_state_get_name (old_state),
              gst_element_state_get_name (new_state));
        }
        break;
      default:
        g_printerr ("Unexpected message received.\n");
        break;
      }
    }
    gst_message_unref (msg);
  } while (!terminate);
}

static void
pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data)
{
  GstPad *sink_pad = NULL;
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad),
      GST_ELEMENT_NAME (src));

  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);

  if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    sink_pad = gst_element_get_static_pad (data->audio_convert, "sink");
  } else if (g_str_has_prefix (new_pad_type, "video/x-raw")) {
    sink_pad = gst_element_get_static_pad (data->video_convert, "sink");
  } else {
    g_print ("It has type '%s' which is not raw audio nor raw video. Ignoring.\n", new_pad_type);
    goto exit;
  }

  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked, Ignoring.\n");
    goto exit;
  }


  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' bus link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  gst_object_unref (sink_pad);
}