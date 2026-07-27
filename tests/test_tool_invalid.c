#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <telos/tool.h>

struct fixture {
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_capability_broker *broker;
    struct telos_tool_definition definition;
};

static enum telos_policy_decision policy_decision = TELOS_POLICY_ALLOW;
static struct telos_cancel *cancel_during_execution;

static enum telos_policy_decision evaluate(
    const struct telos_policy_request *request,
    void *context
)
{
    (void)request;
    (void)context;
    return policy_decision;
}

static bool return_value(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)arguments;
    (void)error;
    *result = telos_value_new_null();
    if (cancel_during_execution != NULL) {
        telos_cancel_request(cancel_during_execution);
    }
    return *result != NULL;
}

static bool fail_silently(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)arguments;
    (void)error;
    *result = telos_value_new_string("discard");
    return false;
}

static bool succeed_without_value(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)arguments;
    (void)result;
    (void)error;
    return true;
}

static struct telos_value *json(const char *text)
{
    return telos_value_parse_json(text, strlen(text), NULL);
}

static bool fixture_create(struct fixture *fixture)
{
    const char *capabilities[] = {"filesystem.read"};
    const struct telos_extension_descriptor descriptor = {
        .id = "schema",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &fixture->definition,
    };
    struct telos_registry_transaction *transaction;

    memset(fixture, 0, sizeof(*fixture));
    fixture->definition.id = "schema";
    fixture->definition.execute = return_value;
    fixture->registry = telos_registry_create(capabilities, 1, NULL);
    if (fixture->registry == NULL) {
        return false;
    }
    transaction = telos_registry_transaction_begin(
        fixture->registry,
        "test.tools",
        NULL
    );
    if (
        transaction == NULL
        || !telos_registry_transaction_add(transaction, &descriptor, NULL)
        || !telos_registry_transaction_commit(transaction, NULL)
    ) {
        return false;
    }
    fixture->generation = telos_registry_acquire(fixture->registry);
    fixture->broker = telos_capability_broker_create(
        capabilities,
        1,
        evaluate,
        NULL,
        NULL
    );
    return fixture->generation != NULL && fixture->broker != NULL;
}

static void fixture_destroy(struct fixture *fixture)
{
    telos_capability_broker_destroy(fixture->broker);
    telos_registry_generation_release(fixture->generation);
    telos_registry_destroy(fixture->registry);
}

static bool execute(
    struct fixture *fixture,
    const char *tool_id,
    enum telos_execution_domain domain,
    const struct telos_value *arguments,
    const struct telos_cancel *cancel,
    struct telos_error **error
)
{
    struct telos_value *result = NULL;
    bool succeeded = telos_tool_execute(
        fixture->generation,
        fixture->broker,
        domain,
        tool_id,
        arguments,
        cancel,
        &result,
        error
    );

    telos_value_release(result);
    return succeeded;
}

