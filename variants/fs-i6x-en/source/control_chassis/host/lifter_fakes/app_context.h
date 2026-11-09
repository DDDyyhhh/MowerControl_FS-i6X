#ifndef BAG_TEST_APP_CONTEXT_H
#define BAG_TEST_APP_CONTEXT_H
#include <stdbool.h>
typedef struct { bool grass_bag_status; int g_system_report_state; } BagTestContext_t;
extern BagTestContext_t g_app;
enum { REPORT_NONE, REPORT_BLADE_INITIALIZING, REPORT_DUMPING_START,
       REPORT_DUMPING_CLOSING, REPORT_DUMPING_DONE };
#endif
