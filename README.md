# MPC Template Chooser

MPC Template Chooser adds a **Local templates** row to the standalone MPC New Project dialog. It looks for `Templates` or `templates` directly at the root of each mounted internal, SD, USB, and SATA drive. It searches those folders and all their subfolders for `.xpj` projects, skips hidden files and symlinks, and lists templates with the same name separately.

## MPC X setup

The package targets the original MPC X running MPC OS 3.10.1 or later. Enable **External Plugin Support** under **Preferences → Enhancements**. Extract the **Elsewave - VST - MPC Template Chooser** folder into the `VST2 Plugins` directory on your plugin drive. Load **MPC Template Chooser** into an effect insert and open its **CHOOSER** page. The effect passes audio through unchanged.

- **Enable on** installs a startup hook. It uses the Open MPC startup-configuration reload already present on the target MPC X.
- **Enable off** removes the hook and restores the original Demos row.

Once enabled, you can remove Template Chooser and the startup hook keeps working. Reinstall the plugin to turn the feature on or off.

The startup helper finds projects and builds the list before MPC opens. It backs up the existing dialog and replaces the Demos row with **Local templates** when projects are available. Its entries point to the original `.xpj` project files.

The startup scan has a 10-second limit, including time for the drives to mount. Restart MPC after connecting a drive to include its templates in the startup dialog.

Both builds pass SDK validation. Loading a generated tile from the SSD and running the hook through an actual MPC reboot still require an in-app test.

## Build

Use the [Akai MPC Plugin Builder SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk), Zig 0.15.2, Python 3, and Pillow. Set `MPC_SDK` to the SDK checkout and run `./build.sh`. Set `ZIG` or `PYTHON` to override the tools on your path. The script fetches the pinned [nlohmann/json](https://github.com/nlohmann/json) header (MIT) and packages the 32-bit and 64-bit ARM builds in `dist/`. The startup configuration described here is specific to the target MPC X.

The refresh helper can be tested against a disposable layout.

```sh
c++ -std=c++17 -DLOCAL_TEMPLATES_TEST -Ibuild/deps helper/Refresh.cpp -o build/test-refresh
python3 tests/refresh_test.py build/test-refresh
```

The MPC logs the helper's outcome in the `acvs.service` journal. The original dialog is saved at `/data/local-templates/original-dialog.json`. For recovery over SSH, remove `/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf` to disable the hook, then restore the saved dialog to `/sdcard/Demos/NewProjectDialog.json` if needed.
