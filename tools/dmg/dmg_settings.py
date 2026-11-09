# dmgbuild settings for OSCar-Editor-mac.dmg (see make_dmg.sh).
# The window shows the three bundles, each with a shortcut to the folder it goes in, joined by
# the arrows of the background. Positions match make_background.py.
import os

src = defines["src"]              # folder holding the three bundles
work = defines["work"]            # the background, prepared by make_dmg.sh

format = "UDZO"
filesystem = "HFS+"
files = [
    (os.path.join(src, "OSCar Editor.component"), "OSCar Editor.component"),
    (os.path.join(src, "OSCar Editor.vst3"), "OSCar Editor.vst3"),
    (os.path.join(src, "OSCar Editor.app"), "OSCar Editor.app"),
]
symlinks = {
    "Components": "/Library/Audio/Plug-Ins/Components",
    "VST3": "/Library/Audio/Plug-Ins/VST3",
    "Applications": "/Applications",
}
icon_locations = {
    "OSCar Editor.component": (170, 128),
    "Components": (470, 128),
    "OSCar Editor.vst3": (170, 222),
    "VST3": (470, 222),
    "OSCar Editor.app": (170, 316),
    "Applications": (470, 316),
}
background = os.path.join(work, "background.png")   # background@2x.png next to it is used on Retina screens
window_rect = ((200, 140), (640, 428))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 56
text_size = 12
arrange_by = None
