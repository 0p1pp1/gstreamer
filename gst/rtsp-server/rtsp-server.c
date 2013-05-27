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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <string.h>

#include "rtsp-server.h"
#include "rtsp-client.h"

#define GST_RTSP_SERVER_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_SERVER, GstRTSPServerPrivate))

#define GST_RTSP_SERVER_GET_LOCK(server)  (&(GST_RTSP_SERVER_CAST(server)->priv->lock))
#define GST_RTSP_SERVER_LOCK(server)      (g_mutex_lock(GST_RTSP_SERVER_GET_LOCK(server)))
#define GST_RTSP_SERVER_UNLOCK(server)    (g_mutex_unlock(GST_RTSP_SERVER_GET_LOCK(server)))

struct _GstRTSPServerPrivate
{
  GMutex lock;                  /* protects everything in this struct */

  /* server information */
  gchar *address;
  gchar *service;
  gint backlog;
  gint max_threads;

  GSocket *socket;

  /* sessions on this server */
  GstRTSPSessionPool *session_pool;

  /* mount points for this server */
  GstRTSPMountPoints *mount_points;

  /* authentication manager */
  GstRTSPAuth *auth;

  /* the clients that are connected */
  GList *clients;
  GQueue loops;                 /* the main loops used in the threads */
};

#define DEFAULT_ADDRESS         "0.0.0.0"
#define DEFAULT_BOUND_PORT      -1
/* #define DEFAULT_ADDRESS         "::0" */
#define DEFAULT_SERVICE         "8554"
#define DEFAULT_BACKLOG         5
#define DEFAULT_MAX_THREADS     0

/* Define to use the SO_LINGER option so that the server sockets can be resused
 * sooner. Disabled for now because it is not very well implemented by various
 * OSes and it causes clients to fail to read the TEARDOWN response. */
#undef USE_SOLINGER

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_SERVICE,
  PROP_BOUND_PORT,
  PROP_BACKLOG,

  PROP_SESSION_POOL,
  PROP_MOUNT_POINTS,
  PROP_MAX_THREADS,
  PROP_LAST
};

enum
{
  SIGNAL_CLIENT_CONNECTED,
  SIGNAL_LAST
};

