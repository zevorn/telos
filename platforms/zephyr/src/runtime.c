#include <zephyr/kernel.h>

#if defined(CONFIG_TELOS_NETWORK) && defined(CONFIG_NET_SOCKETS)
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#endif

#include <telos/event.h>
#include <telos/session.h>
#include <telos/value.h>
#include <telos/zephyr.h>

static bool initialized;
static const char *last_trace = "trace: no events";

int telos_zephyr_initialize(void)
{
    initialized = true;
    return 0;
}

const char *telos_zephyr_status(void)
{
    return initialized ? "ready" : "not-initialized";
}

static bool apply_event(
    struct telos_session_machine *machine,
    uint64_t sequence,
    const char *type,
    const struct telos_value *payload
)
{
    struct telos_id id = telos_id_generate();
    struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = id,
        .session_id = {.high = 1, .low = 1},
        .correlation_id = {.high = 1, .low = 2},
        .causation_id = {.high = 1, .low = sequence - 1},
        .type = type,
        .source = "static:dev.zevorn.echo",
        .timestamp_milliseconds = (int64_t)sequence,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);
    bool result = event != NULL
        && telos_session_machine_apply(machine, event, NULL);

    telos_event_release(event);
    return result;
}

bool telos_zephyr_run_static_scenario(void)
{
    static const char *events[] = {
        "turn.accepted",
        "input.prepare",
        "context.build",
        "provider.dispatch",
        "response.received",
        "tool.authorize",
    };
    struct telos_session_machine *machine;
    struct telos_value *empty;
    struct telos_value *tool_count;
    struct telos_value *echo_input;
    struct telos_value *echo_output;
    uint64_t sequence = 1;
    bool result = initialized;

    machine = telos_session_machine_create(NULL);
    empty = telos_value_new_null();
    tool_count = telos_value_new_integer(1);
    echo_input = telos_value_new_string("zephyr");
    echo_output = telos_value_retain(echo_input);
    result = result
        && machine != NULL
        && empty != NULL
        && tool_count != NULL
        && echo_output != NULL;
    for (
        size_t index = 0;
        result && index < sizeof(events) / sizeof(events[0]);
        ++index
    ) {
        result = apply_event(machine, sequence++, events[index], empty);
    }
    result = result
        && apply_event(machine, sequence++, "tool.execute", tool_count)
        && apply_event(machine, sequence++, "tool.completed", echo_output)
        && apply_event(machine, sequence++, "context.build", empty)
        && apply_event(machine, sequence++, "provider.dispatch", empty)
        && apply_event(machine, sequence++, "response.received", empty)
        && apply_event(machine, sequence++, "final.commit", empty)
        && apply_event(machine, sequence++, "final.committed", empty)
        && telos_session_machine_state(machine) == TELOS_SESSION_COMPLETED;
    if (result) {
        last_trace = "trace: static echo output=zephyr state=COMPLETED";
    }
    telos_value_release(echo_output);
    telos_value_release(echo_input);
    telos_value_release(tool_count);
    telos_value_release(empty);
    telos_session_machine_destroy(machine);
    return result;
}

const char *telos_zephyr_trace(void)
{
    return last_trace;
}

#if defined(CONFIG_TELOS_NETWORK) && defined(CONFIG_NET_SOCKETS)

struct network_address {
    char *buffer;
    size_t size;
    bool found;
};

static void find_ipv4_address(
    struct net_if *interface,
    struct net_if_addr *interface_address,
    void *context
)
{
    struct network_address *address = context;

    (void)interface;
    if (
        !address->found
        && net_addr_ntop(
            NET_AF_INET,
            &interface_address->address.in_addr,
            address->buffer,
            address->size
        ) != NULL
    ) {
        address->found = true;
    }
}

static bool dns_probe(uint32_t timeout_milliseconds)
{
    static const uint8_t query[] = {
        0x54, 0x45, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        0x00,
        0x00, 0x01, 0x00, 0x01,
    };
    uint8_t response[512];
    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
    };
    struct zsock_pollfd poll_descriptor = {
        .events = ZSOCK_POLLIN,
    };
    int socket;
    int received;
    bool result = false;

    socket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (
        socket < 0
        || zsock_inet_pton(
            AF_INET,
            CONFIG_TELOS_NETWORK_PROBE_IPV4,
            &server.sin_addr
        ) != 1
    ) {
        if (socket >= 0) {
            zsock_close(socket);
        }
        return false;
    }
    poll_descriptor.fd = socket;
    if (
        zsock_sendto(
            socket,
            query,
            sizeof(query),
            0,
            (const struct sockaddr *)&server,
            sizeof(server)
        ) != (ssize_t)sizeof(query)
        || zsock_poll(
            &poll_descriptor,
            1,
            (int)timeout_milliseconds
        ) <= 0
    ) {
        zsock_close(socket);
        return false;
    }
    received = zsock_recv(socket, response, sizeof(response), 0);
    if (
        received >= 12
        && response[0] == query[0]
        && response[1] == query[1]
        && (response[2] & 0x80) != 0
        && (response[3] & 0x0f) == 0
    ) {
        result = true;
    }
    zsock_close(socket);
    return result;
}

#endif

bool telos_zephyr_network_wait(
    uint32_t timeout_milliseconds,
    char *address,
    size_t address_size
)
{
#if defined(CONFIG_TELOS_NETWORK) && defined(CONFIG_NET_SOCKETS)
    struct net_if *interface;
    struct network_address result = {
        .buffer = address,
        .size = address_size,
    };

    if (timeout_milliseconds == 0 || address == NULL || address_size == 0) {
        return false;
    }
    interface = net_if_get_default();
    if (interface == NULL) {
        return false;
    }

    net_if_ipv4_addr_foreach(interface, find_ipv4_address, &result);
    return result.found && dns_probe(timeout_milliseconds);
#else
    (void)timeout_milliseconds;
    (void)address;
    (void)address_size;
    return false;
#endif
}
