# MPC Template Chooser 1.0.0 release testing

Testing took place on an original MPC X running MPC OS 3.10.1.12 on November 20, 2026.

## Installer and startup

The packaged installer installed 0.1.5 on the SSD, upgraded it to 1.0.0, uninstalled it, and reinstalled 1.0.0. Each install registered a single plugin entry and the installed binary matched its package checksum. Uninstall removed both the plugin folder and its registration. The startup hook kept its existing configuration throughout the installer cycle.

After restart the MPC loaded the packaged 1.0.0 binary from the SSD and opened the plugin and its info guide. The installer warned that the SSD was mounted with `noexec`. The running MPC still mapped the plugin binary from the SSD. The startup helper populated **Local templates**, and the MPC X owner confirmed that selecting a tile opened the project as a template.

## CPU benchmark

Open MPC's benchmark ran twice against the published ARMv7 binary using 128-frame blocks at 44.1 kHz. An isolated content directory kept the benchmark from changing the startup hook.

| Run | Worst-stage p99 | Worst block | Verdict |
| --- | ---: | ---: | --- |
| 1 | 0.1% | 0.7% | PASS |
| 2 | 0.1% | 1.2% | PASS |

Percentages are shares of the audio block budget. The MPC app stayed running throughout.

## Offline checks

The SDK package validator, 20 host simulator checks, startup-helper fixtures, and the Open MPC catalog package validator passed. The catalog validator flagged `posix_spawn` for maintainer review. The plugin uses it to run the bundled startup helper.

The ARMv7 release ZIP has SHA-256 `1441540ea9019dc9f2ab8799ec850ea3fffcdcf23e89767296dbbdac508b7115`.
