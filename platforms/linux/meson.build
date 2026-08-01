linux_platform_inc = include_directories('include')
linux_platform_includes = [inc_public, linux_platform_inc]

if host_machine.cpu_family() == 'x86_64'
    linux_platform_architecture = 'TELOS_LINUX_ARCHITECTURE_X86_64'
    linux_platform_target = 'linux-x86_64'
elif host_machine.cpu_family() == 'aarch64'
    linux_platform_architecture = 'TELOS_LINUX_ARCHITECTURE_AARCH64'
    linux_platform_target = 'linux-aarch64'
elif host_machine.cpu_family() == 'riscv64'
    linux_platform_architecture = 'TELOS_LINUX_ARCHITECTURE_RISCV64'
    linux_platform_target = 'linux-riscv64'
else
    error('Telos does not support this Linux architecture')
endif

linux_platform_c_args = [
    '-DTELOS_LINUX_ARCHITECTURE=' + linux_platform_architecture,
    '-DTELOS_LINUX_TARGET="' + linux_platform_target + '"',
]

telos_linux_platform = static_library(
    'telos-linux-platform',
    'src/platform.c',
    c_args: linux_platform_c_args,
    include_directories: linux_platform_includes,
)

telos_linux_platform_dep = declare_dependency(
    include_directories: linux_platform_includes,
    link_with: telos_linux_platform,
)

install_headers(
    'include/telos/linux.h',
    subdir: 'telos',
)
