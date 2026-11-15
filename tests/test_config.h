#pragma once
#ifndef __ASSEMBLER__
#include <stdlib.h>
/* Fail tests instead of hanging forever on an LVGL assertion. */
#define LV_ASSERT_HANDLER abort();
#endif
