/*
 * unit_product_topics — unit tests for product topic helpers.
 */
#include <stdio.h>
#include <string.h>

#include "../../app/src/product_topics.h"

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int fail_before;

#define CHECK(expr) do {                                                \
    tests_run++;                                                        \
    if (expr) {                                                         \
        tests_passed++;                                                 \
    } else {                                                            \
        tests_failed++;                                                 \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);\
    }                                                                   \
} while (0)

#define RUN(fn) do {                                                    \
    fail_before = tests_failed;                                         \
    printf("  %-50s ", #fn);                                            \
    fn();                                                               \
    printf("%s\n", tests_failed == fail_before ? "ok" : "FAIL");      \
} while (0)

static void make_settings(field_bridge_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    strcpy(settings->broker.topic_prefix, "site/field-a");
}

static void test_stream_topics(void)
{
    field_bridge_settings_t settings;
    char topic[128];
    make_settings(&settings);

    CHECK(product_topic_status(&settings, topic, sizeof(topic)) == 0);
    CHECK(strcmp(topic, "site/field-a/status") == 0);
    CHECK(product_topic_io(&settings, topic, sizeof(topic)) == 0);
    CHECK(strcmp(topic, "site/field-a/io") == 0);
    CHECK(product_topic_event(&settings, topic, sizeof(topic)) == 0);
    CHECK(strcmp(topic, "site/field-a/event") == 0);
    CHECK(product_topic_test(&settings, topic, sizeof(topic)) == 0);
    CHECK(strcmp(topic, "site/field-a/test") == 0);
}

static void test_rejects_invalid_args(void)
{
    field_bridge_settings_t settings;
    char topic[8];
    make_settings(&settings);

    CHECK(product_topic_build(NULL, "status", topic, sizeof(topic)) == -1);
    CHECK(product_topic_build(&settings, NULL, topic, sizeof(topic)) == -1);
    CHECK(product_topic_build(&settings, "", topic, sizeof(topic)) == -1);
    CHECK(product_topic_build(&settings, "bad/stream", topic, sizeof(topic)) == -1);
    CHECK(product_topic_build(&settings, "status", NULL, sizeof(topic)) == -1);
    CHECK(product_topic_build(&settings, "status", topic, 0) == -1);
    CHECK(product_topic_build(&settings, "status", topic, sizeof(topic)) == -1);
}

static void test_prefix_match(void)
{
    field_bridge_settings_t settings;
    make_settings(&settings);

    CHECK(product_topic_matches_prefix(&settings, "site/field-a/status") == 1);
    CHECK(product_topic_matches_prefix(&settings, "site/field-a/io") == 1);
    CHECK(product_topic_matches_prefix(&settings, "site/field-a") == 0);
    CHECK(product_topic_matches_prefix(&settings, "site/field-b/io") == 0);
    CHECK(product_topic_matches_prefix(&settings, NULL) == 0);
}

int main(void)
{
    printf("=== unit_product_topics ===\n");

    RUN(test_stream_topics);
    RUN(test_rejects_invalid_args);
    RUN(test_prefix_match);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
