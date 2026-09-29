# Contributing

Issues and pull requests are welcome. Keep source comments, commit messages, and documentation in English.

For a hardware report, include the keyboard PCB/controller revision, SuperMini revision, bootloader/SoftDevice versions, operating system, connection interval, and a minimal reproduction. Distinguish physical observations from host-side simulations. Do not post bond keys, complete device dumps, personal typing logs, or an original vendor firmware image.

Keep transport, state-machine, and persistence responsibilities separate. Prefer shared logic over duplicate implementations. Do not modify installed PlatformIO packages to make a build work; add a reproducible project-local patch or configuration instead.

Run `python tools/run_tests.py` for behavior changes, then build and verify both firmware artifacts as documented in the README. Hardware-facing changes also need relevant device checks; describe untested cases in the pull request. Update wiring/protocol documentation when changing pin assignments or message formats.

The default application targets the documented LPC11U13/SuperMini assembly. For another PCB, provide verified GPIO and power information rather than assuming compatibility from a product name.
