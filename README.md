# MPC Template Chooser

MPC Template Chooser adds a **Local templates** row to the standalone MPC New Project dialog. It looks for `Templates` or `templates` directly at the root of each mounted internal, SD, USB, and SATA drive. It searches those folders and all their subfolders for `.xpj` projects, skips hidden files and symlinks, and lists templates with the same name separately.

## MPC X setup

The package targets the original MPC X with MPC OS 3.10.1 or later and the Open MPC boot-reload service installed. Enable **External Plugin Support** under **Preferences → Enhancements**. Extract the **Elsewave - VST - MPC Template Chooser** folder into the `VST2 Plugins` directory on your plugin drive. Load **MPC Template Chooser** into an effect insert and open its **CHOOSER** page. The effect passes audio through unchanged.

- Checking **Enable templates** installs a startup hook. Restart the MPC to let the hook build the row.
- Unchecking **Enable templates** removes the hook and restores the original Demos row.
- Tap **ⓘ** for setup instructions and **OK** to close them.

Once enabled, you can remove Template Chooser and the startup hook keeps working. Reinstall the plugin to turn the feature on or off.

The startup helper finds projects and builds the list before MPC opens. It backs up the existing dialog and replaces the Demos row with **Local templates** when projects are available. Its entries point to the original `.xpj` project files.

The startup scan has a 10-second limit, including time for the drives to mount. Restart MPC after connecting a drive to include its templates in the startup dialog.

The controls, startup scan, and opening a project from **Local templates** were tested on the original MPC X running MPC OS 3.10.1.12. The startup layout is unverified on 64-bit devices.

The Open MPC catalog release includes the ARMv7 build for the original MPC X. The AArch64 build is available from source and awaits Gen2 hardware verification.

**Gen 2 testers wanted.** Download the [experimental Gen 2 build](https://github.com/shhQuiet/MPCTemplateChooser/releases/tag/gen2-test-v1.0.1). Please report your MPC model and firmware, whether Local templates appears after restart, and whether disabling the hook restores Demos.

On supported firmware and mounts, the shared installer used by current [Open MPC add-ins](https://github.com/open-mpc/mpc-vst-plugins/blob/main/docs/ADDINS.md) installs the boot-reload service. For example, a current MPC Remote add-in install on a supported MPC can supply it.

## Build

Use the [Akai MPC Plugin Builder SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk), Zig 0.15.2, Python 3, and Pillow. Set `MPC_SDK` to the SDK checkout and run `./build.sh`. Set `ZIG` or `PYTHON` to override the tools on your path. The script fetches the pinned [nlohmann/json](https://github.com/nlohmann/json) header (MIT) and packages the 32-bit and 64-bit ARM builds in `dist/`. The startup configuration described here is specific to the target MPC X.

The refresh helper can be tested against a disposable layout.

```sh
c++ -std=c++17 -DLOCAL_TEMPLATES_TEST -Ibuild/deps helper/Refresh.cpp -o build/test-refresh
python3 tests/refresh_test.py build/test-refresh
```

The VST starts its bundled helper when the checkbox changes. The helper writes outside the plugin folder. It configures the startup hook at `/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf`, backs up the original dialog to `/data/local-templates/original-dialog.json`, and updates `/sdcard/Demos/NewProjectDialog.json` when the hook runs. The outcome appears in the `acvs.service` journal.

Turn off **Enable templates** before uninstalling if you want to restore Demos. If the MPC UI is unavailable and SSH is already enabled, run `/data/local-templates/template-refresh restore` and `systemctl daemon-reload`. This removes the hook and restores the saved dialog.

## License

The plugin and startup helper are MIT licensed. Packaged builds include the license notices for the Akai MPC Plugin Builder SDK and nlohmann/json.
