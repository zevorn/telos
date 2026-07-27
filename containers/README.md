# Telos build images

- `builder-c` is the reproducible Meson/Ninja C Plugin builder and contains
  the installed Telos Plugin SDK.
- `dev` adds interactive editor, debugger, and language-server tools.
- `plugin-runtime` is the compiler-free process Plugin Host image.
- `builder-zephyr` deliberately requires an explicit, pinned
  `ZEPHYR_BASE_IMAGE` build argument so the Zephyr and SDK versions are never
  selected implicitly.

The installer runs builders without network, host Home, SSH Agent, cloud
credentials, or a container socket. Dependency resolution happens before the
builder starts.
