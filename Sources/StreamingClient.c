#include <string.h>
#include <gst/gst.h>

/*
 * RTP client(receiver) 
*/

#define MAXBUFFLEN 256

/*The caps of the sender RTP stream.*/

#define VIDEO_CAPS "application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264"

#define VIDEO_DEPAY "rtph264depay"
#define VIDEO_DEC   "ffdec_h264"
#define VIDEO_SINK  "xvimagesink"
#define VIDEO_CONV  "ffmpegcolorspace"

/*config parameters */
static char *host = NULL;

/* print the stats of a source */
static void
print_source_stats (GObject * source)
{
  GstStructure *stats;
  gchar *str;

  g_return_if_fail (source != NULL);

  /* get the source stats */
  g_object_get (source, "stats", &stats, NULL);

  /* simply dump the stats structure */
  str = gst_structure_to_string (stats);
  g_print ("source stats: %s\n\n", str);

  gst_structure_free (stats);
  g_free (str);
}

/* will be called when gstrtpbin signals on-ssrc-active. It means that an RTCP
 * packet was received from another source. */
static void
on_ssrc_active_cb (GstElement * rtpbin, guint sessid, guint ssrc,
    GstElement * depay)
{
  GObject *session, *isrc, *osrc;

  g_print("**********************************************************\n");
  g_print ("\nGot RTCP from session %u, SSRC %u\n", sessid, ssrc);

  /* get the right session */
  g_signal_emit_by_name (rtpbin, "get-internal-session", sessid, &session);

  /* get the internal source (the SSRC allocated to us, the receiver */
  g_print("\n Receiver's or Internal source's RTCP details:\n");
  g_object_get (session, "internal-source", &isrc, NULL);
  print_source_stats (isrc);

  /* get the remote source that sent us RTCP */
  g_print("\n Sender's or Remote source's  RTCP details:\n"); 
  g_signal_emit_by_name (session, "get-source-by-ssrc", ssrc, &osrc);
  print_source_stats (osrc);
}

/* will be called when rtpbin has validated a payload that we can depayload */
static void
pad_added_cb (GstElement * rtpbin, GstPad * new_pad, GstElement* depay)
{
  GstPad *sinkpad;
  GstPadLinkReturn lres;
  
  g_print ("new payload on pad: %s\n", GST_PAD_NAME (new_pad));

  if(strstr(GST_PAD_NAME (new_pad),"recv_rtp_src_0"))
  {
  sinkpad = gst_element_get_static_pad (depay, "sink");
  g_assert (sinkpad);

  lres = gst_pad_link (new_pad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);
  }
  else
  {
     g_print ("pad: %s not connected\n", GST_PAD_NAME (new_pad));
  }

}

/*Parse the config file*/
static void config_parser()
{
  char line[MAXBUFFLEN];
  int linenum=0;
  FILE* fp;

  fp = fopen ("client_config.txt","r");
  if (!fp)
  {
    printf("config file read issue\n");
    return ;
  }

  while(fgets(line, MAXBUFFLEN, fp) != NULL)
  {
        char param[MAXBUFFLEN], value[MAXBUFFLEN];

        linenum++;
        if(line[0] == '#') continue;

        if(sscanf(line, "%s %s", param, value) != 2)
        {
                fprintf(stderr, "Syntax error, line %d\n", linenum);
                continue;
        }

        if (!strcmp(param,"host"))
           host = g_strdup(value);
        else
            printf("Invalid parameters\n");

  }
  fclose(fp);
}


