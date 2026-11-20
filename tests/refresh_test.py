#!/usr/bin/env python3
"""Exercise the scanner and startup lifecycle with disposable MPC-style storage."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def write(root, name, contents=b""):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)
    return path


def run(helper, command, root, code=0):
    result = subprocess.run(
        [helper, command, root], capture_output=True, text=True, check=False
    )
    assert result.returncode == code, result
    return result


def main():
    helper = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        rows = [
            {"type": 0, "rowTitle": "PROJECT TEMPLATES", "items": [{"name": "Empty"}]},
            {"type": 1, "rowTitle": "DEMOS", "items": [{"name": "Pop"}], "forgets": True},
            {"type": 2, "rowTitle": "RECENT", "items": []},
        ]
        factory = {"value0": {"rows": rows, "rowSpacing": 10}}
        original = json.dumps(factory).encode() + b"\0\n"
        dialog = write(root, "sdcard/Demos/NewProjectDialog.json", original)
        dialog.chmod(0o644)
        write(root, "proc/self/mountinfo", b"""18 1 179:6 / / ro - ext4 /dev/root ro
35 32 179:6 /storage /media/acvs-content ro - ext4 /dev/root ro
37 18 179:7 / /data rw - ext4 /dev/mmcblk0p7 rw
39 32 179:7 / /media/az01-internal rw - ext4 /dev/mmcblk0p7 rw
77 18 179:97 / /sdcard rw - ext4 /dev/mmcblk1p1 rw
79 32 179:97 / /media/az01-internal-sd rw - ext4 /dev/mmcblk1p1 rw
93 32 8:2 / /media/My\\040SSD rw - exfat /dev/sda2 rw
94 32 8:3 / /media/USB rw - vfat /dev/sdb1 rw
""")
        write(root, "sdcard/Templates/Stereo.xpj")
        write(root, "sdcard/Templates/.hidden.xpj")
        write(root, "sdcard/Templates/.hidden/Hidden.xpj")
        write(root, "sdcard/Templates/Stereo_[ProjectData]/Kick.wav", b"sample")
        write(root, "sdcard/Projects/Not a template.xpj")
        write(root, "sdcard/Other/TEMPLATES/Uppercase folder.xpj")
        write(root, "media/az01-internal-sd/Templates/Alias.xpj")
        write(root, "media/acvs-content/Templates/Firmware.xpj")
        write(root, "media/az01-internal/Projects/templates/Internal.xpj")
        write(root, 'media/My SSD/Session/Templates/nested/Quote " and café.XPJ')
        write(root, "media/My SSD/Session/Templates/nested/Stereo.xpj")
        write(root, "media/USB/Projects/templates/USB Check.xpj")
        os.symlink(root / "sdcard", root / "media/USB/loop")
        hook_folder = root / "data/system/etc/overlay/systemd/system/acvs.service.d"
        hook = hook_folder / "60-local-templates.conf"

        run(helper, "enable", root, 4)
        assert dialog.read_bytes() == original and not hook.exists()

        run(helper, "refresh", root)
        updated = dialog.read_bytes()
        assert updated.endswith(b"\0\n")
        assert dialog.stat().st_mode & 0o777 == 0o644
        result = json.loads(updated.split(b"\0")[0])["value0"]
        updated_rows = result["rows"]
        assert updated_rows[0] == rows[0] and updated_rows[2] == rows[2]
        assert result["rowSpacing"] == 10
        local = updated_rows[1]
        assert local["rowTitle"] == "Local templates" and local["forgets"]
        paths = {entry["fileName"] for entry in local["items"]}
        assert paths == {
            "/sdcard/Templates/Stereo.xpj",
            "/media/az01-internal/Projects/templates/Internal.xpj",
            '/media/My SSD/Session/Templates/nested/Quote " and café.XPJ',
            "/media/My SSD/Session/Templates/nested/Stereo.xpj",
            "/media/USB/Projects/templates/USB Check.xpj",
        }, paths
        assert len({entry["name"] for entry in local["items"]}) == 5
        run(helper, "refresh", root)
        assert dialog.read_bytes() == updated

        write(root, "usr/lib/systemd/system/acvs.service.wants/mpc-addins-reload.service")
        write(root, str(hook_folder.relative_to(root) / "90-mpc-addins.conf"))
        run(helper, "enable", root)
        assert "ExecStartPre=-/data/local-templates/template-refresh boot" in hook.read_text()
        assert "ExecStartPost=-/usr/bin/systemd-run" in hook.read_text()
        assert os.access(root / "data/local-templates/template-refresh", os.X_OK)
        write(root, "sdcard/Templates/Added to known folder.xpj")
        write(root, "sdcard/Fresh/Templates/Added to new folder.xpj")
        run(helper, "boot", root)
        assert b"Added to known folder" in dialog.read_bytes()
        assert b"Added to new folder" not in dialog.read_bytes()
        run(helper, "discover", root)
        assert b"Added to new folder" in dialog.read_bytes()
        run(helper, "restore", root)
        assert not hook.exists() and dialog.read_bytes() == original
        run(helper, "discover", root)
        assert dialog.read_bytes() == original

        mountinfo = root / "proc/self/mountinfo"
        mountinfo.write_text("18 1 179:6 / / ro - ext4 /dev/root ro\n")
        run(helper, "refresh", root, 3)
        assert dialog.read_bytes() == original

        broken = b'{"unexpected": true}'
        dialog.write_bytes(broken)
        run(helper, "refresh", root, 1)
        assert dialog.read_bytes() == broken

    print("Refresh, discovery, validation, startup installation and restore passed.")


if __name__ == "__main__":
    main()
