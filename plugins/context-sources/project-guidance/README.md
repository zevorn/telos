# Project Guidance Context Source

`dev.zevorn.project-guidance` discovers user and project `AGENTS.md` files,
applies directory precedence, and returns their content for the trusted Prompt
slots enforced by Core.

Filesystem discovery is a POSIX host Context Source Plugin rather than a Core
runtime dependency. Minimal Zephyr images can replace it with a generated
read-only context source without bringing in POSIX filesystem APIs.

The Plugin requires `filesystem.read`; it does not change Prompt trust or
capability decisions.
