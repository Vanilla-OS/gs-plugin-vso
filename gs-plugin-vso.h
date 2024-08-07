/*
 * Copyright (C) 2024 Mateus Melchiades
 */

#pragma once

#include <glib-object.h>
#include <glib.h>
#include <gnome-software.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_VSO (gs_plugin_vso_get_type())

G_DECLARE_FINAL_TYPE(GsPluginVso, gs_plugin_vso, GS, PLUGIN_VSO, GsPlugin)

typedef struct {
    GsPlugin *plugin;
    GsAppList *list;
} GsPluginPkgAddFuncData;

G_END_DECLS
