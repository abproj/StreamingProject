#include <gst/gst.h>
#include <glib.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <string.h>

/*RTP server(sender)*/

#define VOFFSET   "0"
#define MAXBUFFLEN 256

/*config parameters */
static int width ; 
static int height ; 
static int fps_num ; 
static int fps_den;
static int bitrate ;
/*IDR interval is 10 seconds*/
static int IDRintv ;
static char *host = NULL; 


/*Parse the config file*/
static void config_parser()
{
  char line[MAXBUFFLEN];
  int linenum=0;
  FILE* fp;

  fp = fopen ("server_config.txt","r");
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
        if(!strcmp(param,"width"))
           width =atoi(value);
        else if(!strcmp(param,"height"))
           height =atoi(value);
        else if(!strcmp(param,"fps_num"))
           fps_num =atoi(value);
        else if(!strcmp(param,"fps_den"))
           fps_den =atoi(value);
        else if(!strcmp(param,"bitrate"))
           bitrate =atoi(value);
        else if(!strcmp(param,"IDRintv")) 
           IDRintv =atoi(value);
        else if (!strcmp(param,"host"))
           host = g_strdup(value); 
        else
            printf("Invalid parameters\n");
            
  }
  fclose(fp);
}

/*Set encoder properties*/
static gboolean set_enc_prop(GstElement *enc)
{
   g_object_set(G_OBJECT(enc), "bitrate" ,bitrate ,"key-int-max",IDRintv, "aud",FALSE,"b-adapt",FALSE,"b-pyramid",FALSE,"bframes",0,"byte-stream", TRUE,"cabac",FALSE,"dct8x8",FALSE,"interlaced",FALSE,"ip-factor",1.4,"pb-factor",1.3,"qp-max",51,"qp-min",10,"qp-step",4,"quantizer", 21,NULL);

   g_object_set(G_OBJECT(enc),"ref" , 1,"sps-id",0,"vbv-buf-capacity",500,"weightb",FALSE,NULL);

   g_object_set(G_OBJECT(enc),"intra-refresh",TRUE,"mb-tree",TRUE,NULL);

   g_object_set(G_OBJECT(enc),"rc-lookahead",40,"sync-lookahead",-1,NULL);

   g_object_set(G_OBJECT(enc),"profile", 1,"psy-tune",0,"speed-preset",6,"tune",0x00000004,NULL);

   return TRUE;
}

static gboolean bus_call 
	(GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *source, *videoParse, *encX264, *sink;
  GstElement *tee, *fileQueue, *streamQueue;
  GstElement *videopay, *rtpbin, *rtpsink, *rtcpsink, *rtcpsrc,*identity;
  GstPad *srcpad, *sinkpad;
  GstBus *bus;

  config_parser();  

  /*Initialize gstreamer lib*/
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Commandline arguments */
  if (argc < 2) {
    g_printerr ("Usage: %s <YUV filename>\n", argv[0]);
    return -1;
   }

   pipeline = gst_pipeline_new ("server_pipeline");
   g_assert (pipeline);

   source = gst_element_factory_make ("filesrc", "file-source");
   g_assert(source);
   /* Set the input filename to the source element */
   g_object_set (G_OBJECT (source), "location", argv[1], NULL);

   videoParse = gst_element_factory_make ("videoparse", "videoparse1");
   g_assert(videoParse);
   g_object_set (G_OBJECT (videoParse), "format", 2, "width", width, "height", height,"framerate",fps_num,fps_den,NULL);

   encX264 = gst_element_factory_make ("x264enc", "codecX264");
   g_assert(encX264);
   set_enc_prop(encX264);

   /* Tee that sends the encoded stream to multiple outputs */
   tee = gst_element_factory_make("tee", "tee");
   g_assert(tee);

  /* Queue creates new thread to store the encoded stream in file */
   fileQueue = gst_element_factory_make("queue", "filequeue");
   g_assert(fileQueue);
 
   sink = gst_element_factory_make ("filesink", "filesink");
   g_assert(sink);
   g_object_set (G_OBJECT(sink), "location","./sendstrm.264", NULL);


  /*Set up the pipeline and add a message handler*/
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);
 
	
  gst_bin_add_many (GST_BIN (pipeline), source, videoParse, encX264, tee, fileQueue, sink, NULL);

  /* we link the elements together */
  if(!gst_element_link_many(source, videoParse, NULL))
  {
    g_error ("Failed to link source, videoParse");
  }
  
  if(!gst_element_link_many (videoParse, encX264, NULL))
  {
    g_error ("Failed to link videoparse, encX264");
  }
  
  if(!gst_element_link_many (encX264, tee, NULL))
  {
    g_error ("Failed to link encX264, tee");
  }

  if(!gst_element_link_many (tee, fileQueue, NULL))
  {
    g_error ("Failed to link tee, fileQueue");
  }

  if(!gst_element_link_many (fileQueue, sink, NULL))
  {
    g_error ("Failed to link fileQueue, sink");
  }


/* Queue creates new thread for videostreaming of the encoded stream over the network */
  streamQueue = gst_element_factory_make("queue", "streamingqueue");
  g_assert(streamQueue);

  videopay = gst_element_factory_make ("rtph264pay", "videopay");
  g_assert (videopay);

  g_object_set(videopay, "mtu", 1024, NULL);

  /* The rtpbin element */
  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");
  g_assert (rtpbin);

  /* The udp sinks and source we will use for RTP and RTCP */
  rtpsink = gst_element_factory_make ("udpsink", "rtpsink");
  g_assert (rtpsink);

  g_object_set (rtpsink,"port", 5000, "host",host, NULL);
  g_object_set(rtpsink,"ts-offset",VOFFSET,NULL);

  rtcpsink = gst_element_factory_make ("udpsink", "rtcpsink");
  g_assert (rtcpsink);
  g_object_set (rtcpsink,"port", 5001, "host",host, NULL);

  /* No need for synchronisation or preroll on the RTCP sink */
  g_object_set (rtcpsink, "async", FALSE, "sync", FALSE, NULL);
 
  g_free(host); 

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

  gst_bin_add_many (GST_BIN (pipeline), streamQueue, videopay, rtpbin, rtpsink, rtcpsink, rtcpsrc,identity, NULL);

  if(!gst_element_link_many (tee, streamQueue, NULL))
  {
    g_error ("Failed to link tee, streamQueue");
  }

  if (!gst_element_link_many (streamQueue, videopay, NULL))
  {
    g_error ("Failed to link video streamQueue and video payloader");

  }
  
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

   /*Set the pipeline to "playing" state*/
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
   /*Iterate*/
  g_main_loop_run (loop);
  /* Out of the main loop, clean up nicely*/
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));
  return 0;
}	