int main (int argc, char *argv[])
{
  GstElement *rtpbin, *rtpsrc, *rtcpsrc, *rtcpsink;

  GstElement *videodepay, *videodec, *videoconv, *videosink, *fsink;
  GstElement *pipeline;
  GstElement *tee, *fileQueue, *displayQueue;
  GMainLoop *loop;
  GstCaps *caps;
  gboolean res;
  GstPadLinkReturn lres;
  GstPad *srcpad, *sinkpad;

  config_parser();

  /*Initialize gstreamer lib*/
  gst_init (&argc, &argv);

  /* the pipeline to hold everything */
  pipeline = gst_pipeline_new ("client_pipeline");
  g_assert (pipeline);

  /* the udpsrc we will use for RTP and RTCP */
  rtpsrc = gst_element_factory_make ("udpsrc", "rtpsrc");
  g_assert (rtpsrc);
  g_object_set (rtpsrc, "port", 5000, NULL);

  /* we need to set caps on the udpsrc for the RTP data */
  caps = gst_caps_from_string (VIDEO_CAPS);
  g_object_set (rtpsrc, "caps", caps, NULL);
  gst_caps_unref (caps); 

  rtcpsrc = gst_element_factory_make ("udpsrc", "rtcpsrc");
  g_assert (rtcpsrc);
  g_object_set (rtcpsrc, "port", 5001, NULL);

  rtcpsink = gst_element_factory_make ("udpsink", "rtcpsink");
  g_assert (rtcpsink);
  g_object_set (rtcpsink, "port", 5005, "host",host, NULL);
  /* no need for synchronisation or preroll on the RTCP sink */
  g_object_set (rtcpsink, "async", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpsrc, rtcpsrc, rtcpsink, NULL);

  /* the rtpbin element */
  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");
  g_assert (rtpbin);
 
  /*Set Jitter Buffer latency*/
  g_object_set(rtpbin,"latency", 500, NULL);
  gst_bin_add (GST_BIN (pipeline), rtpbin);

  /* now link all to the rtpbin, start by getting an RTP sinkpad for session 0*/
  srcpad = gst_element_get_static_pad (rtpsrc, "src");
  sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_0");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP sinkpad in session 0 */
  srcpad = gst_element_get_static_pad (rtcpsrc, "src");
  sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtcp_sink_0");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP srcpad for sending RTCP back to the sender */
  srcpad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_0");
  sinkpad = gst_element_get_static_pad (rtcpsink, "sink");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);


  /* the depayloading and decoding */
  videodepay = gst_element_factory_make (VIDEO_DEPAY, "videodepay");
  g_assert (videodepay);

  /* Tee that sends the decoded stream to multiple outputs */
   tee = gst_element_factory_make("tee", "tee");
   g_assert(tee);

  /* Queue creates new thread to display the decoded stream */
   displayQueue = gst_element_factory_make("queue", "displayqueue");
   g_assert(displayQueue);


  videodec = gst_element_factory_make (VIDEO_DEC, "videodec");
  g_assert (videodec);

  /*the video playback and format conversion */
  videoconv = gst_element_factory_make ("ffmpegcolorspace", "videoconv");
  g_assert (videoconv);

  videosink = gst_element_factory_make (VIDEO_SINK, "videosink");
  g_assert (videosink); 

  
  /* add depayloading and playback to the pipeline and link */
  gst_bin_add_many (GST_BIN (pipeline), videodepay, tee, displayQueue, videodec,                    videoconv, videosink, NULL);

  res =gst_element_link_many (videodepay, tee, displayQueue, videodec, videoconv                              ,videosink, NULL);
  g_assert (res == TRUE);
  
  /* Queue creates new thread to store the decoded stream in file */
   fileQueue = gst_element_factory_make("queue", "filequeue");
   g_assert(fileQueue);
   
   fsink = gst_element_factory_make ("filesink", "filesink");
   g_assert(fsink);
   g_object_set (G_OBJECT(fsink), "location","./recvstrm.264", NULL);

   gst_bin_add_many (GST_BIN (pipeline), fileQueue, fsink, NULL);
   
   if(!gst_element_link_many (tee, fileQueue, NULL))
   {
      g_error ("Failed to link tee, fileQueue");
   }

   if (!gst_element_link_many (fileQueue, fsink, NULL))
   {
      g_error ("Failed to link fileQueue and file sink");
   }


  /* the RTP pad that we have to connect to the depayloader will be created
   * dynamically so we connect to the pad-added signal, pass the depayloader as
   * user_data so that we can link to it. */
     g_signal_connect (rtpbin, "pad-added", G_CALLBACK (pad_added_cb), videodepay);

  /* give some stats when we receive RTCP */
  g_signal_connect (rtpbin, "on-ssrc-active", G_CALLBACK (on_ssrc_active_cb),
  videodepay);
  
  /* set the pipeline to playing */
  g_print ("starting receiver pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  
  g_print ("stopping receiver pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  gst_object_unref (pipeline);

  return 0;
}
