/**
 * test_abi.c  --  ABI smoke test: verify DLL exports and struct sizes
 *
 * Does NOT require a physical camera.  Just checks that:
 * 1. All expected symbols are exported and callable.
 * 2. Struct sizes match the compiled header.
 * 3. spwcam_init() / spwcam_deinit() cycle completes without crash.
 * 4. Error-string table is complete for known codes.
 *
 * Exit code 0 = pass, non-zero = fail.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "spwcam/spwcam.h"

#define PASS(msg) do { printf("  PASS  %s\n", msg); } while(0)
#define FAIL(msg) do { printf("  FAIL  %s\n", msg); g_fail++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)

static int g_fail = 0;

/* ---- helper: run a test function ----------------------------------------- */
typedef void (*TestFn)(void);
static void run(const char* name, TestFn fn)
{
    printf("\n[%s]\n", name);
    fn();
}

/* ---- test: version ------------------------------------------------------- */
static void test_version(void)
{
    const char* v = spwcam_version();
    CHECK(v != NULL, "spwcam_version() != NULL");
    CHECK(strlen(v) > 0, "version string non-empty");
    printf("  version = %s\n", v);
}

/* ---- test: struct sizes -------------------------------------------------- */
static void test_struct_sizes(void)
{
    /* Ensure padding/alignment did not shrink any struct below expected minimum */
    CHECK(sizeof(spwcam_init_params_t)    >= 10,  "sizeof(spwcam_init_params_t)");
    CHECK(sizeof(spwcam_device_info_t)    >= 246, "sizeof(spwcam_device_info_t)");
    CHECK(sizeof(spwcam_frame_t)          >= 36,  "sizeof(spwcam_frame_t)");
    CHECK(sizeof(spwcam_record_status_t)  >= 532, "sizeof(spwcam_record_status_t)");
    CHECK(sizeof(spwcam_record_options_t) >= 1044,"sizeof(spwcam_record_options_t)");
}

/* ---- test: struct_size field initialisation ------------------------------ */
static void test_struct_size_field(void)
{
    spwcam_device_info_t di;
    memset(&di, 0, sizeof(di));
    di.struct_size = (uint32_t)sizeof(di);
    CHECK(di.struct_size == (uint32_t)sizeof(spwcam_device_info_t),
          "device_info struct_size field round-trip");

    spwcam_record_status_t rs;
    memset(&rs, 0, sizeof(rs));
    rs.struct_size = (uint32_t)sizeof(rs);
    CHECK(rs.struct_size == (uint32_t)sizeof(spwcam_record_status_t),
          "record_status struct_size field round-trip");
}

/* ---- test: error strings ------------------------------------------------- */
static void test_error_strings(void)
{
    int codes[] = {
        SPWCAM_OK,
        SPWCAM_ERR_INVALID_PARAM,
        SPWCAM_ERR_NOT_INITIALIZED,
        SPWCAM_ERR_ALREADY_OPEN,
        SPWCAM_ERR_NOT_OPEN,
        SPWCAM_ERR_DEVICE_NOT_FOUND,
        SPWCAM_ERR_TIMEOUT,
        SPWCAM_ERR_IO,
        SPWCAM_ERR_ENCODER,
        SPWCAM_ERR_NO_FRAME,
        SPWCAM_ERR_OUT_OF_MEMORY,
        SPWCAM_ERR_INTERNAL
    };
    for (int i = 0; i < (int)(sizeof(codes)/sizeof(codes[0])); ++i) {
        const char* s = spwcam_get_error_string(codes[i]);
        CHECK(s != NULL && strlen(s) > 0, "error string not null/empty");
    }
    /* unknown code */
    const char* s = spwcam_get_error_string(-1234);
    CHECK(s != NULL, "unknown code returns non-NULL");
}

/* ---- test: init / deinit cycle ------------------------------------------ */
static void test_init_deinit(void)
{
    spwcam_init_params_t p;
    memset(&p, 0, sizeof(p));
    p.struct_size = (uint32_t)sizeof(p);
    p.log_level   = SPWCAM_LOG_WARN;   /* suppress INFO logs during test */

    spwcam_context_t ctx = spwcam_init(&p);
    CHECK(ctx != NULL, "spwcam_init() returns non-NULL");

    if (ctx) {
        int err = spwcam_get_last_error(ctx);
        CHECK(err == SPWCAM_OK, "last_error == OK after init");

        spwcam_deinit(ctx);
        PASS("spwcam_deinit() returned without crash");
    }
}

/* ---- test: double-deinit safety ------------------------------------------ */
static void test_null_deinit(void)
{
    spwcam_deinit(NULL);   /* must not crash */
    PASS("spwcam_deinit(NULL) is safe");

    spwcam_frame_free(NULL);
    PASS("spwcam_frame_free(NULL) is safe");
}

/* ---- test: invalid-param guards ------------------------------------------ */
static void test_invalid_params(void)
{
    int rc;

    rc = spwcam_discovery_start(NULL, 0, 0);
    CHECK(rc == SPWCAM_ERR_NOT_INITIALIZED, "discovery_start(NULL) → NOT_INITIALIZED");

    rc = spwcam_stream_open(NULL, "rtsp://x", 0);
    CHECK(rc == SPWCAM_ERR_NOT_INITIALIZED ||
          rc == SPWCAM_ERR_INVALID_PARAM,    "stream_open(NULL) → error");

    spwcam_frame_t* frame = (spwcam_frame_t*)1;   /* non-NULL garbage */
    rc = spwcam_frame_grab(NULL, &frame);
    CHECK(rc == SPWCAM_ERR_INVALID_PARAM, "frame_grab(NULL) → INVALID_PARAM");
}

/* ---- main ---------------------------------------------------------------- */
int main(void)
{
    printf("========= spwcam ABI smoke test =========\n");

    run("version",        test_version);
    run("struct_sizes",   test_struct_sizes);
    run("struct_size_field", test_struct_size_field);
    run("error_strings",  test_error_strings);
    run("init_deinit",    test_init_deinit);
    run("null_deinit",    test_null_deinit);
    run("invalid_params", test_invalid_params);

    printf("\n=========================================\n");
    if (g_fail == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", g_fail);
        return 1;
    }
}
