#include <string.h>
#include <math.h>
//#include <glib.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
//#include <gst/controller/gstcontroller.h>

/* A simple RTP server*/

/* change this to send the RTP data and RTCP to another host */
//#define DEST_HOST "10.0.0.5"
//#define DEST_HOST "127.0.0.1"
#define VOFFSET   "0"
#define AOFFSET    "0"


/*H264 encode from the source*/

#define VIDEO_SRC  "v4l2src"
#define VIDEO_ENC  "x264enc"
#define VIDEO_PAY  "rtph264pay"


/*AAC encode from the source*/
#define AUDIO_SRC  "autoaudiosrc"
#define AUDIO_ENC  "faac"
#define AUDIO_PAY  "rtpmp4gpay"

/*Default values of the arguments*/
static int width  = 320;
static int height = 240;
static int fps    = 15;
static int bitrate = 300;
static char *host = NULL;

/* print the stats of a source */
static void print_source_stats (GObject * source)
{
  GstStructure *stats;
  gchar *str;
  
  /* get the source stats */
  g_object_get (source, "stats", &stats, NULL);

   /* simply dump the stats structure */
  str = gst_structure_to_string (stats);
  g_print ("source stats: %s\n", str);

  gst_structure_free (stats);
  g_free (str);
}

/* this function is called every second and dumps the RTP manager stats */
static gboolean
print_stats (GstElement * rtpbin)
{
  GObject *session;
  GValueArray *arr;
  GValue *val;
  guint i;

  g_print ("***********************************\n");

  /* get session 0 */
  g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);

  /* print all the sources in the session, this includes the internal source */
  g_object_get (session, "sources", &arr, NULL);

  for (i = 0; i < arr->n_values; i++) {
    GObject *source;

    val = g_value_array_get_nth (arr, i);
     source = g_value_get_object (val);

    print_source_stats (source);
  }
  g_value_array_free (arr);

  g_object_unref (session);

  return TRUE;
}

static GOptionEntry options[] ={
		{ "width", 'w', 0, G_OPTION_ARG_INT, &width,
				"enter video width", NULL },
		{ "height", 'h', 0, G_OPTION_ARG_INT, &height,
				"enter video height", NULL },
		{ "fps", 'f', 0, G_OPTION_ARG_INT, &fps,
				"enter video frame rate", NULL },
		{ "bitrate", 'b', 0, G_OPTION_ARG_INT, &bitrate,
				"enter video bit rate", NULL },
		{ "host", 'd', 0, G_OPTION_ARG_STRING, &host,
				"enter receiver IP", NULL },
		{ NULL }
};

static void arguments_parse(int argcount, char** argvar)
{
       
        GOptionContext *ctx;
        GError *err = NULL;
        //GOptionGroup *group;
 

	// must initialise the threading system before using any other GLib funtion
 	if (!g_thread_supported ())
   		g_thread_init (NULL);
       
        ctx = g_option_context_new ("video_streaming_server");
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_add_group (ctx, gst_init_get_option_group ());

        /*group = g_option_group_new("sender", "mcn_streaming Sender Options:",
			  "Show mcn_streaming sender options", NULL, NULL);
	g_option_group_add_entries(group, options);
        g_option_context_add_group(ctx, group);

	//Need this for gst option initialization
	g_option_context_add_group (ctx, gst_init_get_option_group ());*/

        //g_option_context_free (ctx);

         if (!g_option_context_parse (ctx, &argcount, &argvar, &err)) 
         {
	    g_print ("Failed to initialize: %s\n", err->message);
	    g_error_free (err);
	  }
       
        
        //Default receiver IP to localhost
	if ( !host )
        {
	   host = g_strdup("127.0.0.1");
	 } 
} 

