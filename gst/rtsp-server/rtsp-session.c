/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <string.h>

#include "rtsp-session.h"

#undef DEBUG

static void gst_rtsp_session_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPSession, gst_rtsp_session, G_TYPE_OBJECT);

static void
gst_rtsp_session_class_init (GstRTSPSessionClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_session_finalize;
}

static void
gst_rtsp_session_init (GstRTSPSession * session)
{
}

static void
gst_rtsp_session_free_stream (GstRTSPSessionStream *stream, GstRTSPSessionMedia *media)
{
  if (stream->client_trans)
    gst_rtsp_transport_free (stream->client_trans);

  g_free (stream);
}

static void
gst_rtsp_session_free_media (GstRTSPSessionMedia *media, GstRTSPSession *session)
{
  g_list_foreach (media->streams, (GFunc) gst_rtsp_session_free_stream,
		  media);
  g_list_free (media->streams);

  if (media->url)
    gst_rtsp_url_free (media->url);

  if (media->media)
    g_object_unref (media->media);

  g_free (media);
}

static void
gst_rtsp_session_finalize (GObject * obj)
{
  GstRTSPSession *session;

  session = GST_RTSP_SESSION (obj);

  /* free all media */
  g_list_foreach (session->medias, (GFunc) gst_rtsp_session_free_media,
		  session);
  g_list_free (session->medias);

  /* free session id */
  g_free (session->sessionid);

  G_OBJECT_CLASS (gst_rtsp_session_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_session_manage_media:
 * @sess: a #GstRTSPSession
 * @url: the url for the media
 * @obj: a #GstRTSPMediaObject
 *
 * Manage the media object @obj in @sess. @url will be used to retrieve this
 * media object from the session with gst_rtsp_session_get_media().
 *
 * Returns: a new @GstRTSPSessionMedia object.
 */
GstRTSPSessionMedia *
gst_rtsp_session_manage_media (GstRTSPSession *sess, const GstRTSPUrl *uri,
    GstRTSPMedia *media)
{
  GstRTSPSessionMedia *result;

  result = g_new0 (GstRTSPSessionMedia, 1);
  result->media = media;
  result->url = gst_rtsp_url_copy ((GstRTSPUrl *)uri);

  sess->medias = g_list_prepend (sess->medias, result);

  g_message ("manage new media %p in session %p", media, sess);

  return result;
}

/**
 * gst_rtsp_session_get_media:
 * @sess: a #GstRTSPSession
 * @url: the url for the media
 *
 * Get the session media of the @url.
 *
 * Returns: the configuration for @url in @sess.
 */
GstRTSPSessionMedia *
gst_rtsp_session_get_media (GstRTSPSession *sess, const GstRTSPUrl *url)
{
  GstRTSPSessionMedia *result;
  GList *walk;

  g_return_val_if_fail (GST_IS_RTSP_SESSION (sess), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  result = NULL;

  for (walk = sess->medias; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionMedia *) walk->data; 

    if (strcmp (result->url->abspath, url->abspath) == 0)
      break;

    result = NULL;
  }
  return result;
}

/**
 * gst_rtsp_session_media_get_stream:
 * @media: a #GstRTSPSessionMedia
 * @idx: the stream index
 *
 * Get a previously created or create a new #GstRTSPSessionStream at @idx.
 *
 * Returns: a #GstRTSPSessionStream that is valid until the session of @media
 * is unreffed.
 */
GstRTSPSessionStream *
gst_rtsp_session_media_get_stream (GstRTSPSessionMedia *media, guint idx)
{
  GstRTSPSessionStream *result;
  GstRTSPMediaStream *media_stream;
  GList *walk;

  g_return_val_if_fail (media != NULL, NULL);
  g_return_val_if_fail (media->media != NULL, NULL);

  media_stream = gst_rtsp_media_get_stream (media->media, idx);
  if (media_stream == NULL)
    goto no_media;

  result = NULL;
  for (walk = media->streams; walk; walk = g_list_next (walk)) {
    result = (GstRTSPSessionStream *) walk->data; 

    if (result->media_stream == media_stream)
      break;

    result = NULL;
  }
  if (result == NULL) {
    result = g_new0 (GstRTSPSessionStream, 1);
    result->media_stream = media_stream;

    media->streams = g_list_prepend (media->streams, result);
  }
  return result;

  /* ERRORS */
no_media:
  {
    return NULL;
  }
}

/**
 * gst_rtsp_session_new:
 *
 * Create a new #GstRTSPSession instance.
 */
GstRTSPSession *
gst_rtsp_session_new (const gchar *sessionid)
{
  GstRTSPSession *result;

  result = g_object_new (GST_TYPE_RTSP_SESSION, NULL);
  result->sessionid = g_strdup (sessionid);

  return result;
}

/**
 * gst_rtsp_session_stream_init_udp:
 * @stream: a #GstRTSPSessionStream
 * @ct: a client #GstRTSPTransport
 *
 * Set @ct as the client transport and create and return a matching server
 * transport.
 * 
 * Returns: a server transport or NULL if something went wrong.
 */
GstRTSPTransport *
gst_rtsp_session_stream_set_transport (GstRTSPSessionStream *stream, 
    GstRTSPTransport *ct)
{
  GstRTSPTransport *st;

  /* prepare the server transport */
  gst_rtsp_transport_new (&st);

  st->trans = ct->trans;
  st->profile = ct->profile;
  st->lower_transport = ct->lower_transport;
  st->client_port = ct->client_port;

  /* keep track of the transports */
  if (stream->client_trans)
    gst_rtsp_transport_free (stream->client_trans);
  stream->client_trans = ct;

  st->server_port.min = stream->media_stream->server_port.min;
  st->server_port.max = stream->media_stream->server_port.max;

  return st;
}


/**
 * gst_rtsp_session_media_play:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to start playing and streaming to the client.
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_play (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;
  GstRTSPSessionStream *stream;
  GList *walk;

  for (walk = media->streams; walk; walk = g_list_next (walk)) {
    stream = (GstRTSPSessionStream *) walk->data; 

    gst_rtsp_media_stream_add (stream->media_stream, stream->client_trans);
  }
  ret = gst_rtsp_media_play (media->media);

  return ret;
}

/**
 * gst_rtsp_session_media_pause:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to pause.
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_pause (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;

  ret = gst_rtsp_media_pause (media->media);

  return ret;
}

/**
 * gst_rtsp_session_media_stop:
 * @media: a #GstRTSPSessionMedia
 *
 * Tell the media object @media to stop playing. After this call the media
 * cannot be played or paused anymore
 *
 * Returns: a #GstStateChangeReturn
 */
GstStateChangeReturn
gst_rtsp_session_media_stop (GstRTSPSessionMedia *media)
{
  GstStateChangeReturn ret;

  ret = gst_rtsp_media_stop (media->media);

  return ret;
}

