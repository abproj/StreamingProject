#include <string.h>
#include <math.h>

#include <gst/gst.h>

/*
 * A simple RTP receiver 
*/

/* 
 *  the caps of the sender RTP stream. This is usually negotiated out of band 
 *  with SDP or RTSP.
 */
#define VIDEO_CAPS "application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264"
//#define AUDIO_CAPS "application/x-rtp,media=(string)audio,clock-rate=(int)44100,encoding-name=(string)MPEG4-GENERIC,encoding-params=(string)1,streamtype=(string)5,profile-level-id=(string)2,mode=(string)AAC-hbr,config=(string)1208,sizelength=(string)13,indexlength=(string)3,indexdeltalength=(string)3,ssrc=(uint)853015980,payload=(int)96,clock-base=(uint)2040203639,seqnum-base=(uint)52067"

#define AUDIO_CAPS "application/x-rtp,media=(string)audio,clock-rate=(int)44100,encoding-name=(string)MPEG4-GENERIC,encoding-params=(string)1,streamtype=(string)5,profile-level-id=(string)2,mode=(string)AAC-hbr,config=(string)1208,sizelength=(string)13,indexlength=(string)3,indexdeltalength=(string)3"
#define VIDEO_DEPAY "rtph264depay"
#define VIDEO_DEC   "ffdec_h264"
#define VIDEO_SINK  "autovideosink"
#define VIDEO_CONV  "ffmpegcolorspace"

#define AUDIO_DEPAY "rtpmp4gdepay"
#define AUDIO_DEC   "faad"
#define AUDIO_SINK  "autoaudiosink"

/*
 *the destination machine to send RTCP to. This is the address of the sender 
 *and is used to send back the RTCP reports of this receiver. If the data is 
 *sent from another machine, change this address.
*/  

#define DEST_HOST     "127.0.0.1"     
#define LATENCY       100 

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
  g_print ("source stats: %s\n", str);

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

  g_print ("got RTCP from session %u, SSRC %u\n", sessid, ssrc);

  /* get the right session */
  g_signal_emit_by_name (rtpbin, "get-internal-session", sessid, &session);

  /* get the internal source (the SSRC allocated to us, the receiver */
  g_object_get (session, "internal-source", &isrc, NULL);
  print_source_stats (isrc);

  /* get the remote source that sent us RTCP */
  g_signal_emit_by_name (session, "get-source-by-ssrc", ssrc, &osrc);
  print_source_stats (osrc);
}

