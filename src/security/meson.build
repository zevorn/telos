telos_security = static_library(
    'telos-security',
    'secret.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_security_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_security,
)
