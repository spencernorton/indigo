/* Keep the imported TZ table byte-identical while testing the owned caller
 * with strict conversion warnings. Its leaf count defaults to zero, and the
 * substring length is a nonnegative pointer difference. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "../../../cube/swiss/source/tzdata.c"
#pragma GCC diagnostic pop