/* will be called when rtpbin has validated a payload that we can depayload */
static void
pad_added_cb (GstElement * rtpbin, GstPad * new_pad, GstElement* depayarray[])
{
  GstPad *sinkpad;
  GstPadLinkReturn lres;
  GstElement* depay;
  
  g_print ("new payload on pad: %s\n", GST_PAD_NAME (new_pad));

  if(strstr(GST_PAD_NAME (new_pad),"recv_rtp_src_0"))
  {
  depay=depayarray[0];
  sinkpad = gst_element_get_static_pad (depay, "sink");
  g_assert (sinkpad);

  lres = gst_pad_link (new_pad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);
  }
  else if(strstr(GST_PAD_NAME (new_pad),"recv_rtp_src_1"))
  {
  depay=depayarray[1];
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

/* build a pipeline equivalent to:
   gst-launch -v gstrtpbin name=rtpbin latency=$LATENCY                  \
   udpsrc caps=$VIDEO_CAPS port=5000 ! rtpbin.recv_rtp_sink_0         \
   rtpbin. ! $VIDEO_DEC ! $VIDEO_SINK                               \
   udpsrc port=5001 ! rtpbin.recv_rtcp_sink_0                         \
   rtpbin.send_rtcp_src_0 ! udpsink port=5005 host=$DEST sync=false async=false
*/


int main (int argc, char *argv[])
{
  GstElement *rtpbin, *rtpsrc, *rtcpsrc, *rtcpsink,*audiortpsrc,*audiortcpsrc,*audiortcpsink;

  GstElement *videodepay, *videodec, *videoconv, *videosink;
  GstElement *audiodepay, *audiodec, *audiosink;

  GstElement* array[2];
  GstElement *pipeline;
  GMainLoop *loop;
  GstCaps *caps;
  gboolean res;
  GstPadLinkReturn lres;
  GstPad *srcpad, *sinkpad;
  /* always init first */
  gst_init (&argc, &argv);

  /* the pipeline to hold everything */
  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);

  /* the udp src and source we will use for RTP and RTCP */
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
  g_object_set (rtcpsink, "port", 5005, "host", DEST_HOST, NULL);
  /* no need for synchronisation or preroll on the RTCP sink */
  g_object_set (rtcpsink, "async", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpsrc, rtcpsrc, rtcpsink, NULL);

  /* the depayloading and decoding */
  videodepay = gst_element_factory_make (VIDEO_DEPAY, "videodepay");
  g_assert (videodepay);

  videodec = gst_element_factory_make (VIDEO_DEC, "videodec");
  g_assert (videodec);

  /*the video playback and format conversion */
  videoconv = gst_element_factory_make ("ffmpegcolorspace", "videoconv");
  g_assert (videoconv);
  videosink = gst_element_factory_make (VIDEO_SINK, "videosink");
  g_assert (videosink);  

  /* add depayloading and playback to the pipeline and link */
  gst_bin_add_many (GST_BIN (pipeline), videodepay, videodec, videoconv,
       videosink, NULL);

  res =gst_element_link_many (videodepay, videodec, videoconv, videosink, NULL);
  g_assert (res == TRUE);

  /* the rtpbin element */
  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");
  g_assert (rtpbin);
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

  /***************************Audio*******************************************/

  /* the udp src and source we will use for RTP and RTCP */
  audiortpsrc = gst_element_factory_make ("udpsrc", "audiortpsrc");
  g_assert (audiortpsrc);
  g_object_set (audiortpsrc, "port", 5002, NULL);

  /* we need to set caps on the udpsrc for the RTP data */
  caps = gst_caps_from_string (AUDIO_CAPS);
  g_object_set (audiortpsrc, "caps", caps, NULL);
  gst_caps_unref (caps);

  audiortcpsrc = gst_element_factory_make ("udpsrc", "audiortcpsrc");
  g_assert (audiortcpsrc);
  g_object_set (audiortcpsrc, "port", 5003, NULL);

  audiortcpsink = gst_element_factory_make ("udpsink", "audiortcpsink");
  g_assert (audiortcpsink);
  g_object_set (audiortcpsink, "port", 5005, "host", DEST_HOST, NULL);
  /* no need for synchronisation or preroll on the RTCP sink */
  g_object_set (audiortcpsink, "async", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiortpsrc, audiortcpsrc, audiortcpsink, NULL);

  /* the depayloading and decoding */
  audiodepay = gst_element_factory_make (AUDIO_DEPAY, "audiodepay");
  g_assert (audiodepay);

  audiodec = gst_element_factory_make (AUDIO_DEC, "audiodec");
  g_assert (audiodec);

  /*the audio playback  */
  audiosink = gst_element_factory_make (AUDIO_SINK, "audiosink");
  g_assert (audiosink);


  /* add depayloading and playback to the pipeline and link */
  gst_bin_add_many (GST_BIN (pipeline), audiodepay, audiodec, audiosink, NULL);

  res =gst_element_link_many (audiodepay, audiodec, audiosink, NULL);
  g_assert (res == TRUE);
  
  /* now link all to the rtpbin, start by getting an RTP sinkpad for session 1*/
  srcpad = gst_element_get_static_pad (audiortpsrc, "src");
  sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_1");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP sinkpad in session 1 */
  srcpad = gst_element_get_static_pad (audiortcpsrc, "src");
  sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtcp_sink_1");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP srcpad for sending RTCP back to the sender */
  srcpad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_1");
  sinkpad = gst_element_get_static_pad (audiortcpsink, "sink");
  lres = gst_pad_link (srcpad, sinkpad);
  g_assert (lres == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);

  array[0]=videodepay;
  array[1]=audiodepay;
  /* the RTP pad that we have to connect to the depayloader will be created
   * dynamically so we connect to the pad-added signal, pass the depayloader as
   * user_data so that we can link to it. */
     g_signal_connect (rtpbin, "pad-added", G_CALLBACK (pad_added_cb), array);

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