static gboolean
get_rtcp_packet(GstRTCPPacket *packet)
{
        guint32 rtptime, packet_count, octet_count;
	guint64 ntptime;
	guint count, i;
        
        guint32 exthighestseq, jitter, lsr, dlsr, ssrc;
        guint8 fractionlost;
        gint32 packetslost;

	count = gst_rtcp_packet_get_rb_count(packet);
	g_print("    count         %d", count);
	for (i=0; i<count; i++)
        {
		gst_rtcp_packet_get_rb(packet, i, &ssrc, &fractionlost,
				&packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

		g_print("    block         %d\n", i);
		g_print("    ssrc          %d\n", ssrc);
		g_print("    highest   seq %d\n", exthighestseq);
		g_print("    jitter        %d\n", jitter);
		g_print("    fraction lost %d\n", fractionlost);
		g_print("    packet   lost %d\n", packetslost);

	}

	return TRUE;
}

static gboolean
received_rtcp_packet(GstElement *src, GstBuffer *buf, gpointer data)
{
   GstElement *videoenc;
   GValue v = {0, };
   GValue * bitrateval = &v;
   int int_v ;
   guint count, i;
   gboolean nextpckt;
   GstRTCPPacket packet;
    GstRTCPType type;

  if (!gst_rtcp_buffer_validate(buf))
		g_debug("Received invalid RTCP packet");

   g_print("Received RTCP packet\n");
      
   buf = gst_buffer_make_metadata_writable(buf);
   nextpckt = gst_rtcp_buffer_get_first_packet(buf, &packet);
   while(nextpckt)
   {
	type = gst_rtcp_packet_get_type(&packet);
	switch (type)
        {
        	case GST_RTCP_TYPE_RR:
			//send_event_to_encoder(venc, &rtcp_pkt);
                       // g_print("Packet Type : %d\n",type);  
                        get_rtcp_packet(&packet); 			
			break;
		default:
			g_print("Other type : %d\n",type);
			break;
	}
	nextpckt = gst_rtcp_packet_move_to_next(&packet);
        g_print("Next Packet analyze\n");
    }

   videoenc = (GstElement *)(data);
   
   g_value_init(&v, G_TYPE_INT);
   g_object_get_property(G_OBJECT(videoenc), "bitrate" , &v);
   
   int_v = g_value_get_int(bitrateval);
   g_print("Current bitrate value:%d\n",int_v);
  // bitrate= bitrate + 100;
   
   g_print("In received_rtcp_packet encoder set up\n"); 
   g_object_set (videoenc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",bitrate,NULL);

     
   return TRUE;
}

int main (int argc, char *argv[])
{
  GstElement *videosrc,*audiosrc, *videorate,*audioconv,*videoenc, *audioenc,*videopay, *audiopay;
  GstElement *rtpbin, *rtpsink, *rtcpsink, *rtcpsrc,*audiortpsink,*audiortcpsink,*audiortcpsrc, *identity;
  GstElement *pipeline,*videoqueue;
  GMainLoop *loop;
  GstPad *srcpad, *sinkpad;
  GstCaps *caps;
  gboolean link_ok;

  arguments_parse(argc, argv);
  
  /*Initializes the GStreamer library,registering built-in 
   *elements, and loading standard plugins.Always init first.
   */
  gst_init (&argc, &argv);

  /* The pipeline to hold everything */
  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline); 

  /*the video capture and format conversion*/
  videosrc = gst_element_factory_make (VIDEO_SRC,"videosrc");
  g_assert(videosrc);   

  videoqueue = gst_element_factory_make("queue", "videoqueue");
  g_assert(videoqueue);

  videorate = gst_element_factory_make ("videorate","videorate");
  g_assert(videorate);
  
  /*Encodes video to H264 stream and RTP payloading*/
  videoenc = gst_element_factory_make (VIDEO_ENC, "videoenc");
  g_assert (videoenc);
  
  g_print("Initial encoder set up\n"); 
  g_object_set (videoenc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",bitrate,NULL);
   
  videopay = gst_element_factory_make (VIDEO_PAY, "videopay");
  g_assert (videopay);
  

  /* The audio capture and format conversion */
  audiosrc = gst_element_factory_make (AUDIO_SRC, "audiosrc");
  g_assert (audiosrc);

  
  audioconv = gst_element_factory_make ("audioconvert", "audioconv");
  g_assert (audioconv);

  /*Encodes audio to AAC stream and RTP payloading*/
  audioenc = gst_element_factory_make (AUDIO_ENC, "audioenc");
  g_assert (audioenc);
  
  audiopay = gst_element_factory_make (AUDIO_PAY, "audiopay");
  g_assert (audiopay);


  /*Add video capture and payloading to the pipeline and link */ 
  gst_bin_add_many (GST_BIN (pipeline),videosrc, videoqueue ,videorate,
  videoenc, videopay,NULL);

  if (!gst_element_link_many (videosrc, videoqueue, videorate,NULL))
          
  {
    g_error ("Failed to link videosrc, videoqueue, videorate");
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv","width",  G_TYPE_INT, width,
		        	"height", G_TYPE_INT, height,
                                "framerate", GST_TYPE_FRACTION,fps , 1,NULL);

  link_ok = gst_element_link_filtered (videorate, videoenc, caps);
  gst_caps_unref (caps);

  if (!link_ok)
  {
    g_warning ("Failed to link videorate and videoenc");
  }

  if (!gst_element_link_many (videoenc,videopay, NULL))
  {
    g_error ("Failed to link video encoder and video payloader");

  }
 
 /* The rtpbin element */
  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");
  g_assert (rtpbin);

  gst_bin_add (GST_BIN (pipeline), rtpbin);

  /* The udp sinks and source we will use for RTP and RTCP */
  rtpsink = gst_element_factory_make ("udpsink", "rtpsink");
  g_assert (rtpsink);

  g_object_set (rtpsink, "port", 5000,"host",host, NULL);
  g_object_set(rtpsink,"ts-offset",VOFFSET,NULL);

  rtcpsink = gst_element_factory_make ("udpsink", "rtcpsink");
  g_assert (rtcpsink);
  g_object_set (rtcpsink, "port", 5001,"host",host, NULL);
  

  /* No need for synchronisation or preroll on the RTCP sink */
  g_object_set (rtcpsink, "async", FALSE, "sync", FALSE, NULL);

  rtcpsrc = gst_element_factory_make ("udpsrc", "rtcpsrc");
  g_assert (rtcpsrc);
  
  g_object_set (rtcpsrc, "port", 5005, NULL);


  /*identity element connected to rtcpsrc, to check if received any packets or not*/
	identity = gst_element_factory_make("identity", "udpsrc-rtcp-identity");
	if ( !identity ) 
	{
		g_printerr("Failed to create identity element\n");
		return 0;
	}
	/*set identity element to sync to clock*/
	g_object_set(G_OBJECT (identity), "sync", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpsink, rtcpsink, rtcpsrc,identity, NULL);

  /* Now link all to the rtpbin,start by getting an RTP sinkpad for session 0*/
  sinkpad = gst_element_get_request_pad (rtpbin, "send_rtp_sink_0");
  srcpad = gst_element_get_static_pad (videopay, "src");

  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link video payloader to rtpbin");
   gst_object_unref (srcpad);
  
  /* Get the RTP srcpad that was created when we requested the sinkpad above
   * and link it to the rtpsink sinkpad.
   */
   srcpad = gst_element_get_static_pad (rtpbin, "send_rtp_src_0");
   sinkpad = gst_element_get_static_pad (rtpsink, "sink");

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to rtpsink");
   gst_object_unref (srcpad);
   gst_object_unref (sinkpad);

  /* get an RTCP srcpad for sending RTCP to the receiver */
   srcpad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_0");
   sinkpad = gst_element_get_static_pad (rtcpsink, "sink");

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to rtcpsink");
   gst_object_unref (sinkpad);
   gst_object_unref (srcpad); 

  /* We also want to receive RTCP, request an RTCP sinkpad for session 0 and
   * link it to the srcpad of the udpsrc for RTCP */
   srcpad = gst_element_get_static_pad (rtcpsrc, "src");
   sinkpad = gst_element_get_static_pad (identity, "sink");
   gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (received_rtcp_packet), videoenc);

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtcpsrc to identity");
   gst_object_unref (srcpad);
   gst_object_unref (sinkpad);


   srcpad = gst_element_get_static_pad (identity, "src");
   sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtcp_sink_0");

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link identity to rtpbin");
   gst_object_unref (srcpad);
   gst_object_unref (sinkpad);


  /********************************Audio********************************/
  /*Add audio capture and payloading to the pipeline and link */
  gst_bin_add_many (GST_BIN (pipeline), audiosrc, audioconv, audioenc, audiopay,NULL);

  if (!gst_element_link_many (audiosrc,audioconv,audioenc,audiopay,NULL))
  {
    g_error ("Failed to link audiosrc, audioconv, audioenc,audiopay");
  }

  /* The udp sinks and source we will use for RTP and RTCP */
  audiortpsink = gst_element_factory_make ("udpsink", "audiortpsink");
  g_assert (audiortpsink);

  g_object_set (audiortpsink, "port", 5002,"host",host, NULL);
  g_object_set(audiortpsink,"ts-offset",AOFFSET,NULL);

  audiortcpsink = gst_element_factory_make ("udpsink", "audiortcpsink");
  g_assert (audiortcpsink);
  g_object_set (audiortcpsink, "port", 5003,"host",host, NULL);

  /* No need for synchronisation or preroll on the RTCP sink */
  g_object_set (audiortcpsink, "async", FALSE, "sync", FALSE, NULL);

  audiortcpsrc = gst_element_factory_make ("udpsrc", "audiortcpsrc");
  g_assert (audiortcpsrc);

  g_object_set (audiortcpsrc, "port", 5007, NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiortpsink, audiortcpsink, audiortcpsrc, NULL);

  /* Now link all to the rtpbin,start by getting an RTP sinkpad for session 1*/
  sinkpad = gst_element_get_request_pad (rtpbin, "send_rtp_sink_1");
  srcpad = gst_element_get_static_pad (audiopay, "src");

  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link audio payloader to rtpbin");
  gst_object_unref (srcpad);

  /* Get the RTP srcpad that was created when we requested the sinkpad above
   * and link it to the audiortpsink sinkpad.
   */
   srcpad = gst_element_get_static_pad (rtpbin, "send_rtp_src_1");
   sinkpad = gst_element_get_static_pad (audiortpsink, "sink");

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to audiortpsink");
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* get an RTCP srcpad for sending RTCP to the receiver */
   srcpad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_1");
   sinkpad = gst_element_get_static_pad (audiortcpsink, "sink");

   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtpbin to audiortcpsink");
  gst_object_unref (sinkpad);

  /* We also want to receive RTCP, request an RTCP sinkpad for session 1 and
   * link it to the srcpad of the udpsrc for RTCP 
   */
   srcpad = gst_element_get_static_pad (audiortcpsrc, "src");
   sinkpad = gst_element_get_request_pad (rtpbin, "recv_rtcp_sink_1");
   if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK)
    g_error ("Failed to link rtcpsrc to rtpbin");
   gst_object_unref (srcpad);
  

  /* Set the pipeline to playing */
  g_print ("starting sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  	


  /* print stats every second */
  g_timeout_add (1000, (GSourceFunc) print_stats, rtpbin);
  //g_object_set (videoenc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",400,NULL);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_print ("stopping sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
