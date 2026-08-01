# curl Transport

The curl Transport is the official Linux HTTP transport for Telos Provider
Plugins. It streams response bytes directly to the Provider callback and
supports cooperative cancellation without buffering the complete response.

HTTPS URLs and loopback HTTP URLs are accepted. Loopback HTTP is limited to
`localhost`, `127.0.0.1`, and `[::1]`; other cleartext destinations are
rejected and loopback hosts bypass configured proxies. TLS certificate and
host verification remain enabled through libcurl defaults. Redirects are
disabled so a Provider request cannot silently cross a trust boundary.

The transport is optional at build time because Zephyr supplies networking
through its own platform integration. Configure it explicitly with
`-Dcurl_transport=enabled` when building the Linux interactive agent.
