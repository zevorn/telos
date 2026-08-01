# Source checks

Run the style checks on changed C headers and sources:

```sh
scripts/check-style.sh
```

Use `scripts/check-style.sh --all` for the repository-wide baseline. The
wrapper combines QEMU's `checkpatch.pl` checks with the Telos function layout
rule: the first parameter follows the opening parenthesis, subsequent lines
align to it, and the closing parenthesis stays with the last parameter. Macro
continuation backslashes use spaces and align to one column.

Telos is not linked against QEMU, so the wrapper suppresses only
`checkpatch.pl` suggestions to replace standard `strto*` functions with
QEMU-local `qemu_strto*` helpers. All other diagnostics remain errors.

`checkpatch.pl` is copied unchanged from QEMU commit
`64ce9ac18757d79f3b5b337f7bcbdd0dabef3ce1`. It retains its upstream GPLv2
license and copyright notices and is used only as a development tool; it is
not compiled or linked into Telos.
