struct pipeline_param {};
struct enumval playback_states[] = {};
struct enumval focus_peak_colors[] = {};
static gboolean cam_focus_peak_color_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_focus_peak_level_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_zebra_level_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_overlay_enable_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_overlay_format_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_playback_position_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_playback_rate_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_playback_start_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
static gboolean cam_playback_length_setter(struct pipeline_state *state, const struct pipeline_param *p, GValue *val, char *err)
#define param_offset(_member_) offsetof(struct pipeline_state, _member_)
static const struct pipeline_param cam_dbus_params[] = {
    { "videoState",         G_TYPE_ENUM,    PARAM_FLAG_NOTIFY, param_offset(playstate),         playback_states,    NULL},
    /* Exposure and focus aids. */
    { "overlayEnable",      G_TYPE_BOOLEAN, PARAM_FLAG_NOTIFY, param_offset(overlay.enable),    NULL,               cam_overlay_enable_setter},
    { "overlayFormat",      G_TYPE_STRING,  PARAM_FLAG_NOTIFY, param_offset(overlay.format),    NULL,               cam_overlay_format_setter},
    { "focusPeakingColor",  G_TYPE_ENUM,    PARAM_FLAG_NOTIFY, param_offset(config.peak_color), focus_peak_colors,  cam_focus_peak_color_setter},
    { "focusPeakingLevel",  G_TYPE_DOUBLE,  PARAM_FLAG_NOTIFY, param_offset(config.peak_level), NULL,               cam_focus_peak_level_setter},
    { "zebraLevel",         G_TYPE_DOUBLE,  PARAM_FLAG_NOTIFY, param_offset(config.zebra_level),NULL,               cam_zebra_level_setter},
    /* Playback position and rate. */
    { "playbackRate",       G_TYPE_LONG,    PARAM_FLAG_NOTIFY, param_offset(playrate),          NULL,               cam_playback_rate_setter},
    { "playbackPosition",   G_TYPE_LONG,    0,                 param_offset(position),          NULL,               cam_playback_position_setter},
    { "playbackStart",      G_TYPE_ULONG,   PARAM_FLAG_NOTIFY, param_offset(playstart),         NULL,               cam_playback_start_setter},
    { "playbackLength",     G_TYPE_ULONG,   PARAM_FLAG_NOTIFY, param_offset(playlength),        NULL,               cam_playback_length_setter},
    /* Quantity of recorded video. */
    { "totalFrames",        G_TYPE_LONG,    0,                 param_offset(seglist.totalframes), NULL,             NULL},
    { "totalSegments",      G_TYPE_LONG,    0,                 param_offset(seglist.totalsegs),   NULL,             NULL},
    { NULL, G_TYPE_INVALID, 0, 0, NULL, NULL}
};
gboolean dbus_get_param(struct pipeline_state *state, const char *name, GHashTable *data)
static gboolean dbus_set_enum(struct pipeline_state *state, const struct pipeline_param *p, GValue *gval, char *err)
gboolean dbus_set_param(struct pipeline_state *state, const char *name, GValue *gval, char *err)
GHashTable * dbus_describe_params(struct pipeline_state *state)
