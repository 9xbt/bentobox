#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Include the scheduler header to access sched_exec or equivalent */
#include "kernel/sys/sched.c"

#define USER_STACK_SIZE 4096

START_TEST(test_argv_envp_bounded_copy)
{
    /* Invariant: argv/envp strings must never overflow the allocated user stack region */
    char overflow_payload[USER_STACK_SIZE * 2];
    memset(overflow_payload, 'A', sizeof(overflow_payload) - 1);
    overflow_payload[sizeof(overflow_payload) - 1] = '\0';

    char boundary_payload[USER_STACK_SIZE];
    memset(boundary_payload, 'B', sizeof(boundary_payload) - 1);
    boundary_payload[sizeof(boundary_payload) - 1] = '\0';

    const char *valid_payload = "hello";

    const char *payloads[] = {
        overflow_payload,   /* exploit: string larger than stack */
        boundary_payload,   /* boundary: exactly stack size */
        valid_payload       /* valid: short string */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        /* Allocate a guarded stack region to detect overflow */
        char *stack_region = calloc(1, USER_STACK_SIZE);
        ck_assert_ptr_nonnull(stack_region);

        /* Simulate the copy: total argv content must fit within stack */
        size_t len = strlen(payloads[i]) + 1;
        /* The security property: copy length must be bounded to stack size */
        ck_assert_msg(len <= USER_STACK_SIZE,
            "argv/envp string length %zu exceeds stack bounds %d — "
            "scheduler must reject or truncate oversized arguments",
            len, USER_STACK_SIZE);

        free(stack_region);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_argv_envp_bounded_copy);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}