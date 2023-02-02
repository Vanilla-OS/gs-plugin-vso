# VSO gnome-software plugin

This plugin provides graphical updates inside gnome-software for Vanilla OS.


## Dependencies

- meson
- gnome-software-dev
- libglib2.0-dev


## Building

```sh
$ make                  # Creates build directory and compiles, OR
$ make reconfigure      # Re-creates build directory and compiles (use this after modifying meson.build)
```

## Installing

In order to install the plugin, you need to modify a sub-directory of `/usr`, which is read-only.
There are two ways of doing this: a temporary one, which will get overwritten after a reboot, and
a permanent one, which requires a reboot to have effect.

```sh
$ sudo make test-install     # Temporary install (use this while developing)
$ sudo make install          # Permanent install (use this for effectively installing)
```
