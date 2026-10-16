# Capturing screenshots

The `screenshot` helpers capture actual GTK application widgets in English,
using fixed human-player moves and no Ollama requests. They produce a
configuration view and a game view for each app, without saving game histories
or changing language preferences.

Build the helpers and run them on a graphical session:

```sh
meson setup four-in-a-row/build-ui four-in-a-row -Dapp=true -Dui_tests=true
meson compile -C four-in-a-row/build-ui
meson setup tic-tac-toe/build-ui tic-tac-toe -Dapp=true -Dui_tests=true
meson compile -C tic-tac-toe/build-ui
four-in-a-row/build-ui/screenshot screenshots
tic-tac-toe/build-ui/screenshot screenshots
```

If the build directories already exist, use `meson setup --reconfigure`.
For a virtual display, prefix each capture with
`GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session --`.
CI captures all four PNG files and uploads an `application-screenshots` artifact.
The output directory is ignored by Git. Review the images, then copy chosen
ones into `docs/images/` and link them from the main README. No preview images
are checked in until they can be generated and visually reviewed.
