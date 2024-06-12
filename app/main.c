/* 
 * $__Copyright__$
 */
#include "gallus_apis.h"

int
main(int argc, const char *const argv[]) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if ((ret = gallus_log_initialize(GALLUS_LOG_EMIT_TO_FILE, "./log",
                                   false, true, false)) != GALLUS_RESULT_OK) {
    goto done;
  }
  if ((ret = gallus_set_pidfile("./app.pid")) != GALLUS_RESULT_OK) {
    goto done;
  }
  ret = gallus_mainloop(argc, argv, NULL, NULL,
                        true, true, false);
done:
  if (ret != GALLUS_RESULT_OK) {
    gallus_perror(ret);
  }

  return (ret == GALLUS_RESULT_OK) ? 0 : 1;
}
