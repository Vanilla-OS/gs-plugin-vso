project('gs_plugin_vso', 'c')

files = [
  'gs-plugin-vso.c',
]

deps = [
  dependency('glib-2.0', version : '>= 2.70.0'),
  dependency('json-glib-1.0', version : '>= 1.6.0'),
  dependency('gnome-software'),
  dependency('polkit-gobject-1')
]

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', 'gnome-software')
conf.set('HAVE_POLKIT', 1)
configure_file(
  output : 'config.h',
  configuration : conf
)

args = [
  '-fPIC',
  '-DI_KNOW_THE_GNOME_SOFTWARE_API_IS_SUBJECT_TO_CHANGE',
  '-DG_LOG_DOMAIN="GsPluginVSO"'
]

shared_module(
  'gs_plugin_vso',
  files,
  dependencies: deps,
  c_args: args
)
