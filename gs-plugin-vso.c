/*
 * Copyright (C) 2024 Mateus Melchiades
 */

#include <glib.h>
#include <gnome-software.h>
#include <json-glib/json-glib.h>
#include <stdlib.h>

#include "gs-plugin-vso.h"

static gint get_priority_for_interactivity(gboolean interactive);
static void
setup_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void list_apps_thread_cb(GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable);
void add_package(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data);

struct _GsPluginVso {
    GsPlugin parent;
    GsWorkerThread *worker; /* (owned) */
};

G_DEFINE_TYPE(GsPluginVso, gs_plugin_vso, GS_TYPE_PLUGIN)

#define assert_in_worker(self) g_assert(gs_worker_thread_is_in_worker_context(self->worker))

const gchar *lock_path = "/tmp/ABSystem.Upgrade.lock";

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

    gs_plugin_add_rule(plugin, GS_PLUGIN_RULE_RUN_BEFORE, "os-release");
    gs_plugin_add_rule(plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_adopt_app(GsPlugin *plugin, GsApp *app)
{
    if (gs_app_get_kind(app) == AS_COMPONENT_KIND_DESKTOP_APP &&
        gs_app_has_management_plugin(app, NULL)) {

        gs_app_set_management_plugin(app, plugin);
        gs_app_add_quirk(app, GS_APP_QUIRK_PROVENANCE);
        // FIXME: Appinfo for pre-installed apps have no indidation of what is the package
        // name, so we have no way of knowing how to uninstall them.
        gs_app_add_quirk(app, GS_APP_QUIRK_COMPULSORY);
        gs_app_set_scope(app, AS_COMPONENT_SCOPE_SYSTEM);

        gs_app_set_metadata(app, "GnomeSoftware::SortKey", "200");
        gs_app_set_metadata(app, "GnomeSoftware::PackagingBaseCssColor", "warning_color");
        gs_app_set_metadata(app, "GnomeSoftware::PackagingIcon",
                            "org.vanillaos.FirstSetup-symbolic");

        gs_app_set_metadata(app, "GnomeSoftware::PackagingFormat", "System");

        gs_app_set_origin(app, "vso");
        gs_app_set_origin_ui(app, "Vanilla OS Base");
        gs_app_set_origin_hostname(app, "https://vanillaos.org");

        if (gs_plugin_cache_lookup(plugin, gs_app_get_id(app)) == NULL) {
            gs_plugin_cache_add(plugin, gs_app_get_id(app), app);
        }
    }
}

static void
gs_plugin_vso_list_apps_async(GsPlugin *plugin,
                              GsAppQuery *query,
                              GsPluginListAppsFlags flags,
                              GsPluginEventCallback event_callback,
                              void *event_user_data,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GsPluginVso *self     = GS_PLUGIN_VSO(plugin);
    g_autoptr(GTask) task = NULL;
    gboolean interactive  = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

    task =
        gs_plugin_list_apps_data_new_task(plugin, query, flags, event_callback, event_user_data, cancellable, callback, user_data);
    g_task_set_source_tag(task, gs_plugin_vso_list_apps_async);

    /* Queue a job to get the apps. */
    gs_worker_thread_queue(self->worker, get_priority_for_interactivity(interactive),
                           list_apps_thread_cb, g_steal_pointer(&task));
}

static void
list_apps_thread_cb(GTask *task,
                    gpointer source_object,
                    gpointer task_data,
                    GCancellable *cancellable)
{
    GsPluginVso *self          = GS_PLUGIN_VSO(source_object);
    g_autoptr(GsAppList) list  = gs_app_list_new();
    GsPluginListAppsData *data = task_data;
    GsApp *alternate_of        = NULL;

    assert_in_worker(self);

    if (data->query != NULL) {
        alternate_of = gs_app_query_get_alternate_of(data->query);
    }

    if (alternate_of != NULL) {
        GsApp *app = gs_plugin_cache_lookup(GS_PLUGIN(self), gs_app_get_id(alternate_of));

        gs_app_set_origin(app, "vso");
        gs_app_set_origin_ui(app, "Vanilla OS Base");
        gs_app_set_origin_hostname(app, "https://vanillaos.org");

        if (app != NULL)
            gs_app_list_add(list, app);
    }

    g_task_return_pointer(task, g_steal_pointer(&list), g_object_unref);
}

static GsAppList *
gs_plugin_vso_list_apps_finish(GsPlugin *plugin, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(g_task_get_source_tag(G_TASK(result)) == gs_plugin_vso_list_apps_async,
                         FALSE);
    return g_task_propagate_pointer(G_TASK(result), error);
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

void
gs_plugin_launch(GsPlugin *plugin, GsApp *app, GsPluginLaunchFlags flags, GsPluginPickDesktopFileCallback cb, gpointer cb_user_data, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
    /* only process this app if was created by this plugin */
    if (!gs_app_has_management_plugin(app, plugin))
        return;

    gs_plugin_app_launch_filtered_async(plugin, app, flags, cb, cb_user_data, cancellable, callback, user_data);
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
gs_plugin_update(GsPlugin *plugin, GsAppList *list, GCancellable *cancellable, GError **error)
{
    gboolean ours = false;
    for (guint i = 0; i < gs_app_list_length(list); i++) {
        GsApp *app = gs_app_list_index(list, i);
        if (!g_strcmp0(gs_app_get_id(app), "org.gnome.Software.OsUpdate")) {
            ours = true;
            break;
        }
    }

    // Nothing to do with us...
    if (!ours)
        return FALSE;

    // Cannot update if transactions are locked
    g_autoptr(GFile) lock_file    = g_file_new_for_path(lock_path);
    g_autoptr(GError) local_error = NULL;
    if (g_file_query_exists(lock_file, cancellable)) {
        g_set_error_literal(&local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
                            "Another transaction has already been executed, you must reboot your "
                            "system before starting a new transaction.");
        *error = g_steal_pointer(&local_error);

        return FALSE;
    }

    // Call trigger-update
    const gchar *cmd = "pkexec vso sys-upgrade upgrade";

    g_autoptr(GSubprocess) subprocess = NULL;
    guint exit_status                 = -1;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, error, "sh", "-c", cmd, NULL);
    if (!g_subprocess_wait(subprocess, cancellable, error))
        return FALSE;

    exit_status = g_subprocess_get_exit_status(subprocess);

    if (exit_status != EXIT_SUCCESS) {
        g_set_error_literal(&local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
                            "VSO failed to update the system, please try again later.");
        *error = g_steal_pointer(&local_error);

        return FALSE;
    }

    return TRUE;
}

void
add_package(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    GsPluginPkgAddFuncData *data = user_data;
    GsPlugin *plugin             = data->plugin;
    GsAppList *list              = data->list;

    JsonObject *pkg_info     = json_node_get_object(element_node);
    const gchar *name        = NULL;
    const gchar *old_version = NULL;
    const gchar *new_version = NULL;

    if (json_object_has_member(pkg_info, "name"))
        name = json_object_get_string_member(pkg_info, "name");
    else
        name = "";

    if (json_object_has_member(pkg_info, "previous_version"))
        old_version = json_object_get_string_member(pkg_info, "previous_version");

    if (json_object_has_member(pkg_info, "new_version"))
        new_version = json_object_get_string_member(pkg_info, "new_version");

    g_debug("Adding package %s: %s -> %s", name, old_version, new_version);

    g_autoptr(GsApp) app = gs_app_new(NULL);
    gs_app_set_management_plugin(app, plugin);
    gs_app_add_quirk(app, GS_APP_QUIRK_NEEDS_REBOOT);
    gs_app_set_bundle_kind(app, AS_BUNDLE_KIND_PACKAGE);
    gs_app_set_scope(app, AS_COMPONENT_SCOPE_SYSTEM);
    gs_app_set_kind(app, AS_COMPONENT_KIND_GENERIC);
    gs_app_set_size_download(app, GS_SIZE_TYPE_UNKNOWN, 0);
    gs_app_add_source(app, g_strdup(name));
    gs_app_set_name(app, GS_APP_QUALITY_LOWEST, g_strdup(name));

    if (old_version != NULL && new_version == NULL) { // Removed
        gs_app_set_version(app, g_strdup(old_version));
        gs_app_set_state(app, GS_APP_STATE_UNAVAILABLE);
    } else if (old_version == NULL && new_version != NULL) { // Added
        gs_app_set_version(app, g_strdup(new_version));
        gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
    } else { // Modified
        gs_app_set_version(app, g_strdup(old_version));
        gs_app_set_update_version(app, g_strdup(new_version));
        gs_app_set_state(app, GS_APP_STATE_UPDATABLE);
    }

    gs_plugin_cache_add(plugin, name, app);
    gs_app_list_add(list, app);
}

gboolean
gs_plugin_add_updates(GsPlugin *plugin, GsAppList *list, GCancellable *cancellable, GError **error)
{
    const gchar *cmd = "pkexec vso sys-upgrade check --json";

    g_autoptr(GSubprocess) subprocess = NULL;
    GInputStream *input_stream;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error, "sh", "-c", cmd, NULL);
    if (!g_subprocess_wait(subprocess, cancellable, error))
        return FALSE;

    input_stream = g_subprocess_get_stdout_pipe(subprocess);

    if (input_stream != NULL) {
        g_autoptr(GByteArray) cmd_out = g_byte_array_new();
        gchar buffer[4096];
        gsize nread = 0;
        gboolean success;
        g_auto(GStrv) splits       = NULL;
        g_auto(GStrv) pkg_splits   = NULL;
        g_auto(GStrv) pkg_versions = NULL;

        while (success = g_input_stream_read_all(input_stream, buffer, sizeof(buffer), &nread,
                                                 cancellable, error),
               success && nread > 0) {
            g_byte_array_append(cmd_out, (const guint8 *)buffer, nread);
        }

        // If we have a valid output
        if (success && cmd_out->len > 0) {
            // NUL-terminate the array, to use it as a string
            g_byte_array_append(cmd_out, (const guint8 *)"", 1);
            g_debug("Got JSON: %s", cmd_out->data);

            splits = g_strsplit((gchar *)cmd_out->data, "\n", -1);

            JsonNode *output_json = json_from_string(splits[g_strv_length(splits) - 2], error);
            if (output_json == NULL) {
                (*error)->message = g_strconcat(
                    "Error parsing VSO upgrade-check output: ", (*error)->message, NULL);
                g_debug("%s", (*error)->message);
                return FALSE;
            }

            JsonObject *update_info = json_node_get_object(output_json);
            if (!json_object_get_boolean_member(update_info, "hasUpdate")) {
                g_debug("No updates");
                json_node_free(output_json);
                return TRUE;
            }

            g_debug("New image digest: %s",
                    json_object_get_string_member(update_info, "newDigest"));

            GsPluginPkgAddFuncData data = {.plugin = plugin, .list = list};

            // Parse image packages
            JsonObject *system_pkgs =
                json_object_get_object_member(update_info, "systemPackageDiff");
            json_array_foreach_element(json_object_get_array_member(system_pkgs, "added"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(system_pkgs, "upgraded"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(system_pkgs, "downgraded"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(system_pkgs, "removed"),
                                       add_package, &data);

            // Parse overlay packages
            JsonObject *user_pkgs =
                json_object_get_object_member(update_info, "overlayPackageDiff");
            json_array_foreach_element(json_object_get_array_member(user_pkgs, "added"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(user_pkgs, "upgraded"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(user_pkgs, "downgraded"),
                                       add_package, &data);
            json_array_foreach_element(json_object_get_array_member(user_pkgs, "removed"),
                                       add_package, &data);

            json_node_free(output_json);
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

    plugin_class->setup_async      = gs_plugin_vso_setup_async;
    plugin_class->setup_finish     = gs_plugin_vso_setup_finish;
    plugin_class->list_apps_async  = gs_plugin_vso_list_apps_async;
    plugin_class->list_apps_finish = gs_plugin_vso_list_apps_finish;
}

GType
gs_plugin_query_type(void)
{
    return GS_TYPE_PLUGIN_VSO;
}
