/**
 * test_file_cache.c - Unit tests for the file_cache module
 */

#include "file_cache.h"

#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ============= Test Harness ============= */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            printf("  ASSERT FAILED: %s (line %d)\n", #expr, __LINE__);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define TEST_ASSERT_EQ(a, b)                                                   \
    do {                                                                       \
        int _a = (a), _b = (b);                                                \
        if (_a != _b) {                                                        \
            printf("  ASSERT_EQ FAILED: %s == %d, expected %s == %d "          \
                   "(line %d)\n",                                              \
                   #a, _a, #b, _b, __LINE__);                                  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, b)                                               \
    do {                                                                       \
        const char* _a = (a);                                                  \
        const char* _b = (b);                                                  \
        if (strcmp(_a, _b) != 0) {                                             \
            printf("  ASSERT_STR_EQ FAILED: \"%s\" != \"%s\" (line %d)\n", _a, \
                   _b, __LINE__);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        g_tests_run++;                                                         \
        int _result = fn();                                                    \
        teardown(); /* Always cleanup, even on failure */                      \
        if (_result == 0) {                                                    \
            g_tests_passed++;                                                  \
            printf("[PASS] %s\n", #fn);                                        \
        } else {                                                               \
            g_tests_failed++;                                                  \
            printf("[FAIL] %s\n", #fn);                                        \
        }                                                                      \
    } while (0)

/* ============= Test Helpers ============= */

static char               g_test_dir[] = "/tmp/file_cache_test_XXXXXX";
static FileCacheInstance* g_cache      = NULL;

static void setup(void) {
    /* Create a unique temp directory */
    char* result = mkdtemp(g_test_dir);
    if (!result) {
        fprintf(stderr, "Failed to create temp directory\n");
        exit(1);
    }

    FileCacheConfig config = {
        .cache_dir   = g_test_dir,
        .ttl_seconds = 3600,
        .enabled     = true,
    };
    g_cache = file_cache_create(&config);
}

static void teardown(void) {
    if (g_cache) {
        file_cache_clear(g_cache);
        file_cache_destroy(g_cache);
        g_cache = NULL;
    }
    rmdir(g_test_dir);
    /* Reset template for next setup */
    strcpy(g_test_dir, "/tmp/file_cache_test_XXXXXX");
}

/* ============= Lifecycle Tests ============= */

static int test_create_destroy(void) {
    setup();
    TEST_ASSERT(g_cache != NULL);
    return 0;
}

static int test_create_null_config(void) {
    FileCacheInstance* cache = file_cache_create(NULL);
    TEST_ASSERT(cache == NULL);
    return 0;
}

static int test_create_creates_directory(void) {
    char  dir[]  = "/tmp/file_cache_mkdir_XXXXXX";
    char* result = mkdtemp(dir);
    TEST_ASSERT(result != NULL);
    rmdir(dir); /* remove so file_cache_create has to recreate it */

    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/sub", dir);

    FileCacheConfig config = {
        .cache_dir   = subdir,
        .ttl_seconds = 60,
        .enabled     = true,
    };
    FileCacheInstance* cache = file_cache_create(&config);
    TEST_ASSERT(cache != NULL);

    struct stat st;
    TEST_ASSERT(stat(subdir, &st) == 0);
    TEST_ASSERT(S_ISDIR(st.st_mode));

    file_cache_destroy(cache);
    rmdir(subdir);
    rmdir(dir);
    return 0;
}

/* ============= Core Operations Tests ============= */

static int test_generate_key(void) {
    setup();
    char            key[FILE_CACHE_KEY_LENGTH];
    FileCacheResult res =
        file_cache_generate_key(g_cache, "test_input", key, sizeof(key));
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(strlen(key) == 32); /* MD5 hex = 32 chars */
    return 0;
}

static int test_generate_key_null_input(void) {
    setup();
    char            key[FILE_CACHE_KEY_LENGTH];
    FileCacheResult res =
        file_cache_generate_key(g_cache, NULL, key, sizeof(key));
    TEST_ASSERT_EQ(res, FILE_CACHE_ERROR_PARAM);
    return 0;
}

static int test_generate_key_deterministic(void) {
    setup();
    char key1[FILE_CACHE_KEY_LENGTH];
    char key2[FILE_CACHE_KEY_LENGTH];
    file_cache_generate_key(g_cache, "same_input", key1, sizeof(key1));
    file_cache_generate_key(g_cache, "same_input", key2, sizeof(key2));
    TEST_ASSERT_STR_EQ(key1, key2);
    return 0;
}

static int test_save_and_load(void) {
    setup();
    const char*     data = "hello cache world";
    FileCacheResult res  = file_cache_save(g_cache, "testkey", data, 0);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);

    char* loaded = NULL;
    res          = file_cache_load(g_cache, "testkey", &loaded, NULL);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(loaded != NULL);
    TEST_ASSERT_STR_EQ(loaded, data);
    free(loaded);
    return 0;
}

static int test_save_and_load_size(void) {
    setup();
    const char* data          = "test data";
    size_t      expected_size = strlen(data);
    file_cache_save(g_cache, "sizekey", data, 0);

    char*           loaded = NULL;
    size_t          size   = 0;
    FileCacheResult res = file_cache_load(g_cache, "sizekey", &loaded, &size);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(size == expected_size);
    free(loaded);
    return 0;
}

static int test_load_not_found(void) {
    setup();
    char*           loaded = NULL;
    FileCacheResult res =
        file_cache_load(g_cache, "nonexistent_key", &loaded, NULL);
    TEST_ASSERT(res != FILE_CACHE_OK);
    TEST_ASSERT(loaded == NULL);
    return 0;
}

static int test_is_valid(void) {
    setup();
    file_cache_save(g_cache, "validkey", "data", 0);
    TEST_ASSERT(file_cache_is_valid(g_cache, "validkey") == true);
    return 0;
}

static int test_is_valid_nonexistent(void) {
    setup();
    TEST_ASSERT(file_cache_is_valid(g_cache, "nope") == false);
    return 0;
}

/* ============= File Locking Tests ============= */

static int test_lock_shared(void) {
    setup();
    file_cache_save(g_cache, "lockkey", "data", 0);

    FileCacheLock*  lock = NULL;
    FileCacheResult res =
        file_cache_lock(g_cache, "lockkey", FILE_CACHE_LOCK_SHARED, &lock);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(lock != NULL);
    file_cache_unlock(lock);
    return 0;
}

static int test_lock_exclusive(void) {
    setup();
    file_cache_save(g_cache, "exkey", "data", 0);

    FileCacheLock*  lock = NULL;
    FileCacheResult res =
        file_cache_lock(g_cache, "exkey", FILE_CACHE_LOCK_EXCLUSIVE, &lock);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(lock != NULL);
    file_cache_unlock(lock);
    return 0;
}

static int test_lock_nonexistent(void) {
    setup();
    FileCacheLock*  lock = NULL;
    FileCacheResult res =
        file_cache_lock(g_cache, "no_such_file", FILE_CACHE_LOCK_SHARED, &lock);
    TEST_ASSERT_EQ(res, FILE_CACHE_ERROR_NOT_FOUND);
    TEST_ASSERT(lock == NULL);
    return 0;
}

static int test_lock_null_params(void) {
    setup();
    FileCacheLock*  lock = NULL;
    FileCacheResult res =
        file_cache_lock(NULL, "key", FILE_CACHE_LOCK_SHARED, &lock);
    TEST_ASSERT_EQ(res, FILE_CACHE_ERROR_PARAM);

    res = file_cache_lock(g_cache, NULL, FILE_CACHE_LOCK_SHARED, &lock);
    TEST_ASSERT_EQ(res, FILE_CACHE_ERROR_PARAM);

    res = file_cache_lock(g_cache, "key", FILE_CACHE_LOCK_SHARED, NULL);
    TEST_ASSERT_EQ(res, FILE_CACHE_ERROR_PARAM);
    return 0;
}

static int test_unlock_null(void) {
    /* Should not crash */
    file_cache_unlock(NULL);
    return 0;
}

/**
 * Helper: try to acquire LOCK_EX|LOCK_NB on a file from a child process.
 * Returns 0 if lock acquired, 1 if blocked (EWOULDBLOCK).
 */
static int try_exclusive_lock_from_child(const char* filepath) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1; /* fork failed */
    }

    if (pid == 0) {
        /* Child: try non-blocking exclusive lock */
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) {
            _exit(2);
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            /* EWOULDBLOCK = lock is held by parent */
            close(fd);
            _exit(1);
        }
        /* Lock acquired */
        flock(fd, LOCK_UN);
        close(fd);
        _exit(0);
    }

    /* Parent: wait for child */
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static int test_shared_lock_blocks_exclusive(void) {
    setup();
    file_cache_save(g_cache, "blk", "data", 0);

    /* Get filepath for child process */
    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    file_cache_get_filepath(g_cache, "blk", filepath, sizeof(filepath));

    /* Parent holds shared lock */
    FileCacheLock* lock = NULL;
    file_cache_lock(g_cache, "blk", FILE_CACHE_LOCK_SHARED, &lock);
    TEST_ASSERT(lock != NULL);

    /* Child should be blocked (EWOULDBLOCK) */
    int child_result = try_exclusive_lock_from_child(filepath);
    TEST_ASSERT_EQ(child_result, 1);

    /* Release lock */
    file_cache_unlock(lock);

    /* Now child should succeed */
    child_result = try_exclusive_lock_from_child(filepath);
    TEST_ASSERT_EQ(child_result, 0);

    return 0;
}

static int test_exclusive_lock_blocks_exclusive(void) {
    setup();
    file_cache_save(g_cache, "exblk", "data", 0);

    char filepath[FILE_CACHE_MAX_PATH_LENGTH];
    file_cache_get_filepath(g_cache, "exblk", filepath, sizeof(filepath));

    /* Parent holds exclusive lock */
    FileCacheLock* lock = NULL;
    file_cache_lock(g_cache, "exblk", FILE_CACHE_LOCK_EXCLUSIVE, &lock);
    TEST_ASSERT(lock != NULL);

    /* Child should be blocked */
    int child_result = try_exclusive_lock_from_child(filepath);
    TEST_ASSERT_EQ(child_result, 1);

    /* Release lock */
    file_cache_unlock(lock);

    /* Now child should succeed */
    child_result = try_exclusive_lock_from_child(filepath);
    TEST_ASSERT_EQ(child_result, 0);

    return 0;
}

static int test_multiple_shared_locks(void) {
    setup();
    file_cache_save(g_cache, "multi", "data", 0);

    /* Two shared locks on the same file should both succeed */
    FileCacheLock*  lock1 = NULL;
    FileCacheLock*  lock2 = NULL;
    FileCacheResult res1 =
        file_cache_lock(g_cache, "multi", FILE_CACHE_LOCK_SHARED, &lock1);
    FileCacheResult res2 =
        file_cache_lock(g_cache, "multi", FILE_CACHE_LOCK_SHARED, &lock2);
    TEST_ASSERT_EQ(res1, FILE_CACHE_OK);
    TEST_ASSERT_EQ(res2, FILE_CACHE_OK);
    TEST_ASSERT(lock1 != NULL);
    TEST_ASSERT(lock2 != NULL);

    file_cache_unlock(lock1);
    file_cache_unlock(lock2);
    return 0;
}

/* ============= JSON Helper Tests ============= */

static int test_save_load_json(void) {
    setup();
    json_t* obj = json_object();
    json_object_set_new(obj, "city", json_string("Stockholm"));
    json_object_set_new(obj, "temp", json_real(22.5));

    FileCacheResult res = file_cache_save_json(g_cache, "jsonkey", obj);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    json_decref(obj);

    json_t* loaded = NULL;
    res            = file_cache_load_json(g_cache, "jsonkey", (void**)&loaded);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(loaded != NULL);

    json_t* city = json_object_get(loaded, "city");
    TEST_ASSERT(city != NULL);
    TEST_ASSERT_STR_EQ(json_string_value(city), "Stockholm");

    json_t* temp = json_object_get(loaded, "temp");
    TEST_ASSERT(temp != NULL);
    TEST_ASSERT(json_real_value(temp) == 22.5);

    json_decref(loaded);
    return 0;
}

static int test_load_json_not_found(void) {
    setup();
    json_t*         loaded = NULL;
    FileCacheResult res =
        file_cache_load_json(g_cache, "nojson", (void**)&loaded);
    TEST_ASSERT(res != FILE_CACHE_OK);
    TEST_ASSERT(loaded == NULL);
    return 0;
}

/* ============= Cache Management Tests ============= */

static int test_invalidate(void) {
    setup();
    file_cache_save(g_cache, "inv_key", "data", 0);
    TEST_ASSERT(file_cache_is_valid(g_cache, "inv_key") == true);

    FileCacheResult res = file_cache_invalidate(g_cache, "inv_key");
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT(file_cache_is_valid(g_cache, "inv_key") == false);
    return 0;
}

static int test_invalidate_nonexistent(void) {
    setup();
    FileCacheResult res = file_cache_invalidate(g_cache, "no_key");
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    return 0;
}

static int test_clear(void) {
    setup();
    file_cache_save(g_cache, "key_a", "aaa", 0);
    file_cache_save(g_cache, "key_b", "bbb", 0);
    file_cache_save(g_cache, "key_c", "ccc", 0);

    TEST_ASSERT(file_cache_is_valid(g_cache, "key_a") == true);
    TEST_ASSERT(file_cache_is_valid(g_cache, "key_b") == true);

    FileCacheResult res = file_cache_clear(g_cache);
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);

    TEST_ASSERT(file_cache_is_valid(g_cache, "key_a") == false);
    TEST_ASSERT(file_cache_is_valid(g_cache, "key_b") == false);
    TEST_ASSERT(file_cache_is_valid(g_cache, "key_c") == false);
    return 0;
}

/* ============= Utility Tests ============= */

static int test_normalize_string(void) {
    char            out[128];
    FileCacheResult res =
        file_cache_normalize_string("Hello World", out, sizeof(out));
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);
    TEST_ASSERT_STR_EQ(out, "hello_world");
    return 0;
}

static int test_normalize_consecutive_separators(void) {
    char out[128];
    file_cache_normalize_string("a  b__c", out, sizeof(out));
    TEST_ASSERT_STR_EQ(out, "a_b_c");
    return 0;
}

static int test_normalize_trim(void) {
    char out[128];
    file_cache_normalize_string(" hello ", out, sizeof(out));
    TEST_ASSERT_STR_EQ(out, "hello");
    return 0;
}

static int test_get_filepath(void) {
    setup();
    char            path[FILE_CACHE_MAX_PATH_LENGTH];
    FileCacheResult res =
        file_cache_get_filepath(g_cache, "mykey", path, sizeof(path));
    TEST_ASSERT_EQ(res, FILE_CACHE_OK);

    /* Should end with /mykey.json */
    size_t len = strlen(path);
    TEST_ASSERT(len > 11);
    TEST_ASSERT_STR_EQ(path + len - 11, "/mykey.json");
    return 0;
}

/* ============= Main ============= */

int main(void) {
    printf("=== file_cache unit tests ===\n\n");

    /* Lifecycle */
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_create_null_config);
    RUN_TEST(test_create_creates_directory);

    /* Core operations */
    RUN_TEST(test_generate_key);
    RUN_TEST(test_generate_key_null_input);
    RUN_TEST(test_generate_key_deterministic);
    RUN_TEST(test_save_and_load);
    RUN_TEST(test_save_and_load_size);
    RUN_TEST(test_load_not_found);
    RUN_TEST(test_is_valid);
    RUN_TEST(test_is_valid_nonexistent);

    /* File locking */
    RUN_TEST(test_lock_shared);
    RUN_TEST(test_lock_exclusive);
    RUN_TEST(test_lock_nonexistent);
    RUN_TEST(test_lock_null_params);
    RUN_TEST(test_unlock_null);
    RUN_TEST(test_shared_lock_blocks_exclusive);
    RUN_TEST(test_exclusive_lock_blocks_exclusive);
    RUN_TEST(test_multiple_shared_locks);

    /* JSON helpers */
    RUN_TEST(test_save_load_json);
    RUN_TEST(test_load_json_not_found);

    /* Cache management */
    RUN_TEST(test_invalidate);
    RUN_TEST(test_invalidate_nonexistent);
    RUN_TEST(test_clear);

    /* Utilities */
    RUN_TEST(test_normalize_string);
    RUN_TEST(test_normalize_consecutive_separators);
    RUN_TEST(test_normalize_trim);
    RUN_TEST(test_get_filepath);

    printf("\n=== %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    if (g_tests_failed > 0) {
        printf("=== %d FAILED ===\n", g_tests_failed);
        return 1;
    }

    return 0;
}
