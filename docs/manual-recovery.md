# Manual recovery for MPC Template Chooser

Use these steps if you cannot open Template Chooser to turn off **Enable templates**. For normal removal, follow [Uninstall and restore Demos](https://github.com/shhQuiet/MPCTemplateChooser/releases/tag/gen2-test-v1.0.1#uninstall-and-restore-demos).

If the MPC interface is unavailable and you already have SSH with system access, run these commands on the MPC:

```sh
/data/local-templates/template-refresh restore && systemctl daemon-reload
```

If the command succeeds, delete the **Elsewave - VST - MPC Template Chooser** folder from **VST2 Plugins** on your plugin drive and restart. If the helper fails and the following hook file exists, you can move just that file aside to stop it from running at startup:

```sh
hook=/data/system/etc/overlay/systemd/system/acvs.service.d/60-local-templates.conf
mv -n "$hook" "$hook.disabled" && [ ! -e "$hook" ] && systemctl daemon-reload
```

Once the fallback command succeeds, restart the MPC. Keep the dialog backup in `/data/local-templates/` and include the restore error in a [GitHub issue](https://github.com/shhQuiet/MPCTemplateChooser/issues) so we can help restore Demos.
