#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>

/* A simple RTP server*/

#define VOFFSET   "0"
#define AOFFSET    "0"

/*H264 encode from the source*/

#define VIDEO_SRC  "v4l2src"
#define VIDEO_ENC  "x264enc"
#define VIDEO_PAY  "rtph264pay"
#define PERCENTLOSS  5 
#define MAXBITRATE   42000  

/*Default values of the command line rguments*/
static int width  = 320;
static int height = 240;
static int fps    = 24;
static int bitrate = 40000;
static char *host = NULL;

static gint32 prevpacketslost = 0;
static gint32 newpacketslost = 0;
static int intervalcount = 0;
static gint32  prevlost = 0;

typedef struct rtcp_packet
{
	guint32 jitter;
	guint8  fractionlost;
	gint32  packetslost;
}rtcp_packet_detail;


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

	// must initialise the threading system before using any other GLib funtion
 	if (!g_thread_supported ())
   		g_thread_init (NULL);
       
        ctx = g_option_context_new ("video_streaming_server");
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_add_group (ctx, gst_init_get_option_group ());

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

/* print the stats of a source */
static void print_source_stats (GObject * source)
{
  GstStructure *stats;
  gchar *str;

  /* get the source stats */
  g_object_get (source, "stats", &stats, NULL);

   /* simply dump the stats structure */
  str = gst_structure_to_string (stats);
  g_print ("source stats: %s\n\n", str);

  gst_structure_free (stats);
  g_free (str);
}


static gboolean packet_analyze_cb(GstElement *videoenc)
{
   int totalbitslost;
   int percentbitslost;
   GValue v = {0, };
   GValue * bitrateval = &v;
   int int_v,uploadspeed ;
   int prevbitrate;

 
   g_value_init(&v, G_TYPE_INT);
   g_object_get_property(G_OBJECT(videoenc), "bitrate" , &v);

   int_v = g_value_get_int(bitrateval);
   g_print("Current bitrate value:%d\n",int_v);
  
   g_print("\n Packets lost in 3 seconds:%d\n",newpacketslost);
   
   /*Check for 15 seconds and no packets lost
     increase the bitrate 5% of the current bitrate*/
   if( newpacketslost == 0 && intervalcount < 3)
   {
         intervalcount += 1;
         if(intervalcount == 3)
         {
                prevbitrate = bitrate;
         	bitrate = bitrate + (0.05 * bitrate);
                if(bitrate <= MAXBITRATE)
        	{	g_print("bitrate value has been increased\n");
        		g_object_set (videoenc,"tune",0x00000004,"bitrate",bitrate,NULL);
		}
		else
		   	bitrate = prevbitrate;
		
          } 
          
   }    
   else
   {
	intervalcount = 0;	   	 
   }
   /* total bits lost in every second*/
   totalbitslost = (newpacketslost/3) * 8;
 
   //g_print("totalbitslost is :%d\n", totalbitslost);
  
   /* 30% of total bits lost*/
   percentbitslost =( totalbitslost * 0.30) ;
 
   //g_print("percentbitslost is :%d\n", percentbitslost);
  
   /*bitrate change if percentbitslost is 5
     or more, where 5 is thresold value*/
    if(percentbitslost >= PERCENTLOSS)
    {
	bitrate= bitrate - percentbitslost;
    	g_print("bitrate value has been  decreased\n");
        g_object_set (videoenc,"tune",0x00000004,"bitrate",bitrate,NULL);  
    }    
   newpacketslost = 0;
   return TRUE;
}


static gboolean upstream_event_to_encoder(GstElement *videoenc,rtcp_packet_detail *rtcppacket)
{

        GstPad *srcpad;
        GstEvent *event;
        GstStructure *eventstruct;
	

        eventstruct = gst_structure_new("GstForceKeyUnit",
                      "type",G_TYPE_STRING,"receiver_report",
                      "jitter",G_TYPE_UINT, rtcppacket->jitter,
		      "fractionlost", G_TYPE_UINT, rtcppacket->fractionlost,
                      "packetslost", G_TYPE_UINT, rtcppacket->packetslost,NULL);
           	  
         
        if(!eventstruct)
	{
            g_error("Failed to create upstream event structure\n");
            return FALSE;
        } 

        event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, eventstruct);
	if(!event) 	
        {  
	    g_error ("Failed to create upstream event\n");
            return FALSE;
        } 

        srcpad = gst_element_get_static_pad(videoenc, "src");
      
        if(!srcpad)
        {
            g_error ("Failed to create videoenc srcpad\n");
            return FALSE;
        }
     
        //send event to videoenc src pad
        g_print("Send upstream event to %s\n",gst_element_get_name(videoenc));
        g_print("Prev packt lost values %d\n", prevlost);
        
        g_print("packet lost%d\n",rtcppacket->packetslost);
           
        if((rtcppacket->packetslost - prevlost) >= 1)
            gst_pad_send_event(srcpad, event);

        prevlost = rtcppacket->packetslost ;
       
        gst_object_unref(srcpad);
          
        return TRUE;
} 

