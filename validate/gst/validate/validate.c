/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * validate.c - Validate generic functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
/**
 * SECTION:validate
 * @title: Initialization
 * @short_description: Initialize GstValidate
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <locale.h>             /* for LC_NUMERIC */

#include <string.h>
/* For g_stat () */
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <math.h>

#include "validate.h"
#include "gst-validate-utils.h"
#include "gst-validate-internal.h"

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN     /* prevents from including too many things */
#include <windows.h>            /* GetStdHandle, windows console */
HMODULE _priv_gstvalidate_dll_handle = NULL;
#endif /* G_OS_WIN32 */

GST_DEBUG_CATEGORY (gstvalidate_debug);

static GMutex _gst_validate_registry_mutex;
static GstRegistry *_gst_validate_registry_default = NULL;

static GList *core_config = NULL;
static GList *testfile_structs = NULL;
static gchar *global_testfile = NULL;
static gboolean validate_initialized = FALSE;
static gboolean loaded_globals = FALSE;
GstClockTime _priv_start_time;

GQuark _Q_VALIDATE_MONITOR;

#ifdef G_OS_WIN32
BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);
BOOL WINAPI
DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH)
    _priv_gstvalidate_dll_handle = (HMODULE) hinstDLL;

  return TRUE;
}
#endif /* G_OS_WIN32 */

static GstRegistry *
gst_validate_registry_get (void)
{
  GstRegistry *registry;

  g_mutex_lock (&_gst_validate_registry_mutex);
  if (G_UNLIKELY (!_gst_validate_registry_default)) {
    _gst_validate_registry_default = g_object_new (GST_TYPE_REGISTRY, NULL);
    gst_object_ref_sink (GST_OBJECT_CAST (_gst_validate_registry_default));
  }
  registry = _gst_validate_registry_default;
  g_mutex_unlock (&_gst_validate_registry_mutex);

  return registry;
}

#define GST_VALIDATE_PLUGIN_CONFIG "gst-validate-plugin-config"

static void
_free_plugin_config (gpointer data)
{
  g_list_free_full (data, (GDestroyNotify) gst_structure_free);
}

