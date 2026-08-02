# POSIX Agent Tools Plugin

This official Plugin supplies the bounded local tools used by the terminal
Agent: `read`, `write`, `edit`, and `bash`. It keeps filesystem and process
behavior outside Core while the Core Tool and Capability interfaces enforce
schema validation, cancellation, and policy decisions.

Paths are resolved below the configured working directory. Reads and writes
reject paths that escape that directory, follow no final symlink, and enforce
a one MiB file limit. Shell output is capped at 256 KiB and is returned with
the child's exit code so a non-zero command can be corrected by the Agent.

The Plugin requires `filesystem.read`, `filesystem.write`, and
`process.spawn`. Applications should grant only the capabilities appropriate
for their trust policy.
