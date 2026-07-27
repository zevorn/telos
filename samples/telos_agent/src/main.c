#include <stdio.h>

#include <telos/zephyr.h>

int main(void)
{
#if defined(CONFIG_TELOS_NETWORK)
    char network_address[64];
#endif

    if (telos_zephyr_initialize() != 0) {
        return 1;
    }
    printf("Telos Agentic Framework: %s\n", telos_zephyr_status());
    if (!telos_zephyr_run_static_scenario()) {
        printf("TELOS_SCENARIO: FAILED\n");
        return 1;
    }
    printf("TELOS_SCENARIO: PASSED\n");
    printf("%s\n", telos_zephyr_trace());
#if defined(CONFIG_TELOS_NETWORK)
    if (
        !telos_zephyr_network_wait(
            10000,
            network_address,
            sizeof(network_address)
        )
    ) {
        printf("TELOS_NETWORK: FAILED\n");
        return 1;
    }
    printf("TELOS_NETWORK: READY %s\n", network_address);
#endif
    return 0;
}
