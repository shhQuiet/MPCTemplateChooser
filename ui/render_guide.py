"""Render the instructions behind the MPC's native Enable control."""

from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


def render(path):
    image = Image.new("RGB", (1280, 628), "#1e1d1e")
    draw = ImageDraw.Draw(image)
    heading = ImageFont.load_default(size=38)
    body = ImageFont.load_default(size=27)

    x = 38
    draw.line((x, 151, 1195, 151), fill="#535256", width=1)
    draw.text((x, 186), "Start new projects from your own templates", font=heading, fill="#e7f4f5")

    instructions = [
        (256, 'Save an MPC project (.xpj) in a folder named "Templates" at the top level'),
        (291, "of any drive. You can organize projects in subfolders."),
        (353, "Turn on Enable and restart MPC. The startup script replaces Demos"),
        (388, "with Local templates on the New Project screen. Pick a project to open it."),
        (450, "Turn off Enable to restore Demos on the next launch."),
        (510, "Once enabled, you can remove the Startup plugin. The startup script"),
        (545, "keeps working. Reinstall the plugin to turn the feature on or off."),
    ]
    for y, line in instructions:
        assert draw.textlength(line, font=body) < 1195 - x
        draw.text((x, y), line, font=body, fill="#d9d8da")
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


if __name__ == "__main__":
    render(Path(sys.argv[1]))