G_DEFINE_TYPE (GstRTSPServer, gst_rtsp_server, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (rtsp_server_debug);
#define GST_CAT_DEFAULT rtsp_server_debug

typedef struct _ClientContext ClientContext;
typedef struct _Loop Loop;

static guint gst_rtsp_server_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_server_finalize (GObject * object);

static gpointer do_loop (Loop * loop);
static GstRTSPClient *default_create_client (GstRTSPServer * server);
static gboolean default_accept_client (GstRTSPServer * server,
    GstRTSPClient * client, GSocket * socket, GError ** error);

static void
gst_rtsp_server_class_init (GstRTSPServerClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPServerPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_server_get_property;
  gobject_class->set_property = gst_rtsp_server_set_property;
  gobject_class->finalize = gst_rtsp_server_finalize;

  /**
   * GstRTSPServer::address:
   *
   * The address of the server. This is the address where the server will
   * listen on.
   */
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "The address the server uses to listen on", DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::service:
   *
   * The service of the server. This is either a string with the service name or
   * a port number (as a string) the server will listen on.
   */
  g_object_class_install_property (gobject_class, PROP_SERVICE,
      g_param_spec_string ("service", "Service",
          "The service or port number the server uses to listen on",
          DEFAULT_SERVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::bound-port:
   *
   * The actual port the server is listening on. Can be used to retrieve the
   * port number when the server is started on port 0, which means bind to a
   * random port. Set to -1 if the server has not been bound yet.
   */
  g_object_class_install_property (gobject_class, PROP_BOUND_PORT,
      g_param_spec_int ("bound-port", "Bound port",
          "The port number the server is listening on",
          -1, G_MAXUINT16, DEFAULT_BOUND_PORT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::backlog:
   *
   * The backlog argument defines the maximum length to which the queue of
   * pending connections for the server may grow. If a connection request arrives
   * when the queue is full, the client may receive an error with an indication of
   * ECONNREFUSED or, if the underlying protocol supports retransmission, the
   * request may be ignored so that a later reattempt at  connection succeeds.
   */
  g_object_class_install_property (gobject_class, PROP_BACKLOG,
      g_param_spec_int ("backlog", "Backlog",
          "The maximum length to which the queue "
          "of pending connections may grow", 0, G_MAXINT, DEFAULT_BACKLOG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::session-pool:
   *
   * The session pool of the server. By default each server has a separate
   * session pool but sessions can be shared between servers by setting the same
   * session pool on multiple servers.
   */
  g_object_class_install_property (gobject_class, PROP_SESSION_POOL,
      g_param_spec_object ("session-pool", "Session Pool",
          "The session pool to use for client session",
          GST_TYPE_RTSP_SESSION_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::mount-points:
   *
   * The mount points to use for this server. By default the server has no
   * mount points and thus cannot map urls to media streams.
   */
  g_object_class_install_property (gobject_class, PROP_MOUNT_POINTS,
      g_param_spec_object ("mount-points", "Mount Points",
          "The mount points to use for client session",
          GST_TYPE_RTSP_MOUNT_POINTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPServer::max-threads:
   *
   * The maximum amount of threads to use for client connections. A value of
   * 0 means to use only the mainloop, -1 means an unlimited amount of
   * threads.
   */
  g_object_class_install_property (gobject_class, PROP_MAX_THREADS,
      g_param_spec_int ("max-threads", "Max Threads",
          "The maximum amount of threads to use for client connections "
          "(0 = only mainloop, -1 = unlimited)", -1, G_MAXINT,
          DEFAULT_MAX_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED] =
      g_signal_new ("client-connected", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPServerClass, client_connected),
      NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      gst_rtsp_client_get_type ());

  klass->create_client = default_create_client;
  klass->accept_client = default_accept_client;

  klass->pool = g_thread_pool_new ((GFunc) do_loop, klass, -1, FALSE, NULL);

  GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "rtspserver", 0, "GstRTSPServer");
}

static void
gst_rtsp_server_init (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = GST_RTSP_SERVER_GET_PRIVATE (server);

  server->priv = priv;

  g_mutex_init (&priv->lock);
  priv->address = g_strdup (DEFAULT_ADDRESS);
  priv->service = g_strdup (DEFAULT_SERVICE);
  priv->socket = NULL;
  priv->backlog = DEFAULT_BACKLOG;
  priv->session_pool = gst_rtsp_session_pool_new ();
  priv->mount_points = gst_rtsp_mount_points_new ();
  priv->max_threads = DEFAULT_MAX_THREADS;
  g_queue_init (&priv->loops);
}

static void
gst_rtsp_server_finalize (GObject * object)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "finalize server");

  g_free (priv->address);
  g_free (priv->service);

  if (priv->socket)
    g_object_unref (priv->socket);

  g_object_unref (priv->session_pool);
  g_object_unref (priv->mount_points);

  if (priv->auth)
    g_object_unref (priv->auth);

  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_server_parent_class)->finalize (object);
}

/**
 * gst_rtsp_server_new:
 *
 * Create a new #GstRTSPServer instance.
 */
GstRTSPServer *
gst_rtsp_server_new (void)
{
  GstRTSPServer *result;

  result = g_object_new (GST_TYPE_RTSP_SERVER, NULL);

  return result;
}

/**
 * gst_rtsp_server_set_address:
 * @server: a #GstRTSPServer
 * @address: the address
 *
 * Configure @server to accept connections on the given address.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_address (GstRTSPServer * server, const gchar * address)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (address != NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  g_free (priv->address);
  priv->address = g_strdup (address);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_address:
 * @server: a #GstRTSPServer
 *
 * Get the address on which the server will accept connections.
 *
 * Returns: the server address. g_free() after usage.
 */
gchar *
gst_rtsp_server_get_address (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (priv->address);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_get_bound_port:
 * @server: a #GstRTSPServer
 *
 * Get the port number where the server was bound to.
 *
 * Returns: the port number
 */
int
gst_rtsp_server_get_bound_port (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GSocketAddress *address;
  int result = -1;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), result);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if (priv->socket == NULL)
    goto out;

  address = g_socket_get_local_address (priv->socket, NULL);
  result = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (address));
  g_object_unref (address);

out:
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_service:
 * @server: a #GstRTSPServer
 * @service: the service
 *
 * Configure @server to accept connections on the given service.
 * @service should be a string containing the service name (see services(5)) or
 * a string containing a port number between 1 and 65535.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_service (GstRTSPServer * server, const gchar * service)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));
  g_return_if_fail (service != NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  g_free (priv->service);
  priv->service = g_strdup (service);
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_service:
 * @server: a #GstRTSPServer
 *
 * Get the service on which the server will accept connections.
 *
 * Returns: the service. use g_free() after usage.
 */
gchar *
gst_rtsp_server_get_service (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = g_strdup (priv->service);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_backlog:
 * @server: a #GstRTSPServer
 * @backlog: the backlog
 *
 * configure the maximum amount of requests that may be queued for the
 * server.
 *
 * This function must be called before the server is bound.
 */
void
gst_rtsp_server_set_backlog (GstRTSPServer * server, gint backlog)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  priv->backlog = backlog;
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_backlog:
 * @server: a #GstRTSPServer
 *
 * The maximum amount of queued requests for the server.
 *
 * Returns: the server backlog.
 */
gint
gst_rtsp_server_get_backlog (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gint result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  result = priv->backlog;
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_session_pool:
 * @server: a #GstRTSPServer
 * @pool: a #GstRTSPSessionPool
 *
 * configure @pool to be used as the session pool of @server.
 */
void
gst_rtsp_server_set_session_pool (GstRTSPServer * server,
    GstRTSPSessionPool * pool)
{
  GstRTSPServerPrivate *priv;
  GstRTSPSessionPool *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (pool)
    g_object_ref (pool);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->session_pool;
  priv->session_pool = pool;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_server_get_session_pool:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPSessionPool used as the session pool of @server.
 *
 * Returns: (transfer full): the #GstRTSPSessionPool used for sessions. g_object_unref() after
 * usage.
 */
GstRTSPSessionPool *
gst_rtsp_server_get_session_pool (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPSessionPool *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->session_pool))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_mount_points:
 * @server: a #GstRTSPServer
 * @mounts: a #GstRTSPMountPoints
 *
 * configure @mounts to be used as the mount points of @server.
 */
void
gst_rtsp_server_set_mount_points (GstRTSPServer * server,
    GstRTSPMountPoints * mounts)
{
  GstRTSPServerPrivate *priv;
  GstRTSPMountPoints *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (mounts)
    g_object_ref (mounts);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->mount_points;
  priv->mount_points = mounts;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_server_get_mount_points:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPMountPoints used as the mount points of @server.
 *
 * Returns: (transfer full): the #GstRTSPMountPoints of @server. g_object_unref() after
 * usage.
 */
GstRTSPMountPoints *
gst_rtsp_server_get_mount_points (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPMountPoints *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->mount_points))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_auth:
 * @server: a #GstRTSPServer
 * @auth: a #GstRTSPAuth
 *
 * configure @auth to be used as the authentication manager of @server.
 */
void
gst_rtsp_server_set_auth (GstRTSPServer * server, GstRTSPAuth * auth)
{
  GstRTSPServerPrivate *priv;
  GstRTSPAuth *old;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  if (auth)
    g_object_ref (auth);

  GST_RTSP_SERVER_LOCK (server);
  old = priv->auth;
  priv->auth = auth;
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_rtsp_server_get_auth:
 * @server: a #GstRTSPServer
 *
 * Get the #GstRTSPAuth used as the authentication manager of @server.
 *
 * Returns: (transfer full): the #GstRTSPAuth of @server. g_object_unref() after
 * usage.
 */
GstRTSPAuth *
gst_rtsp_server_get_auth (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  GstRTSPAuth *result;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  if ((result = priv->auth))
    g_object_ref (result);
  GST_RTSP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_rtsp_server_set_max_threads:
 * @server: a #GstRTSPServer
 * @max_threads: maximum threads
 *
 * Set the maximum threads used by the server to handle client requests.
 * A value of 0 will use the server mainloop, a value of -1 will use an
 * unlimited number of threads.
 */
void
gst_rtsp_server_set_max_threads (GstRTSPServer * server, gint max_threads)
{
  GstRTSPServerPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_SERVER (server));

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  priv->max_threads = max_threads;
  GST_RTSP_SERVER_UNLOCK (server);
}

/**
 * gst_rtsp_server_get_max_threads:
 * @server: a #GstRTSPServer
 *
 * Get the maximum number of threads used for client connections.
 * See gst_rtsp_server_set_max_threads().
 *
 * Returns: the maximum number of threads.
 */
gint
gst_rtsp_server_get_max_threads (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv;
  gint res;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), -1);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  res = priv->max_threads;
  GST_RTSP_SERVER_UNLOCK (server);

  return res;
}


static void
gst_rtsp_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      g_value_take_string (value, gst_rtsp_server_get_address (server));
      break;
    case PROP_SERVICE:
      g_value_take_string (value, gst_rtsp_server_get_service (server));
      break;
    case PROP_BOUND_PORT:
      g_value_set_int (value, gst_rtsp_server_get_bound_port (server));
      break;
    case PROP_BACKLOG:
      g_value_set_int (value, gst_rtsp_server_get_backlog (server));
      break;
    case PROP_SESSION_POOL:
      g_value_take_object (value, gst_rtsp_server_get_session_pool (server));
      break;
    case PROP_MOUNT_POINTS:
      g_value_take_object (value, gst_rtsp_server_get_mount_points (server));
      break;
    case PROP_MAX_THREADS:
      g_value_set_int (value, gst_rtsp_server_get_max_threads (server));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPServer *server = GST_RTSP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      gst_rtsp_server_set_address (server, g_value_get_string (value));
      break;
    case PROP_SERVICE:
      gst_rtsp_server_set_service (server, g_value_get_string (value));
      break;
    case PROP_BACKLOG:
      gst_rtsp_server_set_backlog (server, g_value_get_int (value));
      break;
    case PROP_SESSION_POOL:
      gst_rtsp_server_set_session_pool (server, g_value_get_object (value));
      break;
    case PROP_MOUNT_POINTS:
      gst_rtsp_server_set_mount_points (server, g_value_get_object (value));
      break;
    case PROP_MAX_THREADS:
      gst_rtsp_server_set_max_threads (server, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_server_create_socket:
 * @server: a #GstRTSPServer
 * @cancellable: a #GCancellable
 * @error: a #GError
 *
 * Create a #GSocket for @server. The socket will listen on the
 * configured service.
 *
 * Returns: (transfer full): the #GSocket for @server or NULL when an error occured.
 */
GSocket *
gst_rtsp_server_create_socket (GstRTSPServer * server,
    GCancellable * cancellable, GError ** error)
{
  GstRTSPServerPrivate *priv;
  GSocketConnectable *conn;
  GSocketAddressEnumerator *enumerator;
  GSocket *socket = NULL;
#ifdef USE_SOLINGER
  struct linger linger;
#endif
  GError *sock_error = NULL;
  GError *bind_error = NULL;
  guint16 port;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  GST_RTSP_SERVER_LOCK (server);
  GST_DEBUG_OBJECT (server, "getting address info of %s/%s", priv->address,
      priv->service);

  /* resolve the server IP address */
  port = atoi (priv->service);
  if (port != 0 || !strcmp (priv->service, "0"))
    conn = g_network_address_new (priv->address, port);
  else
    conn = g_network_service_new (priv->service, "tcp", priv->address);

  enumerator = g_socket_connectable_enumerate (conn);
  g_object_unref (conn);

  /* create server socket, we loop through all the addresses until we manage to
   * create a socket and bind. */
  while (TRUE) {
    GSocketAddress *sockaddr;

    sockaddr =
        g_socket_address_enumerator_next (enumerator, cancellable, error);
    if (!sockaddr) {
      if (!*error)
        GST_DEBUG_OBJECT (server, "no more addresses %s",
            *error ? (*error)->message : "");
      else
        GST_DEBUG_OBJECT (server, "failed to retrieve next address %s",
            (*error)->message);
      break;
    }

    /* only keep the first error */
    socket = g_socket_new (g_socket_address_get_family (sockaddr),
        G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
        sock_error ? NULL : &sock_error);

    if (socket == NULL) {
      GST_DEBUG_OBJECT (server, "failed to make socket (%s), try next",
          sock_error->message);
      g_object_unref (sockaddr);
      continue;
    }

    if (g_socket_bind (socket, sockaddr, TRUE, bind_error ? NULL : &bind_error)) {
      g_object_unref (sockaddr);
      break;
    }

    GST_DEBUG_OBJECT (server, "failed to bind socket (%s), try next",
        bind_error->message);
    g_object_unref (sockaddr);
    g_object_unref (socket);
    socket = NULL;
  }
  g_object_unref (enumerator);

  if (socket == NULL)
    goto no_socket;

  g_clear_error (&sock_error);
  g_clear_error (&bind_error);

  GST_DEBUG_OBJECT (server, "opened sending server socket");

  /* keep connection alive; avoids SIGPIPE during write */
  g_socket_set_keepalive (socket, TRUE);

#if 0
#ifdef USE_SOLINGER
  /* make sure socket is reset 5 seconds after close. This ensure that we can
   * reuse the socket quickly while still having a chance to send data to the
   * client. */
  linger.l_onoff = 1;
  linger.l_linger = 5;
  if (setsockopt (sockfd, SOL_SOCKET, SO_LINGER,
          (void *) &linger, sizeof (linger)) < 0)
    goto linger_failed;
#endif
#endif

  /* set the server socket to nonblocking */
  g_socket_set_blocking (socket, FALSE);

  /* set listen backlog */
  g_socket_set_listen_backlog (socket, priv->backlog);

  if (!g_socket_listen (socket, error))
    goto listen_failed;

  GST_DEBUG_OBJECT (server, "listening on server socket %p with queue of %d",
      socket, priv->backlog);

  GST_RTSP_SERVER_UNLOCK (server);

  return socket;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket");
    goto close_error;
  }
#if 0
#ifdef USE_SOLINGER
linger_failed:
  {
    GST_ERROR_OBJECT (server, "failed to no linger socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#endif
#endif
listen_failed:
  {
    GST_ERROR_OBJECT (server, "failed to listen on socket: %s",
        (*error)->message);
    goto close_error;
  }
close_error:
  {
    if (socket)
      g_object_unref (socket);

    if (sock_error) {
      if (error == NULL)
        g_propagate_error (error, sock_error);
      else
        g_error_free (sock_error);
    }
    if (bind_error) {
      if ((error == NULL) || (*error == NULL))
        g_propagate_error (error, bind_error);
      else
        g_error_free (bind_error);
    }
    GST_RTSP_SERVER_UNLOCK (server);
    return NULL;
  }
}

struct _Loop
{
  gint refcnt;

  GstRTSPServer *server;
  GMainLoop *mainloop;
  GMainContext *mainctx;
};

/* must be called with the lock held */
static void
loop_unref (Loop * loop)
{
  GstRTSPServer *server = loop->server;
  GstRTSPServerPrivate *priv = server->priv;

  loop->refcnt--;

  if (loop->refcnt <= 0) {
    g_queue_remove (&priv->loops, loop);
    g_main_loop_quit (loop->mainloop);
  }
}

struct _ClientContext
{
  GstRTSPServer *server;
  Loop *loop;
  GstRTSPClient *client;
};

static gboolean
free_client_context (ClientContext * ctx)
{
  GST_RTSP_SERVER_LOCK (ctx->server);
  if (ctx->loop)
    loop_unref (ctx->loop);
  GST_RTSP_SERVER_UNLOCK (ctx->server);

  g_object_unref (ctx->client);
  g_slice_free (ClientContext, ctx);

  return G_SOURCE_REMOVE;
}

static gpointer
do_loop (Loop * loop)
{
  GST_INFO ("enter mainloop");
  g_main_loop_run (loop->mainloop);
  GST_INFO ("exit mainloop");

  g_main_context_unref (loop->mainctx);
  g_main_loop_unref (loop->mainloop);
  g_object_unref (loop->server);
  g_slice_free (Loop, loop);

  return NULL;
}

/* Must be called with lock held */

static Loop *
gst_rtsp_server_get_main_loop (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = server->priv;
  Loop *loop;

  if (priv->max_threads > 0 &&
      g_queue_get_length (&priv->loops) >= priv->max_threads) {
    loop = g_queue_pop_head (&priv->loops);
    loop->refcnt++;
  } else {
    GstRTSPServerClass *klass = GST_RTSP_SERVER_GET_CLASS (server);

    loop = g_slice_new0 (Loop);
    loop->refcnt = 1;
    loop->server = g_object_ref (server);
    loop->mainctx = g_main_context_new ();
    loop->mainloop = g_main_loop_new (loop->mainctx, FALSE);

    g_thread_pool_push (klass->pool, loop, NULL);
  }

  g_queue_push_tail (&priv->loops, loop);

  return loop;
}

static void
unmanage_client (GstRTSPClient * client, ClientContext * ctx)
{
  GstRTSPServer *server = ctx->server;
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "unmanage client %p", client);

  g_object_ref (server);

  GST_RTSP_SERVER_LOCK (server);
  priv->clients = g_list_remove (priv->clients, ctx);
  GST_RTSP_SERVER_UNLOCK (server);

  if (ctx->loop) {
    GSource *src;

    src = g_idle_source_new ();
    g_source_set_callback (src, (GSourceFunc) free_client_context, ctx, NULL);
    g_source_attach (src, ctx->loop->mainctx);
    g_source_unref (src);
  } else {
    free_client_context (ctx);
  }

  g_object_unref (server);
}

/* add the client context to the active list of clients, takes ownership
 * of client */
static void
manage_client (GstRTSPServer * server, GstRTSPClient * client)
{
  ClientContext *ctx;
  GstRTSPServerPrivate *priv = server->priv;
  GMainContext *mainctx;

  GST_DEBUG_OBJECT (server, "manage client %p", client);

  ctx = g_slice_new0 (ClientContext);
  ctx->server = server;
  ctx->client = client;

  GST_RTSP_SERVER_LOCK (server);
  if (priv->max_threads == 0) {
    GSource *source;

    /* find the context to add the watch */
    if ((source = g_main_current_source ()))
      mainctx = g_source_get_context (source);
    else
      mainctx = NULL;
  } else {
    ctx->loop = gst_rtsp_server_get_main_loop (server);
    mainctx = ctx->loop->mainctx;
  }

  g_signal_connect (client, "closed", (GCallback) unmanage_client, ctx);
  priv->clients = g_list_prepend (priv->clients, ctx);

  gst_rtsp_client_attach (client, mainctx);

  GST_RTSP_SERVER_UNLOCK (server);
}

static GstRTSPClient *
default_create_client (GstRTSPServer * server)
{
  GstRTSPClient *client;
  GstRTSPServerPrivate *priv = server->priv;

  /* a new client connected, create a session to handle the client. */
  client = gst_rtsp_client_new ();

  /* set the session pool that this client should use */
  GST_RTSP_SERVER_LOCK (server);
  gst_rtsp_client_set_session_pool (client, priv->session_pool);
  /* set the mount points that this client should use */
  gst_rtsp_client_set_mount_points (client, priv->mount_points);
  /* set authentication manager */
  gst_rtsp_client_set_auth (client, priv->auth);
  GST_RTSP_SERVER_UNLOCK (server);

  return client;
}

/* default method for creating a new client object in the server to accept and
 * handle a client connection on this server */
static gboolean
default_accept_client (GstRTSPServer * server, GstRTSPClient * client,
    GSocket * socket, GError ** error)
{
  /* accept connections for that client, this function returns after accepting
   * the connection and will run the remainder of the communication with the
   * client asyncronously. */
  if (!gst_rtsp_client_accept (client, socket, NULL, error))
    goto accept_failed;

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    GST_ERROR_OBJECT (server,
        "Could not accept client on server : %s", (*error)->message);
    return FALSE;
  }
}

/**
 * gst_rtsp_server_transfer_connection:
 * @server: a #GstRTSPServer
 * @socket: a network socket
 * @ip: the IP address of the remote client
 * @port: the port used by the other end
 * @initial_buffer: any initial data that was already read from the socket
 *
 * Take an existing network socket and use it for an RTSP connection. This
 * is used when transferring a socket from an HTTP server which should be used
 * as an RTSP over HTTP tunnel. The @initial_buffer contains any remaining data
 * that the HTTP server read from the socket while parsing the HTTP header.
 *
 * Returns: TRUE if all was ok, FALSE if an error occured.
 */
gboolean
gst_rtsp_server_transfer_connection (GstRTSPServer * server, GSocket * socket,
    const gchar * ip, gint port, const gchar * initial_buffer)
{
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;
  GError *error = NULL;

  klass = GST_RTSP_SERVER_GET_CLASS (server);

  if (klass->create_client)
    client = klass->create_client (server);
  if (client == NULL)
    goto client_failed;

  /* a new client connected, create a client object to handle the client. */
  if (!gst_rtsp_client_use_socket (client, socket, ip,
          port, initial_buffer, &error)) {
    goto transfer_failed;
  }

  /* manage the client connection */
  manage_client (server, client);

  g_signal_emit (server, gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED], 0,
      client);

  return TRUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    return FALSE;
  }
transfer_failed:
  {
    GST_ERROR_OBJECT (server, "failed to accept client: %s", error->message);
    g_error_free (error);
    g_object_unref (client);
    return FALSE;
  }
}

/**
 * gst_rtsp_server_io_func:
 * @socket: a #GSocket
 * @condition: the condition on @source
 * @server: a #GstRTSPServer
 *
 * A default #GSocketSourceFunc that creates a new #GstRTSPClient to accept and handle a
 * new connection on @socket or @server.
 *
 * Returns: TRUE if the source could be connected, FALSE if an error occured.
 */
gboolean
gst_rtsp_server_io_func (GSocket * socket, GIOCondition condition,
    GstRTSPServer * server)
{
  gboolean result = TRUE;
  GstRTSPClient *client = NULL;
  GstRTSPServerClass *klass;
  GError *error = NULL;

  if (condition & G_IO_IN) {
    klass = GST_RTSP_SERVER_GET_CLASS (server);

    if (klass->create_client)
      client = klass->create_client (server);
    if (client == NULL)
      goto client_failed;

    /* a new client connected, create a client object to handle the client. */
    if (klass->accept_client)
      result = klass->accept_client (server, client, socket, &error);
    if (!result)
      goto accept_failed;

    /* manage the client connection */
    manage_client (server, client);

    g_signal_emit (server, gst_rtsp_server_signals[SIGNAL_CLIENT_CONNECTED], 0,
        client);
  } else {
    GST_WARNING_OBJECT (server, "received unknown event %08x", condition);
  }
  return G_SOURCE_CONTINUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    return G_SOURCE_CONTINUE;
  }
accept_failed:
  {
    GST_ERROR_OBJECT (server, "failed to accept client: %s", error->message);
    g_error_free (error);
    g_object_unref (client);
    return G_SOURCE_CONTINUE;
  }
}

static void
watch_destroyed (GstRTSPServer * server)
{
  GstRTSPServerPrivate *priv = server->priv;

  GST_DEBUG_OBJECT (server, "source destroyed");

  g_object_unref (priv->socket);
  priv->socket = NULL;
  g_object_unref (server);
}

/**
 * gst_rtsp_server_create_source:
 * @server: a #GstRTSPServer
 * @cancellable: a #GCancellable or %NULL.
 * @error: a #GError
 *
 * Create a #GSource for @server. The new source will have a default
 * #GSocketSourceFunc of gst_rtsp_server_io_func().
 *
 * @cancellable if not NULL can be used to cancel the source, which will cause
 * the source to trigger, reporting the current condition (which is likely 0
 * unless cancellation happened at the same time as a condition change). You can
 * check for this in the callback using g_cancellable_is_cancelled().
 *
 * Returns: the #GSource for @server or NULL when an error occured. Free with
 * g_source_unref ()
 */
GSource *
gst_rtsp_server_create_source (GstRTSPServer * server,
    GCancellable * cancellable, GError ** error)
{
  GstRTSPServerPrivate *priv;
  GSocket *socket, *old;
  GSource *source;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), NULL);

  priv = server->priv;

  socket = gst_rtsp_server_create_socket (server, NULL, error);
  if (socket == NULL)
    goto no_socket;

  GST_RTSP_SERVER_LOCK (server);
  old = priv->socket;
  priv->socket = g_object_ref (socket);
  GST_RTSP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);

  /* create a watch for reads (new connections) and possible errors */
  source = g_socket_create_source (socket, G_IO_IN |
      G_IO_ERR | G_IO_HUP | G_IO_NVAL, cancellable);
  g_object_unref (socket);

  /* configure the callback */
  g_source_set_callback (source,
      (GSourceFunc) gst_rtsp_server_io_func, g_object_ref (server),
      (GDestroyNotify) watch_destroyed);

  return source;

no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket");
    return NULL;
  }
}

/**
 * gst_rtsp_server_attach:
 * @server: a #GstRTSPServer
 * @context: (allow-none): a #GMainContext
 *
 * Attaches @server to @context. When the mainloop for @context is run, the
 * server will be dispatched. When @context is NULL, the default context will be
 * used).
 *
 * This function should be called when the server properties and urls are fully
 * configured and the server is ready to start.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext.
 */
guint
gst_rtsp_server_attach (GstRTSPServer * server, GMainContext * context)
{
  guint res;
  GSource *source;
  GError *error = NULL;

  g_return_val_if_fail (GST_IS_RTSP_SERVER (server), 0);

  source = gst_rtsp_server_create_source (server, NULL, &error);
  if (source == NULL)
    goto no_source;

  res = g_source_attach (source, context);
  g_source_unref (source);

  return res;

  /* ERRORS */
no_source:
  {
    GST_ERROR_OBJECT (server, "failed to create watch: %s", error->message);
    g_error_free (error);
    return 0;
  }
}
