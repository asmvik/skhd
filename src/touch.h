#ifndef SKHD_TOUCH_H
#define SKHD_TOUCH_H

#include <stdbool.h>

struct table;
struct mode;
struct carbon_event;

bool touch_begin(struct table *mode_map, struct table *blacklst, struct mode **current_mode, struct carbon_event *carbon);
void touch_end(void);

#endif
