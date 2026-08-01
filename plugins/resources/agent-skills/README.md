# Agent Skills Resource Source

`dev.zevorn.agent-skills` discovers OpenAI-compatible Agent Skills from
configured filesystem roots, validates `SKILL.md` metadata, publishes
immutable Resource Generations, and resolves paths without allowing a Skill
to escape its package directory.

This is a Linux Context Source Plugin because it depends on POSIX directory
and filesystem APIs. It is intentionally absent from minimal Zephyr builds;
firmware should use a separate Context Source Plugin backed by generated,
read-only tables.

The Plugin requires `filesystem.read`. Skill instructions and auxiliary files
are data and cannot grant capabilities that Core Policy did not provide.
