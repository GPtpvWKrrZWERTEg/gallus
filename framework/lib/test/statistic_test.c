/* 
 * $__Copyright__$
 */
#include "gallus_apis.h"
#include "unity.h"

#include <math.h>





void
setUp(void) {
}


void
tearDown(void) {
}





void
test_normal(void) {
  gallus_result_t r;
  gallus_statistic_t s = NULL;
  gallus_statistic_t s_check = NULL;

  r = gallus_statistic_create(&s, "test");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  if (r == GALLUS_RESULT_OK) {
    size_t i;
    int64_t min, max;
    double avg, avg_check;
    int64_t sum = 0;
    double sum2 = 0.0;
    double sd, sd_check, ssd_check;

    r = gallus_statistic_find(&s_check, "test");
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(s, s_check);

    r = gallus_statistic_sample_n(&s);
    TEST_ASSERT_EQUAL(r, 0);

    for (i = 1; i <= 10; i++) {
      r = gallus_statistic_record(&s, (int64_t)i);
      TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
      sum += (int64_t)i;
    }
    avg_check = (double)sum / 10.0;

    for (i = 1; i <= 10; i++) {
      sum2 += ((double)i - avg_check) * ((double)i - avg_check);
    }
    sd_check = sqrt(sum2 / 10.0);
    ssd_check = sqrt(sum2 / 9.0);

    r = gallus_statistic_sample_n(&s);
    TEST_ASSERT_EQUAL(r, 10);

    r = gallus_statistic_min(&s, &min);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(min, 1);

    r = gallus_statistic_max(&s, &max);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(max, 10);

    r = gallus_statistic_average(&s, &avg);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(avg, avg_check);

    r = gallus_statistic_sd(&s, &sd, false);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(sd, sd_check);

    r = gallus_statistic_sd(&s, &sd, true);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(sd, ssd_check);

    r = gallus_statistic_reset(&s);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

    r = gallus_statistic_sample_n(&s);
    TEST_ASSERT_EQUAL(r, 0);

    r = gallus_statistic_min(&s, &min);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(min, LLONG_MAX);

    r = gallus_statistic_max(&s, &max);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(max, LLONG_MIN);

    r = gallus_statistic_average(&s, &avg);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(avg, 0);

    r = gallus_statistic_sd(&s, &sd, false);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(sd, 0);

    r = gallus_statistic_sd(&s, &sd, true);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    TEST_ASSERT_EQUAL(sd, 0);

    gallus_statistic_destroy(&s);
    s = NULL;

    r = gallus_statistic_find(&s, "test");
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_NOT_FOUND);
    TEST_ASSERT_EQUAL(s, NULL);
  }
}


void
test_invalid_args(void) {
  gallus_result_t r;
  gallus_statistic_t s = NULL;
  gallus_statistic_t s_check = NULL;
  int64_t dum_int;
  double dum_dbl;

  r = gallus_statistic_create(NULL, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_create(&s, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_create(&s, "");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_create(&s, "test2");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_statistic_find(NULL, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_find(NULL, "");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_find(&s_check, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_find(&s_check, "");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_reset(NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_sample_n(NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_min(NULL, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_min(&s, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_min(NULL, &dum_int);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_max(NULL, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_max(&s, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_max(NULL, &dum_int);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_average(NULL, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_average(&s, NULL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_average(NULL, &dum_dbl);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_sd(NULL, NULL, false);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_sd(&s, NULL, false);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_sd(NULL, &dum_dbl, false);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  r = gallus_statistic_sd(NULL, NULL, true);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_sd(&s, NULL, true);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);
  r = gallus_statistic_sd(NULL, &dum_dbl, true);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_INVALID_ARGS);

  gallus_statistic_destroy_by_name("test2");

  r = gallus_statistic_find(&s, "test2");
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_NOT_FOUND);
  TEST_ASSERT_EQUAL(s, NULL);
}
