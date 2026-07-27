#include <stdio.h>

#include <telos/zephyr.h>

int main(void)
{
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
    return 0;
}
