#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

static void compare(char *input, char *expected_output)
{
    char *input_sanitized = sanitize_path(input);
    if (!input_sanitized) {
        exit(1);
    }

    if (strcmp(input_sanitized, expected_output)) {
        printf("\n\nInput =\t\t\t%s\nOutput =\t\t%s\nExpected output =\t%s\n",
               input, input_sanitized, expected_output);

        exit(1);
    }

    free(input_sanitized);
}

static void sanitize_path_test(void)
{
    /* The maximum accepted input keeps its terminator. A buffer with no
     * terminator within the bound, ending in a partial .. element, must fail
     * rather than silently select a truncated path. The buffer is exactly
     * MAX_PATH_LEN bytes, so any read past the bound is an overflow.
     */
    char limit[MAX_PATH_LEN];
    memset(limit, 'a', sizeof(limit));
    limit[MAX_PATH_LEN - 1] = '\0';
    compare(limit, limit);
    limit[MAX_PATH_LEN - 2] = '.';
    limit[MAX_PATH_LEN - 1] = '.';
    assert(!sanitize_path(limit));

    /* Already clean */
    compare("", ".");
    compare("abc", "abc");
    compare("abc/def", "abc/def");
    compare(".", ".");
    compare("..", "..");
    compare("../..", "../..");
    compare("../../abc", "../../abc");
    compare("/abc", "/abc");
    compare("/", "/");

    /* Remove trailing slash */
    compare("abc/", "abc");
    compare("abc/def/", "abc/def");
    compare("a/b/c/", "a/b/c");
    compare("./", ".");
    compare("../", "..");
    compare("../../", "../..");
    compare("/abc/", "/abc");

    /* Remove doubled slash */
    compare("abc//def//ghi", "abc/def/ghi");
    compare("//abc", "/abc");
    compare("///abc", "/abc");
    compare("//abc//", "/abc");
    compare("abc//", "abc");

    /* Remove . elements */
    compare("abc/./def", "abc/def");
    compare("/./abc/def", "/abc/def");
    compare("abc/.", "abc");

    /* Remove .. elements */
    compare("abc/def/ghi/../jkl", "abc/def/jkl");
    compare("abc/def/../ghi/../jkl", "abc/jkl");
    compare("abc/def/..", "abc");
    compare("abc/def/../..", ".");
    compare("/abc/def/../..", "/");
    compare("abc/def/../../..", "..");
    compare("/abc/def/../../..", "/");
    compare("abc/def/../../../ghi/jkl/../../../mno", "../../mno");

    /* Combinations */
    compare("abc/./../def", "def");
    compare("abc//./../def", "def");
    compare("abc/../../././../def", "../../def");
}

int main(void)
{
    sanitize_path_test();

    return 0;
}
