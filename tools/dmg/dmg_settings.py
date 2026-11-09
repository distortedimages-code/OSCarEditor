# dmgbuild settings for OSCar-Editor-mac.dmg (see make_dmg.sh).
# The window shows the three bundles, a shortcut to the folder each one goes in, and
# Installer.command. Positions match make_background.py.
import os

src = defines["src"]              # folder holding the three bundles
work = defines["work"]            # Installer.command and the background, prepared by make_dmg.sh

format = "UDZO"
filesystem = "HFS+"
files = [
    (os.path.join(src, "OSCar Editor.component"), "OSCar Editor.component"),
    (os.path.join(src, "OSCar Editor.vst3"), "OSCar Editor.vst3"),
    (os.path.join(src, "OSCar Editor.app"), "OSCar Editor.app"),
    (os.path.join(work, "Installer.command"), "Installer.command"),
]
symlinks = {
    "Components (AU)": "/Library/Audio/Plug-Ins/Components",
    "VST3": "/Library/Audio/Plug-Ins/VST3",
    "Applications": "/Applications",
}
icon_locations = {
    "OSCar Editor.component": (196, 166),
    "Components (AU)": (524, 166),
    "OSCar Editor.vst3": (196, 264),
    "VST3": (524, 264),
    "OSCar Editor.app": (196, 362),
    "Applications": (524, 362),
    "Installer.command": (118, 470),
}
background = os.path.join(work, "background.png")   # background@2x.png next to it is used on Retina screens
window_rect = ((160, 120), (720, 562))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 64
text_size = 12
arrange_by = None