static bool rejects_schema(
    struct fixture *fixture,
    const char *schema_text,
    const char *arguments_text
)
{
    struct telos_value *schema = json(schema_text);
    struct telos_value *arguments = json(arguments_text);
    struct telos_error *error = NULL;
    bool rejected;

    fixture->definition.input_schema = schema;
    fixture->definition.execute = return_value;
    rejected = schema != NULL
        && arguments != NULL
        && !execute(
            fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            arguments,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    telos_value_release(arguments);
    telos_value_release(schema);
    return rejected;
}

static bool accepts_schema(
    struct fixture *fixture,
    const char *schema_text,
    const char *arguments_text
)
{
    struct telos_value *schema = json(schema_text);
    struct telos_value *arguments = json(arguments_text);
    bool accepted;

    fixture->definition.input_schema = schema;
    fixture->definition.execute = return_value;
    accepted = schema != NULL
        && arguments != NULL
        && execute(
            fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            arguments,
            NULL,
            NULL
        );
    telos_value_release(arguments);
    telos_value_release(schema);
    return accepted;
}

int main(void)
{
    struct fixture fixture;
    struct telos_value *empty = json("{}");
    struct telos_value *empty_schema = json("{}");
    struct telos_error *error = NULL;
    struct telos_value *result = NULL;
    struct telos_cancel *cancel = telos_cancel_create();
    const char *bad_capabilities[] = {NULL};
    const char *empty_capabilities[] = {""};
    const char *required_capabilities[] = {"filesystem.write"};
    bool passed = fixture_create(&fixture);

    passed = passed
        && telos_capability_broker_create(NULL, 1, evaluate, NULL, &error)
            == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_capability_broker_create(NULL, 0, NULL, NULL, &error)
            == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_capability_broker_create(
            bad_capabilities,
            1,
            evaluate,
            NULL,
            &error
        ) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_capability_broker_create(
            empty_capabilities,
            1,
            evaluate,
            NULL,
            &error
        ) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_capability_broker_create(
            empty_capabilities,
            SIZE_MAX,
            evaluate,
            NULL,
            &error
        ) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    telos_capability_broker_destroy(NULL);

    passed = passed
        && !telos_tool_execute(
            NULL,
            fixture.broker,
            TELOS_EXECUTION_CORE,
            "schema",
            empty,
            NULL,
            &result,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !telos_tool_execute(
            fixture.generation,
            NULL,
            TELOS_EXECUTION_CORE,
            "schema",
            empty,
            NULL,
            &result,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !execute(
            &fixture,
            "",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !execute(
            &fixture,
            "missing",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_PROVIDER,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;

    fixture.definition.input_schema = empty_schema;
    fixture.definition.id = "different";
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    fixture.definition.id = "schema";

    passed = passed
        && rejects_schema(&fixture, "[]", "{}")
        && rejects_schema(&fixture, "{\"type\":\"unknown\"}", "null")
        && rejects_schema(&fixture, "{\"type\":\"object\"}", "[]")
        && rejects_schema(
            &fixture,
            "{\"type\":\"object\",\"required\":{}}",
            "{}"
        )
        && rejects_schema(
            &fixture,
            "{\"type\":\"object\",\"required\":[1]}",
            "{}"
        )
        && rejects_schema(
            &fixture,
            "{\"type\":\"object\",\"required\":[\"name\"]}",
            "{}"
        )
        && rejects_schema(
            &fixture,
            "{\"type\":\"object\",\"properties\":[]}",
            "{}"
        )
        && rejects_schema(
            &fixture,
            "{\"type\":\"object\",\"properties\":"
            "{\"name\":{\"type\":\"string\"}}}",
            "{\"name\":1}"
        )
        && accepts_schema(&fixture, "{}", "null")
        && accepts_schema(&fixture, "{\"type\":\"object\"}", "{}")
        && accepts_schema(&fixture, "{\"type\":\"array\"}", "[]")
        && accepts_schema(&fixture, "{\"type\":\"string\"}", "\"ok\"")
        && accepts_schema(&fixture, "{\"type\":\"integer\"}", "1")
        && accepts_schema(&fixture, "{\"type\":\"number\"}", "1")
        && accepts_schema(&fixture, "{\"type\":\"number\"}", "1.5")
        && accepts_schema(&fixture, "{\"type\":\"boolean\"}", "true")
        && accepts_schema(&fixture, "{\"type\":\"null\"}", "null")
        && accepts_schema(
            &fixture,
            "{\"type\":\"object\",\"required\":[\"name\"],"
            "\"properties\":{\"name\":{\"type\":\"string\"}}}",
            "{\"name\":\"ok\"}"
        );

    fixture.definition.input_schema = empty_schema;
    fixture.definition.required_capabilities = required_capabilities;
    fixture.definition.required_capability_count = 1;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    fixture.definition.required_capabilities = NULL;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    fixture.definition.required_capability_count = 0;

    policy_decision = TELOS_POLICY_DENY;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    policy_decision = TELOS_POLICY_REQUIRE_APPROVAL;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    policy_decision = TELOS_POLICY_ALLOW;

    telos_cancel_request(cancel);
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            cancel,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    telos_cancel_release(cancel);
    cancel = telos_cancel_create();

    fixture.definition.execute = fail_silently;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            NULL
        );

    fixture.definition.execute = succeed_without_value;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            NULL,
            &error
        )
        && error != NULL;
    telos_error_release(error);
    error = NULL;

    fixture.definition.execute = return_value;
    cancel_during_execution = cancel;
    passed = passed
        && !execute(
            &fixture,
            "schema",
            TELOS_EXECUTION_CORE,
            empty,
            cancel,
            &error
        )
        && error != NULL;
    cancel_during_execution = NULL;
    telos_error_release(error);
    telos_cancel_release(cancel);
    telos_value_release(empty_schema);
    telos_value_release(empty);
    fixture_destroy(&fixture);

    if (!passed) {
        fputs("Tool validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
