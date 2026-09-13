# VSO gnome-software plugin

This plugin provides graphical updates inside gnome-software for Vanilla OS.

## Dependencies

- `meson`
- `gnome-software-dev`
- `libglib2.0-dev`

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

## Use of Generative AI

Maintainers may use generative AI tools as assistants while working on gs-plugin-vso. Non-trivial assisted commits disclose the tool, model, and scope of the work.

AI tools may assist with code comments, documentation, repetitive code, and issue triage. Maintainers make project decisions and review every assisted change before it is merged.

Use these trailers for non-trivial assisted commits:

```plain
Assisted-by: <tool>:<model-version>
AI-Scope: <what the tool generated and the prompt or a short prompt summary>
```

Single-line completions, renames, and formatting changes do not need trailers.

Coding agents must also follow [AGENTS.md](AGENTS.md) before changing files,
creating commits, or opening pull requests.
