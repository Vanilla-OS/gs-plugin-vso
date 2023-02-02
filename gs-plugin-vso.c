/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#include <glib.h>
#include <gnome-software.h>
#include <stdlib.h>

#include "gs-plugin-vso.h"

static gint get_priority_for_interactivity(gboolean interactive);
static void
setup_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);

struct _GsPluginVso {
    GsPlugin parent;
    GsWorkerThread *worker; /* (owned) */
};

G_DEFINE_TYPE(GsPluginVso, gs_plugin_vso, GS_TYPE_PLUGIN)

#define assert_in_worker(self) g_assert(gs_worker_thread_is_in_worker_context(self->worker))

static void
gs_plugin_vso_dispose(GObject *object)
{
    GsPluginVso *self = GS_PLUGIN_VSO(object);

    g_clear_object(&self->worker);
    G_OBJECT_CLASS(gs_plugin_vso_parent_class)->dispose(object);
}

static void
gs_plugin_vso_finalize(GObject *object)
{
    G_OBJECT_CLASS(gs_plugin_vso_parent_class)->finalize(object);
}

static gint
get_priority_for_interactivity(gboolean interactive)
{
    return interactive ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW;
}

static void
gs_plugin_vso_setup_async(GsPlugin *plugin,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GsPluginVso *self     = GS_PLUGIN_VSO(plugin);
    g_autoptr(GTask) task = NULL;

    task = g_task_new(plugin, cancellable, callback, user_data);
    g_task_set_source_tag(task, gs_plugin_vso_setup_async);

    // Start up a worker thread to process all the plugin’s function calls.
    self->worker = gs_worker_thread_new("gs-plugin-vso");

    gs_worker_thread_queue(self->worker, G_PRIORITY_DEFAULT, setup_thread_cb,
                           g_steal_pointer(&task));
}

static void
setup_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    g_task_return_boolean(task, TRUE);
}

static gboolean
gs_plugin_vso_setup_finish(GsPlugin *plugin, GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void
gs_plugin_vso_init(GsPluginVso *self)
{
    GsPlugin *plugin = GS_PLUGIN(self);

    gs_plugin_add_rule(plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static gboolean
plugin_vso_pick_desktop_file_cb(GsPlugin *plugin,
                                GsApp *app,
                                const gchar *filename,
                                GKeyFile *key_file)
{
    return strstr(filename, "/snapd/") == NULL && strstr(filename, "/snap/") == NULL &&
           strstr(filename, "/flatpak/") == NULL &&
           g_key_file_has_group(key_file, "Desktop Entry") &&
           !g_key_file_has_key(key_file, "Desktop Entry", "X-Flatpak", NULL) &&
           !g_key_file_has_key(key_file, "Desktop Entry", "X-SnapInstanceName", NULL);
}

gboolean
gs_plugin_launch(GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
    /* only process this app if was created by this plugin */
    if (!gs_app_has_management_plugin(app, plugin))
        return TRUE;

    return gs_plugin_app_launch_filtered(plugin, app, plugin_vso_pick_desktop_file_cb, NULL, error);
}

gboolean
gs_plugin_add_updates_historical(GsPlugin *plugin,
                                 GsAppList *list,
                                 GCancellable *cancellable,
                                 GError **error)
{
    return TRUE;
}

gboolean
gs_plugin_add_updates(GsPlugin *plugin, GsAppList *list, GCancellable *cancellable, GError **error)
{
    /* const gchar *cmd = "pkexec vso update-check"; */
    const gchar *cmd = "pkexec /home/matbme/Documents/vanilla-system-operator/vso update-check";
    g_autoptr(GError) local_error = NULL;

    g_autoptr(GSubprocess) subprocess = NULL;
    GInputStream *input_stream;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error, "sh", "-c", cmd, NULL);
    if (!g_subprocess_wait(subprocess, cancellable, error))
        return FALSE;

    input_stream = g_subprocess_get_stdout_pipe(subprocess);

    if (input_stream != NULL) {
        g_autoptr(GByteArray) ls_out = g_byte_array_new();
        gchar buffer[4096];
        gsize nread = 0;
        gboolean success;
        g_auto(GStrv) splits       = NULL;
        g_auto(GStrv) pkg_splits   = NULL;
        g_auto(GStrv) pkg_versions = NULL;

        while (success = g_input_stream_read_all(input_stream, buffer, sizeof(buffer), &nread,
                                                 cancellable, error),
               success && nread > 0) {
            g_byte_array_append(ls_out, (const guint8 *)buffer, nread);
        }

        if (local_error != NULL) {
            g_debug("Error checking for updates: %s", local_error->message);
            return FALSE;
        }

        // If we have a valid output
        if (success && ls_out->len > 0) {
            // NUL-terminate the array, to use it as a string
            g_byte_array_append(ls_out, (const guint8 *)"", 1);

            splits = g_strsplit((gchar *)ls_out->data, "\n", -1);

            // Format: "  - %s\t%s -> %s", pkg_name, pkg_oldver, pkg_newver
            for (guint i = 0; i < g_strv_length(splits); i++) {
                if (g_str_has_prefix(splits[i], "  - ")) {
                    gchar *split_no_prefix = &splits[i][4];

                    pkg_splits   = g_strsplit(split_no_prefix, "\t", 2);
                    pkg_versions = g_strsplit(pkg_splits[1], " -> ", 2);

                    g_debug("Package: %s", pkg_splits[0]);
                    g_debug("Old ver: %s", pkg_versions[0]);
                    g_debug("New ver: %s", pkg_versions[1]);

                    g_autoptr(GsApp) app = gs_app_new(NULL);
                    gs_app_set_management_plugin(app, plugin);
                    gs_app_add_quirk(app, GS_APP_QUIRK_NEEDS_REBOOT);
                    gs_app_set_scope(app, AS_COMPONENT_SCOPE_SYSTEM);
                    gs_app_set_bundle_kind(app, AS_BUNDLE_KIND_PACKAGE);
                    gs_app_set_kind(app, AS_COMPONENT_KIND_GENERIC);
                    gs_app_set_size_download(app, GS_SIZE_TYPE_VALID, 0);
                    gs_app_add_source(app, g_strdup(pkg_splits[0]));
                    gs_app_set_version(app, g_strdup(pkg_versions[0]));
                    gs_app_set_update_version(app, g_strdup(pkg_versions[1]));
                    gs_app_set_state(app, GS_APP_STATE_UPDATABLE);

                    gs_plugin_cache_add(plugin, pkg_splits[0], app);
                    gs_app_list_add(list, app);
                }
            }
        }
    }

    return TRUE;
}

static void
gs_plugin_vso_class_init(GsPluginVsoClass *klass)
{
    GObjectClass *object_class  = G_OBJECT_CLASS(klass);
    GsPluginClass *plugin_class = GS_PLUGIN_CLASS(klass);

    object_class->dispose  = gs_plugin_vso_dispose;
    object_class->finalize = gs_plugin_vso_finalize;

    plugin_class->setup_async  = gs_plugin_vso_setup_async;
    plugin_class->setup_finish = gs_plugin_vso_setup_finish;
}

GType
gs_plugin_query_type(void)
{
    return GS_TYPE_PLUGIN_VSO;
}
