# MPC Template Chooser

MPC Template Chooser adds a **Local templates** row to the standalone MPC New Project dialog. It looks for `.xpj` projects under folders named `Templates` or `templates` on the currently mounted internal, SD, USB, and SATA drives. It searches nested folders, skips hidden files and symlinks, and lists templates with the same name separately.

## MPC X setup

The package targets the original MPC X running MPC OS 3.10.1 or later. Enable **External Plugin Support** under **Preferences → Enhancements**. Extract the **Elsewave - VST - MPC Template Chooser** folder into the `VST2 Plugins` directory on the internal plugin drive. The helper needs a drive that permits execution. Load **MPC Template Chooser** into an effect insert and open its **CHOOSER** page. The effect passes audio through unchanged.

- **Refresh → Apply** scans the mounted drives and replaces the Demos row. If there are no templates, it restores Demos. Save your work before relaunching MPC to view the new row.
- **Enable → Apply** also installs the startup refresh for future launches. It uses the Open MPC startup-configuration reload already present on the target MPC X. If that reload is absent, Enable reports **Startup reload missing**. Manual Refresh works independently.
- **Restore → Apply** reinstates Demos and removes the startup hook. Run it before removing the plugin folder.

The refresh backs up the existing dialog and updates only the Demos row. It generates project entries with paths to the original files. A full first scan can take several minutes on a drive with a large directory tree.

At startup, the helper rescans the `Templates` folders it found previously. This updates projects added to those folders and drops projects on missing drives. This step has a 10-second limit, including time for the drives to mount. After MPC launches, a low-priority scan discovers new folders and updates the list for the next launch. Use **Refresh** to discover new folders or drives on demand.

Both builds pass SDK validation. On an MPC X running 3.10.1.12, the read-only scanner found eight projects across the mounted drives and the ARM plugin dispatched each of its three actions to a temporary test helper. Loading a generated tile from the SSD and running the hook through an actual MPC reboot still require an in-app test.

## Build

Use the [Akai MPC Plugin Builder SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk), Zig 0.15.2, Python 3, and Pillow. Set `MPC_SDK` to the SDK checkout and run `./build.sh`. Set `ZIG` or `PYTHON` to override the tools on your path. The script fetches the pinned [nlohmann/json](https://github.com/nlohmann/json) header (MIT) and packages the 32-bit and 64-bit ARM builds in `dist/`. The startup configuration described here is specific to the target MPC X.

The refresh helper can be tested against a disposable layout.

```sh
c++ -std=c++17 -DLOCAL_TEMPLATES_TEST -Ibuild/deps helper/Refresh.cpp -o build/test-refresh
python3 tests/refresh_test.py build/test-refresh
```

The MPC logs the helper's outcome in the `acvs.service` journal. The original dialog is saved at `/data/local-templates/original-dialog.json`. For recovery over SSH, remove `/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf` to disable the hook, then restore the saved dialog to `/sdcard/Demos/NewProjectDialog.json` if needed.
