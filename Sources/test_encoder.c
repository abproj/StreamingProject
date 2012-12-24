#include <string.h>
#include <math.h>

#include <gst/gst.h>

/* A simple RTP server*/

/* change this to send the RTP data and RTCP to another host */
//#define DEST_HOST "10.0.0.5"
//#define DEST_HOST "127.0.0.1"
#define VOFFSET   "0"
#define AOFFSET    "0"


/*H264 encode from the source*/

#define VIDEO_SRC  "v4l2src"
#define VIDEO_ENC  "x264enc"

static int bitrate = 256;

/* this function is called every five second and change enc properties*/

static gboolean change_enc_prop(GstElement *enc)
{
   GValue v = {0, };
   GValue * bitrateval = &v;
   GValue r = {0, };
   GValue *refframe = &r;
   int int_v, int_ref ;

   g_value_init(&v, G_TYPE_INT);
   g_object_get_property(G_OBJECT(enc), "bitrate" , &v);

   g_value_init(&r, G_TYPE_INT);
   g_object_get_property(G_OBJECT(enc), "ref", &r);

   int_v = g_value_get_int(bitrateval);
   int_ref = g_value_get_int(refframe);
   g_print("Current bitrate value:%d\n",int_v);
   g_print("Ref frame count: %d\n", int_ref);
   if(bitrate < 1024)
      bitrate= bitrate * 2  ;

   g_print("In change_enc_prop new bitrate set\n");
   g_object_set (enc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",bitrate,NULL);

  return TRUE; 			
} 

int main (int argc, char *argv[])
{
  GstElement *videosrc,*videorate,*videoenc,*filesink;
  GstElement *pipeline,*videoqueue;
  GMainLoop *loop;
  GstCaps *caps;
  gboolean link_ok;
  
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
  
  /*Encodes video to H264 stream */
  videoenc = gst_element_factory_make (VIDEO_ENC, "videoenc");
  g_assert (videoenc);
   
  g_object_set (videoenc, "tune",0x00000004,"byte-stream",TRUE,"bitrate",bitrate  ,NULL); 

  filesink = gst_element_factory_make("filesink", "filesink");
  g_assert(filesink);

  g_object_set (filesink, "location", "encstream.mp4", NULL);
 
  /*Add video capture to the pipeline and link */ 
  gst_bin_add_many (GST_BIN (pipeline),videosrc,videoqueue,videorate,videoenc, NULL);


  if (!gst_element_link_many (videosrc, videoqueue,NULL))
          
  {
    g_error ("Failed to link videosrc, videoqueue");
  }

  if (!gst_element_link_many ( videoqueue,videorate,NULL))

  {
    g_error ("Failed to link videoqueue, videorate");
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv","width",  G_TYPE_INT, 320,
                                "height", G_TYPE_INT,240 ,
              "framerate", GST_TYPE_FRACTION, 15, 1,
              NULL);

  link_ok = gst_element_link_filtered (videorate, videoenc, caps);
  gst_caps_unref (caps);

  if (!link_ok)
  {
    g_warning ("Failed to link videorate and videoenc");
  }

  gst_bin_add_many (GST_BIN (pipeline), videoenc,filesink, NULL);
  if(!gst_element_link_many (videoenc, filesink, NULL))
  {
    g_error ("Failed to link videoenc, filesink");
  }

  /* Set the pipeline to playing */
  g_print ("starting sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /*change encoder properties evry 5 sec*/
  g_timeout_add (5000, (GSourceFunc) change_enc_prop, videoenc);
	
  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_print ("stopping sender pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
