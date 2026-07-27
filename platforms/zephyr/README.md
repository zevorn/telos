# Telos Zephyr adapter

Telos uses Meson and Ninja for its host runtime and Plugin SDK. Zephyr's
upstream build owns Kconfig, Devicetree, board selection, toolchain setup, and
the final firmware link, so this directory intentionally contains only a thin
module adapter.

Build the smoke sample from a Zephyr workspace:

```sh
west build -b native_sim/native/64 samples/telos_agent \
  -- -DZEPHYR_EXTRA_MODULES=/path/to/telos/platforms/zephyr

west build -b qemu_cortex_a53 samples/telos_agent \
  -- -DZEPHYR_EXTRA_MODULES=/path/to/telos/platforms/zephyr
```

The ARM Virt target uses Zephyr's `qemu_cortex_a53` board and its emulated
E1000 Ethernet device. Use `tools/run_zephyr_native.sh` and
`tools/run_zephyr_qemu.sh` to validate the complete static Tool scenario.
The QEMU runner uses user networking and does not require a host TAP device.
