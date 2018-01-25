/* Generated by dbus-binding-tool; do not edit! */

#include <glib.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#ifndef _DBUS_GLIB_ASYNC_DATA_FREE
#define _DBUS_GLIB_ASYNC_DATA_FREE
static inline void
_dbus_glib_async_data_free (gpointer stuff)
{
	g_slice_free (DBusGAsyncData, stuff);
}
#endif

#ifndef DBUS_GLIB_CLIENT_WRAPPERS_com_krontech_chronos_control
#define DBUS_GLIB_CLIENT_WRAPPERS_com_krontech_chronos_control

static inline gboolean
com_krontech_chronos_control_get_video_settings (DBusGProxy *proxy, GHashTable** OUT_data, GError **error)

{
  return dbus_g_proxy_call (proxy, "get_video_settings", error, G_TYPE_INVALID, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), OUT_data, G_TYPE_INVALID);
}

typedef void (*com_krontech_chronos_control_get_video_settings_reply) (DBusGProxy *proxy, GHashTable *OUT_data, GError *error, gpointer userdata);

static void
com_krontech_chronos_control_get_video_settings_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  GHashTable* OUT_data;
  dbus_g_proxy_end_call (proxy, call, &error, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &OUT_data, G_TYPE_INVALID);
  (*(com_krontech_chronos_control_get_video_settings_reply)data->cb) (proxy, OUT_data, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
com_krontech_chronos_control_get_video_settings_async (DBusGProxy *proxy, com_krontech_chronos_control_get_video_settings_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_slice_new (DBusGAsyncData);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "get_video_settings", com_krontech_chronos_control_get_video_settings_async_callback, stuff, _dbus_glib_async_data_free, G_TYPE_INVALID);
}
static inline gboolean
com_krontech_chronos_control_set_video_settings (DBusGProxy *proxy, const GHashTable* IN_data, GError **error)

{
  return dbus_g_proxy_call (proxy, "set_video_settings", error, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), IN_data, G_TYPE_INVALID, G_TYPE_INVALID);
}

typedef void (*com_krontech_chronos_control_set_video_settings_reply) (DBusGProxy *proxy, GError *error, gpointer userdata);

static void
com_krontech_chronos_control_set_video_settings_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID);
  (*(com_krontech_chronos_control_set_video_settings_reply)data->cb) (proxy, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
com_krontech_chronos_control_set_video_settings_async (DBusGProxy *proxy, const GHashTable* IN_data, com_krontech_chronos_control_set_video_settings_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_slice_new (DBusGAsyncData);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "set_video_settings", com_krontech_chronos_control_set_video_settings_async_callback, stuff, _dbus_glib_async_data_free, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), IN_data, G_TYPE_INVALID);
}
static inline gboolean
com_krontech_chronos_control_get_camera_data (DBusGProxy *proxy, GHashTable** OUT_data, GError **error)

{
  return dbus_g_proxy_call (proxy, "get_camera_data", error, G_TYPE_INVALID, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), OUT_data, G_TYPE_INVALID);
}

typedef void (*com_krontech_chronos_control_get_camera_data_reply) (DBusGProxy *proxy, GHashTable *OUT_data, GError *error, gpointer userdata);

static void
com_krontech_chronos_control_get_camera_data_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  GHashTable* OUT_data;
  dbus_g_proxy_end_call (proxy, call, &error, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &OUT_data, G_TYPE_INVALID);
  (*(com_krontech_chronos_control_get_camera_data_reply)data->cb) (proxy, OUT_data, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
com_krontech_chronos_control_get_camera_data_async (DBusGProxy *proxy, com_krontech_chronos_control_get_camera_data_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_slice_new (DBusGAsyncData);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "get_camera_data", com_krontech_chronos_control_get_camera_data_async_callback, stuff, _dbus_glib_async_data_free, G_TYPE_INVALID);
}
static inline gboolean
com_krontech_chronos_control_get_sensor_data (DBusGProxy *proxy, GHashTable** OUT_data, GError **error)

{
  return dbus_g_proxy_call (proxy, "get_sensor_data", error, G_TYPE_INVALID, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), OUT_data, G_TYPE_INVALID);
}

typedef void (*com_krontech_chronos_control_get_sensor_data_reply) (DBusGProxy *proxy, GHashTable *OUT_data, GError *error, gpointer userdata);

static void
com_krontech_chronos_control_get_sensor_data_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  GHashTable* OUT_data;
  dbus_g_proxy_end_call (proxy, call, &error, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &OUT_data, G_TYPE_INVALID);
  (*(com_krontech_chronos_control_get_sensor_data_reply)data->cb) (proxy, OUT_data, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
com_krontech_chronos_control_get_sensor_data_async (DBusGProxy *proxy, com_krontech_chronos_control_get_sensor_data_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_slice_new (DBusGAsyncData);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "get_sensor_data", com_krontech_chronos_control_get_sensor_data_async_callback, stuff, _dbus_glib_async_data_free, G_TYPE_INVALID);
}
static inline gboolean
com_krontech_chronos_control_get_timing_limits (DBusGProxy *proxy, const gint IN_hres, const gint IN_vres, GHashTable** OUT_data, GError **error)

{
  return dbus_g_proxy_call (proxy, "get_timing_limits", error, G_TYPE_INT, IN_hres, G_TYPE_INT, IN_vres, G_TYPE_INVALID, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), OUT_data, G_TYPE_INVALID);
}

typedef void (*com_krontech_chronos_control_get_timing_limits_reply) (DBusGProxy *proxy, GHashTable *OUT_data, GError *error, gpointer userdata);

static void
com_krontech_chronos_control_get_timing_limits_async_callback (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  DBusGAsyncData *data = (DBusGAsyncData*) user_data;
  GError *error = NULL;
  GHashTable* OUT_data;
  dbus_g_proxy_end_call (proxy, call, &error, dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &OUT_data, G_TYPE_INVALID);
  (*(com_krontech_chronos_control_get_timing_limits_reply)data->cb) (proxy, OUT_data, error, data->userdata);
  return;
}

static inline DBusGProxyCall*
com_krontech_chronos_control_get_timing_limits_async (DBusGProxy *proxy, const gint IN_hres, const gint IN_vres, com_krontech_chronos_control_get_timing_limits_reply callback, gpointer userdata)

{
  DBusGAsyncData *stuff;
  stuff = g_slice_new (DBusGAsyncData);
  stuff->cb = G_CALLBACK (callback);
  stuff->userdata = userdata;
  return dbus_g_proxy_begin_call (proxy, "get_timing_limits", com_krontech_chronos_control_get_timing_limits_async_callback, stuff, _dbus_glib_async_data_free, G_TYPE_INT, IN_hres, G_TYPE_INT, IN_vres, G_TYPE_INVALID);
}
#endif /* defined DBUS_GLIB_CLIENT_WRAPPERS_com_krontech_chronos_control */

G_END_DECLS
