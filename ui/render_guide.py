"""Render the settings row and help panel, then bind them to the MPC skin."""

import json
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


SCREEN = (1280, 628)
HELP_HANDLE = "Local/2/templateHelp"


def render_images(skins):
    body = ImageFont.load_default(size=27)
    heading = ImageFont.load_default(size=38)

    settings = Image.new("RGB", SCREEN, "#111115")
    draw = ImageDraw.Draw(settings)
    draw.text((54, 85), "Enable templates", font=body, fill="#e2e3ea", anchor="lm")
    draw.line((40, 137, 1240, 137), fill="#37373d", width=1)
    settings.save(skins / "chooser-settings.png")

    for checked in (False, True):
        box = Image.new("RGBA", (56, 56))
        draw = ImageDraw.Draw(box)
        draw.rounded_rectangle((4, 4, 51, 51), radius=6, outline="#ccd0da", width=2)
        if checked:
            draw.line(((14, 28), (24, 38), (42, 17)), fill="#dd2850", width=7, joint="curve")
        box.save(skins / ("checkbox-on.png" if checked else "checkbox-off.png"))

    info = Image.new("RGBA", (56, 56))
    draw = ImageDraw.Draw(info)
    draw.ellipse((6, 6, 49, 49), fill="#f1f2f6")
    draw.ellipse((25, 14, 31, 20), fill="#121218")
    draw.rounded_rectangle((25, 24, 31, 40), radius=2, fill="#121218")
    info.save(skins / "info.png")

    ok = Image.new("RGBA", (148, 52))
    draw = ImageDraw.Draw(ok)
    draw.rounded_rectangle((0, 0, 147, 51), radius=5, fill="#ccd0df")
    draw.text((74, 26), "OK", font=ImageFont.load_default(size=24), fill="#16171d", anchor="mm")
    ok.save(skins / "info-ok.png")

    guide = Image.new("RGB", SCREEN, "#09090c")
    draw = ImageDraw.Draw(guide)
    draw.rounded_rectangle((100, 40, 1180, 590), radius=12, fill="#202027", outline="#55565f", width=1)
    draw.text((146, 78), "Start new projects from your own templates", font=heading, fill="#f1f2f6")
    lines = [
        (153, 'Save an MPC project (.xpj) in a folder named "Templates" at the top level'),
        (188, "of any drive. You can organize projects in subfolders."),
        (246, "Turn on Enable templates and restart MPC. The startup script replaces"),
        (281, "Demos with Local templates on the New Project screen. Pick a project to open it."),
        (339, "Turn off Enable templates to restore Demos on the next launch."),
        (398, "Once enabled, you can remove Template Chooser. The startup script"),
        (433, "keeps working. Reinstall the plugin to turn the feature on or off."),
    ]
    for y, line in lines:
        assert draw.textlength(line, font=body) < 980
        draw.text((146, y), line, font=body, fill="#d9dae2")
    guide.save(skins / "chooser-guide.png")


def get_field(obj, key):
    return next(value for name, value in obj if name == key)


def set_field(obj, key, value):
    index = next(i for i, (name, _) in enumerate(obj) if name == key)
    obj[index] = (key, value)


def bind_skin(skins, sdk):
    sys.path.insert(0, str(sdk / "tools"))
    import gen_ui as ui

    def visibility(show_help):
        return [f"IndexedEnabling/{int(show_help)}/2/{HELP_HANDLE}"]

    def help_button(name, image, bounds, show_help=None):
        button = ui.button_data(image, image, 1, 1)
        set_field(button, "handleName", HELP_HANDLE)
        return ui.entry(name, "Button", button, bounds, "No", "Hide", "Always",
                        invalidating=visibility(show_help) if show_help is not None else None)

    path = skins / "TUI.json"
    definition = json.loads(path.read_text(), object_pairs_hook=ui.Obj)
    page = get_field(definition, "pageData")
    tabs = get_field(page, "tabs")
    tab, = tabs
    local = get_field(get_field(page, "componentDefinitions"), "localComponentDefinitions")
    toggle = next(item for item in local if get_field(item, "key") == "toggle")

    checkbox = ui.entry(
        "checkbox", "Button", ui.button_data("checkbox-on.png", "checkbox-off.png", 1, 1),
        "814 20 56 56", "No", "Show", "Always",
    )
    toggle_actions = [
        ui.action("Mouse Down", "Q-Link", "Data"),
        ui.action("Enter Pressed", "Toggle Switch", "Data"),
    ]
    set_field(toggle, "value", ui.component_definition(toggle_actions, [checkbox], "00000000"))
    components = [
        ui.entry("Enable templates", "toggle", ui.obj(("version", 1), ("handleName", "Data")),
                 "36 37 880 96", "Yes", "Hide", "Always",
                 remap=[("Data", "Parameter 1")], invalidating=visibility(False)),
        help_button("Info", "info.png", "1050 57 56 56", show_help=False),
        ui.entry("Instructions", "template help", ui.obj(("version", 1), ("handleName", "Data")),
                 "0 0 1280 628", "No", "Hide", "Always",
                 remap=[("Data", HELP_HANDLE)], invalidating=visibility(True)),
    ]
    guide = ui.component_definition(
        [], [help_button("OK", "info-ok.png", "566 505 148 52")], "ff09090c",
        bg_image="chooser-guide.png",
    )
    local.append(ui.obj(("key", "template help"), ("value", guide)))
    set_field(tab, "componentDefinition",
              ui.component_definition([], components, "ff111115", bg_image="chooser-settings.png"))
    set_field(tab, "qlinkBoundsData", ["26 27 900 116"])
    path.write_text(ui.dump_json(definition) + "\n", encoding="utf-8")

    # The base skin's pill is replaced by the checkbox.
    for image in ("toggle_on.png", "toggle_off.png"):
        (skins / image).unlink(missing_ok=True)


if __name__ == "__main__":
    sdk, project = map(Path, sys.argv[1:])
    manifest = json.loads((project / "plugin.json").read_text())
    folder = f'{manifest["manufacturer"]} - VST - {manifest["name"]}'
    skins = project / "build" / folder / "Plugin Skins"
    render_images(skins)
    bind_skin(skins, sdk)