static gboolean
get_rtcp_packet(GstRTCPPacket *packet,GstElement *videoenc)
{
        guint count, i;
        guint32 ssrc, rtptime, packet_count, octet_count;
        rtcp_packet_detail rtcppacket;

        count = gst_rtcp_packet_get_rb_count(packet);
        g_print(" rb  count %d\n", count);
        for (i=0; i<count; i++)
        {

                guint32 exthighestseq, jitter, lsr, dlsr;

                guint8 fractionlost;
                gint32 packetslost;

                gst_rtcp_packet_get_rb(packet, i, &ssrc, &fractionlost,
                                &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

                g_print("    block         %d\n", i);
                g_print("    ssrc          %d\n", ssrc);
                g_print("    highest   seq %d\n", exthighestseq);
                g_print("    jitter        %d\n", jitter);
                g_print("    fraction lost %d\n", fractionlost);
                g_print("    packet   lost %d\n", packetslost);
                g_print("    last SR timestamp %d\n",lsr);
                g_print("    delay since last SR %d\n",dlsr);

                rtcppacket.jitter = jitter;
                rtcppacket.fractionlost = fractionlost;
                rtcppacket.packetslost = packetslost;

                newpacketslost = packetslost - prevpacketslost;
                prevpacketslost = packetslost;

                upstream_event_to_encoder(videoenc, &rtcppacket);

        }

        return TRUE;
}

static gboolean
received_rtcp_packet_cb(GstElement *src, GstBuffer *buf, gpointer data )
{
   GstElement *videoenc;
 /*  GValue v = {0, };
   GValue * bitrateval = &v;
   int int_v ;*/
   guint count, i;
   gboolean nextpckt;
   GstRTCPPacket packet;
   GstRTCPType type;
   

   videoenc = (GstElement *)(data); 
   
   if (!gst_rtcp_buffer_validate(buf))
		g_debug("Received invalid RTCP packet");

   g_print("***************************************************************\n");
   g_print("Received RTCP packet\n");
   
   buf = gst_buffer_make_metadata_writable(buf);
   nextpckt = gst_rtcp_buffer_get_first_packet(buf, &packet);
   while(nextpckt)
   {
	type = gst_rtcp_packet_get_type(&packet);
	switch (type)
        {
        	case GST_RTCP_TYPE_RR:
			g_print("Packet Type : %d\n",type);  
                        get_rtcp_packet(&packet,videoenc); 	
                        //upstream_event_to_encoder(videoenc, &rtcppacket); 
                        break;
		default:
			g_print("Other packet type : %d\n",type);
			break;
	}
	nextpckt = gst_rtcp_packet_move_to_next(&packet);
        //g_print("Next Packet analyze\n");
    }

  videoenc = (GstElement *)(data);
   
  /* g_value_init(&v, G_TYPE_INT);
   g_object_get_property(G_OBJECT(videoenc), "bitrate" , &v);
   
   int_v = g_value_get_int(bitrateval);
   g_print("Current bitrate value:%d\n",int_v);*/
  // bitrate= bitrate + 100;
   
  // g_object_set (videoenc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",bitrate,NULL);
     
   return TRUE;
}

/* will be called when gstrtpbin signals on-ssrc-active. It means that an RTCP
 * packet was received from another source. */
static void
on_ssrc_active_cb (GstElement * rtpbin, guint sessid, guint ssrc,
    GstElement * pay)
{
  GObject *session, *isrc, *osrc;

  g_print ("\nGot RTCP from session %u, SSRC %u\n", sessid, ssrc);

  /* get the right session */
  g_signal_emit_by_name (rtpbin, "get-internal-session", sessid, &session);

  /* get the internal source (the SSRC allocated to us, the receiver */
   g_print(" \nReceiver's or Internal source's RTCP details:\n");
  g_object_get (session, "internal-source", &isrc, NULL);
  print_source_stats (isrc);

  /* get the remote source that sent us RTCP */
  g_print("\nSender's or Remote source's  RTCP details:\n");
  g_signal_emit_by_name (session, "get-source-by-ssrc", ssrc, &osrc);
  print_source_stats (osrc);
}

static gboolean
 created_rtp_packet_cb(GstElement *src, GstBuffer *buf, gpointer data )
{

    guint packtlen;
    packtlen = gst_rtp_buffer_get_payload_len(buf);
    //totalpacktlen = totalpacktlen + packtlen ;

    //g_print("packet length %d\n",packtlen); 
    return TRUE;	
}

int main (int argc, char *argv[])
{
  GstElement *videosrc, *time, *videorate, *videoenc, *videopay;
  GstElement *rtpbin, *rtpsink, *rtcpsink, *rtcpsrc,*identity;
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

  time = gst_element_factory_make ("timeoverlay","vtimeoverlay");
  g_assert(time);

  videoqueue = gst_element_factory_make("queue", "videoqueue");
  g_assert(videoqueue);

  videorate = gst_element_factory_make ("videorate","videorate");
  g_assert(videorate);
 

  /*Encodes video to H264 stream and RTP payloading*/
  videoenc = gst_element_factory_make (VIDEO_ENC, "videoenc");
  g_assert (videoenc);
  
  g_print("Initial encoder set up\n"); 
  g_object_set (videoenc,"tune",0x00000004,"bitrate",bitrate,NULL);
   
  videopay = gst_element_factory_make (VIDEO_PAY, "videopay");
  g_assert (videopay);

  g_object_set(videopay, "mtu", 1024, NULL);
  
  /*Add video capture and payloading to the pipeline and link */ 
  gst_bin_add_many (GST_BIN (pipeline),videosrc, time, videoqueue ,videorate,videoenc, videopay,NULL);

  if (!gst_element_link_many (videosrc, time, videoqueue, videorate,NULL))
          
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

   /*Added buffer probe to measure the bandwidth or upload speed*/
   //gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (created_rtp_packet_cb),NULL);

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

  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (received_rtcp_packet_cb),videoenc);
   
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
  /* give some stats when we receive RTCP */
  g_signal_connect (rtpbin, "on-ssrc-active", G_CALLBACK (on_ssrc_active_cb),
  NULL);

  /* packet loss analyze in every 3 seconds */
  g_timeout_add (3000, (GSourceFunc) packet_analyze_cb,videoenc);

  
  /* Set the pipeline to playing */
  g_print ("starting sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);


  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_print ("stopping sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