/* Copied from gststructure.c to avoid assertion */
static gboolean
gst_structure_validate_name (const gchar * name)
{
  const gchar *s;

  g_return_val_if_fail (name != NULL, FALSE);

  if (G_UNLIKELY (!g_ascii_isalpha (*name))) {
    GST_INFO ("Invalid character '%c' at offset 0 in structure name: %s",
        *name, name);
    return FALSE;
  }

  /* FIXME: test name string more */
  s = &name[1];
  while (*s && (g_ascii_isalnum (*s) || strchr ("/-_.:+", *s) != NULL))
    s++;
  if (*s == ',')
    return TRUE;

  if (G_UNLIKELY (*s != '\0')) {
    GST_INFO ("Invalid character '%c' at offset %" G_GUINTPTR_FORMAT " in"
        " structure name: %s", *s, ((guintptr) s - (guintptr) name), name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
_set_vars_func (GQuark field_id, const GValue * value, GstStructure * vars)
{
  gst_structure_id_set_value (vars, field_id, value);

  return TRUE;
}

static GstStructure *
get_test_file_meta (void)
{
  GList *tmp;

  for (tmp = testfile_structs; tmp; tmp = tmp->next) {
    if (gst_structure_has_name (tmp->data, "meta"))
      return tmp->data;
  }

  return NULL;
}

/* Takes ownership of @structures */
static GList *
get_config_from_structures (GList * structures, GstStructure * local_vars,
    const gchar * suffix)
{
  GList *tmp, *result = NULL;

  for (tmp = structures; tmp; tmp = tmp->next) {
    GstStructure *structure = tmp->data;

    if (gst_structure_has_name (structure, suffix)) {
      if (gst_structure_has_field (structure, "set-vars")) {
        gst_structure_remove_field (structure, "set-vars");
        if (!local_vars) {
          GST_WARNING ("Unused `set-vars` config: %" GST_PTR_FORMAT, structure);
          continue;
        }
        gst_structure_foreach (structure,
            (GstStructureForeachFunc) _set_vars_func, local_vars);
        gst_structure_free (structure);
      } else {
        gst_validate_structure_resolve_variables (structure, local_vars);
        result = g_list_append (result, structure);
      }
    } else {
      if (!loaded_globals && gst_structure_has_name (structure, "set-globals")) {
        gst_validate_structure_resolve_variables (structure, local_vars);
        gst_validate_set_globals (structure);
      }
      gst_structure_free (structure);
    }
  }
  g_list_free (structures);

  return result;
}

static GList *
create_config (const gchar * config, const gchar * suffix)
{
  GstStructure *local_vars;
  GList *structures = NULL, *result = NULL;
  gchar *config_file = NULL;

  if (!suffix) {
    GST_WARNING ("suffix is NULL");
    return NULL;
  }

  local_vars = gst_structure_new_empty ("vars");
  structures =
      gst_validate_utils_structs_parse_from_filename (config, &config_file);
  if (!structures) {
    GstCaps *confs = NULL;

    if (gst_structure_validate_name (config))
      confs = gst_caps_from_string (config);

    if (confs) {
      gint i;

      for (i = 0; i < gst_caps_get_size (confs); i++) {
        GstStructure *structure = gst_caps_get_structure (confs, i);

        if (gst_structure_has_name (structure, suffix))
          structures =
              g_list_append (structures, gst_structure_copy (structure));
      }

      gst_caps_unref (confs);
    }
  }

  gst_validate_structure_set_variables_from_struct_file (local_vars,
      config_file);
  g_free (config_file);

  result = get_config_from_structures (structures, local_vars, suffix);

  loaded_globals = TRUE;
  gst_structure_free (local_vars);
  return result;
}

static GList *
gst_validate_get_testfile_configs (const gchar * suffix)
{
  GList *res = NULL;
  gchar **config_strs = NULL, *filename = NULL, *debug = NULL;
  gint current_lineno = -1;
  GstStructure *meta = get_test_file_meta ();

  if (!meta)
    return NULL;

  gst_structure_get (meta,
      "__lineno__", G_TYPE_INT, &current_lineno,
      "__debug__", G_TYPE_STRING, &debug,
      "__filename__", G_TYPE_STRING, &filename, NULL);
  config_strs = gst_validate_utils_get_strv (meta, "configs");

  if (config_strs) {
    gint i;

    for (i = 0; config_strs[i]; i++) {
      GstStructure *tmpstruct =
          gst_structure_from_string (config_strs[i], NULL);

      if (tmpstruct == NULL) {
        gst_validate_abort ("%s:%d: Invalid structure\n  %4d | %s\n%s",
            filename, current_lineno, current_lineno, config_strs[i], debug);
      }

      gst_structure_set (tmpstruct,
          "__lineno__", G_TYPE_INT, current_lineno,
          "__filename__", G_TYPE_STRING, filename,
          "__debug__", G_TYPE_STRING, debug, NULL);
      res = g_list_append (res, tmpstruct);
    }
  }

  g_free (filename);
  g_free (debug);
  g_strfreev (config_strs);

  return get_config_from_structures (res, NULL, suffix);
}

/**
 * gst_validate_plugin_get_config:
 * @plugin: a #GstPlugin, or #NULL
 *
 * Return the configuration specific to @plugin, or the "core" one if @plugin
 * is #NULL
 *
 * Returns: (transfer none) (element-type GstStructure): a list of #GstStructure
 */
GList *
gst_validate_plugin_get_config (GstPlugin * plugin)
{
  const gchar *suffix;
  GList *plugin_conf = NULL;

  if (plugin) {
    if ((plugin_conf =
            g_object_get_data (G_OBJECT (plugin), GST_VALIDATE_PLUGIN_CONFIG)))
      return plugin_conf;

    suffix = gst_plugin_get_name (plugin);
  } else {
    if (core_config)
      return core_config;

    suffix = "core";
  }

  plugin_conf = gst_validate_get_config (suffix);
  if (plugin)
    g_object_set_data_full (G_OBJECT (plugin), GST_VALIDATE_PLUGIN_CONFIG,
        plugin_conf, _free_plugin_config);
  else
    core_config = plugin_conf;

  return plugin_conf;
}

GList *
gst_validate_get_config (const gchar * structname)
{

  const gchar *config;
  GStrv tmp;
  guint i;
  GList *configs;


  configs = gst_validate_get_testfile_configs (structname);
  config = g_getenv ("GST_VALIDATE_CONFIG");
  if (!config) {
    return configs;
  }

  tmp = g_strsplit (config, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; tmp[i] != NULL; i++) {
    GList *l;

    l = create_config (tmp[i], structname);
    if (l)
      configs = g_list_concat (configs, l);
  }
  g_strfreev (tmp);

  return configs;
}

static void
gst_validate_init_plugins (void)
{
  GstRegistry *registry;
  const gchar *plugin_path;

  gst_registry_fork_set_enabled (FALSE);
  registry = gst_validate_registry_get ();

  plugin_path = g_getenv ("GST_VALIDATE_PLUGIN_PATH");
  if (plugin_path) {
    char **list;
    int i;

    GST_DEBUG ("GST_VALIDATE_PLUGIN_PATH set to %s", plugin_path);
    list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; list[i]; i++) {
      gst_registry_scan_path (registry, list[i]);
    }
    g_strfreev (list);
  } else {
    GST_DEBUG ("GST_VALIDATE_PLUGIN_PATH not set");
  }

  if (plugin_path == NULL) {
    char *home_plugins;

    /* plugins in the user's home directory take precedence over
     * system-installed ones */
    home_plugins = g_build_filename (g_get_user_data_dir (),
        "gstreamer-" GST_API_VERSION, "plugins", NULL);

    GST_DEBUG ("scanning home plugins %s", home_plugins);
    gst_registry_scan_path (registry, home_plugins);
    g_free (home_plugins);

    /* add the main (installed) library path */

#ifdef G_OS_WIN32
    {
      char *base_dir;
      char *dir;

      base_dir =
          g_win32_get_package_installation_directory_of_module
          (_priv_gstvalidate_dll_handle);

      dir = g_build_filename (base_dir,
          "lib", "gstreamer-" GST_API_VERSION, "validate", NULL);

      GST_DEBUG ("scanning DLL dir %s", dir);

      gst_registry_scan_path (registry, dir);

      g_free (dir);
      g_free (base_dir);
    }
#else
    gst_registry_scan_path (registry, VALIDATEPLUGINDIR);
#endif
  }
  gst_registry_fork_set_enabled (TRUE);
}

void
gst_validate_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (gstvalidate_debug, "validate", 0,
      "Validation library");
}

/**
 * gst_validate_init:
 *
 * Initializes GstValidate. Call this before any usage of GstValidate.
 * You should take care of initializing GStreamer before calling this
 * function.
 */
void
gst_validate_init (void)
{
  if (validate_initialized) {
    return;
  }
  gst_validate_init_debug ();
  _priv_start_time = gst_util_get_timestamp ();
  _Q_VALIDATE_MONITOR = g_quark_from_static_string ("validate-monitor");

  setlocale (LC_NUMERIC, "C");

  /* init the report system (can be called multiple times) */
  gst_validate_report_init ();

  /* Init the scenario system */
  init_scenarios ();

  /* Ensure we load overrides before any use of a monitor */
  gst_validate_override_registry_preload ();

  validate_initialized = TRUE;

  gst_validate_init_plugins ();
  gst_validate_init_runner ();
}

void
gst_validate_deinit (void)
{
  g_mutex_lock (&_gst_validate_registry_mutex);
  _free_plugin_config (core_config);
  gst_validate_deinit_runner ();

  gst_validate_scenario_deinit ();

  g_clear_object (&_gst_validate_registry_default);

  g_list_free_full (testfile_structs, (GDestroyNotify) gst_structure_free);
  testfile_structs = NULL;
  g_clear_pointer (&global_testfile, g_free);

  _priv_validate_override_registry_deinit ();
  core_config = NULL;
  validate_initialized = FALSE;
  gst_validate_report_deinit ();

  g_mutex_unlock (&_gst_validate_registry_mutex);
  g_mutex_clear (&_gst_validate_registry_mutex);
}

gboolean
gst_validate_is_initialized (void)
{
  return validate_initialized;
}

gboolean
gst_validate_get_test_file_scenario (GList ** structs,
    const gchar ** scenario_name, gchar ** original_name)
{
  GList *res = NULL, *tmp;
  GstStructure *meta = get_test_file_meta ();

  if (!testfile_structs)
    return FALSE;

  if (meta && gst_structure_has_field (meta, "scenario")) {
    *scenario_name = gst_structure_get_string (meta, "scenario");

    return TRUE;
  }

  for (tmp = testfile_structs; tmp; tmp = tmp->next) {
    GstStructure *structure = NULL;

    if (gst_structure_has_name (tmp->data, "set-globals"))
      continue;

    structure = gst_structure_copy (tmp->data);
    if (gst_structure_has_name (structure, "meta"))
      gst_structure_remove_fields (structure, "configs", "gst-validate-args",
          NULL);
    res = g_list_append (res, structure);
  }

  *structs = res;
  *original_name = global_testfile;

  return TRUE;
}

GstStructure *
gst_validate_setup_test_file (const gchar * testfile, gboolean use_fakesinks)
{
  const gchar *tool;
  GstStructure *res = NULL;

  if (global_testfile)
    gst_validate_abort ("A testfile was already loaded: %s", global_testfile);

  gst_validate_set_globals (NULL);
  gst_validate_structure_set_variables_from_struct_file (NULL, testfile);
  testfile_structs =
      gst_validate_utils_structs_parse_from_filename (testfile, NULL);

  if (!testfile_structs)
    gst_validate_abort ("Could not load test file: %s", testfile);

  res = testfile_structs->data;
  if (gst_structure_has_name (testfile_structs->data, "set-globals")) {
    GstStructure *globals = testfile_structs->data;
    gst_validate_set_globals (globals);
    res = testfile_structs->next->data;
  }

  if (!gst_structure_has_name (res, "meta"))
    gst_validate_abort
        ("First structure of a .validatetest file should be a `meta` or "
        "`set-gobals` then `meta`, got: %s", gst_structure_to_string (res));

  register_action_types ();
  gst_validate_scenario_check_and_set_needs_clock_sync (testfile_structs, &res);

  gst_validate_set_test_file_globals (res, testfile, use_fakesinks);

  gst_validate_structure_resolve_variables (res, NULL);

  tool = gst_structure_get_string (res, "tool");
  if (!tool)
    tool = "gst-validate-" GST_API_VERSION;

  if (g_strcmp0 (tool, g_get_prgname ()))
    gst_validate_abort
        ("Validate test file: '%s' was made to be run with '%s' not '%s'",
        testfile, tool, g_get_prgname ());
  global_testfile = g_strdup (testfile);

  return res;
}
