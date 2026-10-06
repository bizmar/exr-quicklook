# dmgbuild settings for the release disk image. Used by Tools/package-release.sh:
#   dmgbuild -s Tools/dmg-settings.py -D stage=<dir> -D background=<tiff> \
#            -D icon=<icns> "<volume name>" <out.dmg>
# Icon positions are Finder window points, top-left origin, and must match
# Tools/make-dmg-background/main.swift.
import os

stage = defines["stage"]  # noqa: F821 -- provided by dmgbuild

format = "ULFO"            # LZFSE; readable on every macOS this app supports
filesystem = "HFS+"
size = None                # sized to fit

files = [
    os.path.join(stage, "EXR Quick Look.app"),
    os.path.join(stage, "INSTALL.txt"),
    os.path.join(stage, "LICENSE.txt"),
    os.path.join(stage, "THIRD_PARTY_NOTICES.md"),
]
symlinks = {"Applications": "/Applications"}
icon = defines["icon"]     # noqa: F821 -- volume icon

background = defines["background"]  # noqa: F821
window_rect = ((200, 120), (640, 420))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False
icon_size = 96
text_size = 13
arrange_by = None
icon_locations = {
    "EXR Quick Look.app": (170, 130),
    "Applications": (470, 130),
    "INSTALL.txt": (170, 320),
    "LICENSE.txt": (320, 320),
    "THIRD_PARTY_NOTICES.md": (470, 320),
}
