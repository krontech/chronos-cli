#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "api/cam-rpc.h"

typedef struct {
    GObject parent;
} CamObject;

typedef struct {
    GObjectClass parent;
} CamObjectClass;

static gboolean cam_dbus_get_video_settings(CamObject *cam, GHashTable **data, GError **error);
static gboolean cam_dbus_set_video_settings(CamObject *cam, GHashTable *data, GError **error);
static gboolean cam_dbus_get_sensor_data(CamObject *cam, GHashTable **data, GError **error);

#include "api/cam-dbus-server.h"

/*-------------------------------------
 * DBUS/GObject Registration Mapping
 *-------------------------------------
 */
GType cam_object_get_type(void);

#define CAM_OBJECT_TYPE    (cam_object_get_type())

#define CAM_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_CAST((_obj_), CAM_OBJECT_TYPE, CamObject))
#define CAM_OBJECT_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_CAST((_kclass_), CAM_OBJECT_TYPE, CamObjectClass))
#define CAM_IS_OBJECT(_obj_) \
    (G_TYPE_CHECK_INSTANCE_TYPE(_obj_), CAM_OBJECT_TYPE))
#define CAM_IS_CLASS(_kclass_) \
    (G_TYPE_CHECK_CLASS_TYPE(_kclass_), CAM_OBJECT_TYPE))
#define CAM_GET_CLASS(_obj_) \
    (G_TYPE_INSTANCE_GET_CLASS(_obj_), CAM_OBJECT_TYPE, CamObjectClass))

G_DEFINE_TYPE(CamObject, cam_object, G_TYPE_OBJECT)

static void
cam_object_init(CamObject *cam)
{
    g_assert(cam != NULL);
}

static void
cam_object_class_init(CamObjectClass *kclass)
{
    g_assert(kclass != NULL);
    dbus_g_object_type_install_info(CAM_OBJECT_TYPE, &dbus_glib_cam_dbus_object_info);
}

/*-------------------------------------
 * DBUS API Calls
 *-------------------------------------
 */
static gboolean
cam_dbus_get_video_settings(CamObject *cam, GHashTable **data, GError **error)
{
    /* TODO: Implement Real Stuff */
    if (*data = cam_dbus_dict_new()) {
        cam_dbus_dict_add_uint(*data, "hRes", 1);
        cam_dbus_dict_add_uint(*data, "vRes", 2);
        cam_dbus_dict_add_uint(*data, "hOffset", 3);
        cam_dbus_dict_add_uint(*data, "vOffset", 4);
        cam_dbus_dict_add_uint(*data, "frameRate", 5);
        cam_dbus_dict_add_uint(*data, "exposureNsec", 6);
        cam_dbus_dict_add_uint(*data, "frameRate", 7);
        cam_dbus_dict_add_uint(*data, "gain", 8);
    }
    return (*data != NULL);
}

static gboolean
cam_dbus_set_video_settings(CamObject *cam, GHashTable *data, GError **error)
{
    /* TODO: Implement Real Stuff */
    g_hash_table_destroy(data);
    return TRUE;
}

static gboolean
cam_dbus_get_sensor_data(CamObject *cam, GHashTable **data, GError **error)
{
    if (*data = cam_dbus_dict_new()) {
        cam_dbus_dict_add_string(*data, "name", "LUX1310");
        cam_dbus_dict_add_boolean(*data, "monochrome", 1);
        cam_dbus_dict_add_uint(*data, "hMax", 1280);
        cam_dbus_dict_add_uint(*data, "hMax", 1280);
        cam_dbus_dict_add_uint(*data, "vMax", 1024);
        cam_dbus_dict_add_uint(*data, "hMin", 336);
        cam_dbus_dict_add_uint(*data, "vMin", 96);
        cam_dbus_dict_add_uint(*data, "hIncrement", 16);
        cam_dbus_dict_add_uint(*data, "vIncrement", 2);
        cam_dbus_dict_add_uint(*data, "minExposureNsec", UINT_MAX);
        cam_dbus_dict_add_uint(*data, "maxExposureNsec", 1000);
    }
    return (*data != NULL);
}

/*-------------------------------------
 * The actual application.
 *-------------------------------------
 */
int
main(void)
{
    DBusGConnection *bus = NULL;
    DBusGProxy *proxy = NULL;
    CamObject *cam;

    GMainLoop* mainloop = NULL;
    guint result;
    GError* error = NULL;

    g_type_init();
    mainloop = g_main_loop_new(NULL, FALSE);
    if (!mainloop) {
        perror("Main loop creation failed");
    }

    /* Bring up DBus and register with the system. */
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL) {
        g_printerr("Failed to get a bus D-Bus (%s)\n", error->message);
        return -1;
    }

    /* Connect to the system dbus to register our service. */
    proxy = dbus_g_proxy_new_for_name(bus,
                                        DBUS_SERVICE_DBUS,
                                        DBUS_PATH_DBUS,
                                        DBUS_INTERFACE_DBUS);
    if (proxy == NULL) {
        g_printerr("Failed to get a proxy for D-Bus (%s)\n", error->message);
        return -1;
    }
    if (!dbus_g_proxy_call(proxy,
                            "RequestName",
                            &error,
                            G_TYPE_STRING, CAM_DBUS_SERVICE,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_printerr("D-Bus.RequstName RPC failed (%s)\n", error->message);
        return -1;
    }
    if (result != 1) {
        perror("D-Bus.RequstName call failed");
        return -1;
    }

    /* Create the camera object and register with the D-Bus system. */
    cam = g_object_new(CAM_OBJECT_TYPE, NULL);
    dbus_g_connection_register_g_object(bus, CAM_DBUS_PATH, G_OBJECT(cam));

    /* Run the loop until something interesting happens. */
    g_main_loop_run(mainloop);
}
