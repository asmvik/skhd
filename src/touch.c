#include <math.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Carbon/Carbon.h>

#include "multitouch.h"
#include "touch.h"
#include "log.h"
#include "hotkey.h"
#include "carbon.h"
#include "hashtable.h"

#define THREE_FINGER_SWIPE_MIN_DISTANCE 0.05f
#define THREE_FINGER_SWIPE_MIN_VELOCITY 0.05f
#define THREE_FINGER_SWIPE_AXIS_RATIO   1.45f
#define THREE_FINGER_TAP_MAX_DURATION   0.18
#define THREE_FINGER_TAP_MAX_MOVEMENT   0.04f
#define TIP_TAP_MAX_DURATION            0.10
#define TIP_TAP_HOLD_MAX_MOVEMENT       0.08f
#define TIP_TAP_TAP_MAX_MOVEMENT        0.03f
#define TIP_TAP_MIN_OFFSET              0.04f
#define TIP_TAP_AXIS_RATIO              0.30f

struct touch_context
{
	struct table *mode_map;
	struct table *blacklst;
	struct mode **current_mode;
	struct carbon_event *carbon;
};

static struct touch_context touch_ctx;
static MTDeviceRef touch_device;
static bool touch_tracking;
static bool touch_fired;
static MTPoint touch_start_pos;
static double touch_start_time;
static float touch_max_delta;
static bool touch_tracking_four;
static bool touch_fired_four;
static MTPoint touch_start_pos_four;
static double touch_start_time_four;
static float touch_max_delta_four;
static bool touch_tracking_five;
static bool touch_fired_five;
static MTPoint touch_start_pos_five;
static double touch_start_time_five;
static float touch_max_delta_five;
static enum {
	TipTapState_Idle = 0,
	TipTapState_TwoFingersDown,
	TipTapState_ThreeFingersDown
} tip_tap_state;
static MTPoint tip_tap_two_finger_pos;
static MTPoint tip_tap_three_finger_pos_1;
static MTPoint tip_tap_three_finger_pos_2;
static MTPoint tip_tap_three_finger_pos_3;
static double tip_tap_two_finger_start_time;
static double tip_tap_three_fingers_start_time;
static float tip_tap_two_finger_max_delta;
static float tip_tap_tap_max_delta;
static bool tip_tap_gesture_fired;

static inline uint32_t
current_modifier_flags(void)
{
	CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
	return cgevent_flags_to_hotkey_flags(flags);
}

static inline void
dispatch_gesture(uint32_t key)
{
	if (!touch_ctx.current_mode || !(*touch_ctx.current_mode)) return;
	if (touch_ctx.blacklst && touch_ctx.carbon &&
		table_find(touch_ctx.blacklst, touch_ctx.carbon->process_name)) {
		return;
	}

	struct hotkey eventkey = {
		.key = key,
		.flags = current_modifier_flags()
	};

	find_and_exec_hotkey(&eventkey, touch_ctx.mode_map, touch_ctx.current_mode, touch_ctx.carbon);
}

static inline bool
touch_state_is_active(MTTouchState state)
{
	return state == MTTouchStateMakeTouch ||
		   state == MTTouchStateTouching ||
		   state == MTTouchStateLingerInRange;
}

static inline bool
touch_state_is_present(MTTouchState state)
{
	return state != MTTouchStateNotTracking &&
		   state != MTTouchStateOutOfRange;
}

static inline void
get_average_position(MTTouch *data, size_t nFingers, MTPoint *out_pos)
{
	float sum_x = 0.0f;
	float sum_y = 0.0f;
	int count = 0;

	for (size_t i = 0; i < nFingers; ++i) {
		if (touch_state_is_active(data[i].state)) {
			sum_x += data[i].normalizedVector.position.x;
			sum_y += data[i].normalizedVector.position.y;
			++count;
		}
	}

	if (count > 0) {
		out_pos->x = sum_x / (float)count;
		out_pos->y = sum_y / (float)count;
	} else {
		out_pos->x = 0.0f;
		out_pos->y = 0.0f;
	}
}

static inline void
get_two_finger_positions(MTTouch *data, size_t nFingers, MTPoint *pos1, MTPoint *pos2)
{
	int found = 0;
	for (size_t i = 0; i < nFingers && found < 2; ++i) {
		if (touch_state_is_present(data[i].state)) {
			if (found == 0) {
				*pos1 = data[i].normalizedVector.position;
			} else {
				*pos2 = data[i].normalizedVector.position;
			}
			++found;
		}
	}
}

static inline void
get_three_finger_positions(MTTouch *data, size_t nFingers, MTPoint *pos1, MTPoint *pos2, MTPoint *pos3)
{
	int found = 0;
	for (size_t i = 0; i < nFingers && found < 3; ++i) {
		if (touch_state_is_present(data[i].state)) {
			if (found == 0) {
				*pos1 = data[i].normalizedVector.position;
			} else if (found == 1) {
				*pos2 = data[i].normalizedVector.position;
			} else {
				*pos3 = data[i].normalizedVector.position;
			}
			++found;
		}
	}
}

static inline void
reset_tip_tap_state(void)
{
	tip_tap_state = TipTapState_Idle;
	tip_tap_two_finger_pos = (MTPoint){0};
	tip_tap_three_finger_pos_1 = (MTPoint){0};
	tip_tap_three_finger_pos_2 = (MTPoint){0};
	tip_tap_three_finger_pos_3 = (MTPoint){0};
	tip_tap_two_finger_start_time = 0.0;
	tip_tap_three_fingers_start_time = 0.0;
	tip_tap_two_finger_max_delta = 0.0f;
	tip_tap_tap_max_delta = 0.0f;
	tip_tap_gesture_fired = false;
}

static void
touch_callback(MTDeviceRef device, MTTouch *data, size_t nFingers, double timestamp, size_t frame)
{
	(void) device;
	(void) timestamp;
	(void) frame;

	if (nFingers != 3 && nFingers != 4 && nFingers != 5) {
		if (touch_tracking && !touch_fired) {
			double duration = timestamp - touch_start_time;
			if (duration <= THREE_FINGER_TAP_MAX_DURATION &&
				touch_max_delta <= THREE_FINGER_TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_ThreeFingerTap);
			}
		}
		if (touch_tracking_four && !touch_fired_four) {
			double duration = timestamp - touch_start_time_four;
			if (duration <= THREE_FINGER_TAP_MAX_DURATION &&
				touch_max_delta_four <= THREE_FINGER_TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FourFingerTap);
			}
		}
		if (touch_tracking_five && !touch_fired_five) {
			double duration = timestamp - touch_start_time_five;
			if (duration <= THREE_FINGER_TAP_MAX_DURATION &&
				touch_max_delta_five <= THREE_FINGER_TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FiveFingerTap);
			}
		}
		touch_tracking = false;
		touch_fired = false;
		touch_tracking_four = false;
		touch_fired_four = false;
		touch_tracking_five = false;
		touch_fired_five = false;
	}

	if (nFingers <= 3) {
		if (nFingers == 0 && tip_tap_gesture_fired) {
			reset_tip_tap_state();
			return;
		}
		if (tip_tap_gesture_fired && nFingers > 0) {
			return;
		}
		switch (tip_tap_state) {
			case TipTapState_Idle: {
				if (nFingers == 2) {
					get_average_position(data, nFingers, &tip_tap_two_finger_pos);
					tip_tap_two_finger_start_time = timestamp;
					tip_tap_two_finger_max_delta = 0.0f;
					tip_tap_state = TipTapState_TwoFingersDown;
				}
				break;
			}

			case TipTapState_TwoFingersDown: {
				if (nFingers == 0) {
					reset_tip_tap_state();
				} else if (nFingers == 2) {
					MTPoint current_pos;
					get_average_position(data, nFingers, &current_pos);
					float dx = current_pos.x - tip_tap_two_finger_pos.x;
					float dy = current_pos.y - tip_tap_two_finger_pos.y;
					float abs_dx = fabsf(dx);
					float abs_dy = fabsf(dy);
					if (abs_dx > tip_tap_two_finger_max_delta) tip_tap_two_finger_max_delta = abs_dx;
					if (abs_dy > tip_tap_two_finger_max_delta) tip_tap_two_finger_max_delta = abs_dy;
					if (tip_tap_two_finger_max_delta > TIP_TAP_HOLD_MAX_MOVEMENT) {
						reset_tip_tap_state();
					}
				} else if (nFingers == 3) {
					get_three_finger_positions(data, nFingers, &tip_tap_three_finger_pos_1, &tip_tap_three_finger_pos_2, &tip_tap_three_finger_pos_3);
					tip_tap_three_fingers_start_time = timestamp;
					tip_tap_tap_max_delta = 0.0f;
					tip_tap_state = TipTapState_ThreeFingersDown;
				}
				break;
			}

			case TipTapState_ThreeFingersDown: {
				if (nFingers == 0) {
					reset_tip_tap_state();
				} else if (nFingers == 2) {
					reset_tip_tap_state();
				} else if (nFingers == 3) {
					MTPoint pos1, pos2, pos3;
					get_three_finger_positions(data, nFingers, &pos1, &pos2, &pos3);
					float dx1 = pos1.x - tip_tap_three_finger_pos_1.x;
					float dy1 = pos1.y - tip_tap_three_finger_pos_1.y;
					float dx2 = pos2.x - tip_tap_three_finger_pos_2.x;
					float dy2 = pos2.y - tip_tap_three_finger_pos_2.y;
					float dx3 = pos3.x - tip_tap_three_finger_pos_3.x;
					float dy3 = pos3.y - tip_tap_three_finger_pos_3.y;
					float max_dx = fmaxf(fmaxf(fabsf(dx1), fabsf(dx2)), fabsf(dx3));
					float max_dy = fmaxf(fmaxf(fabsf(dy1), fabsf(dy2)), fabsf(dy3));
					float max_delta = fmaxf(max_dx, max_dy);
					if (max_delta > tip_tap_tap_max_delta) tip_tap_tap_max_delta = max_delta;
					if (tip_tap_tap_max_delta > TIP_TAP_TAP_MAX_MOVEMENT ||
						timestamp - tip_tap_three_fingers_start_time > TIP_TAP_MAX_DURATION) {
						float d1 = fabsf(tip_tap_three_finger_pos_1.x - tip_tap_two_finger_pos.x) + fabsf(tip_tap_three_finger_pos_1.y - tip_tap_two_finger_pos.y);
						float d2 = fabsf(tip_tap_three_finger_pos_2.x - tip_tap_two_finger_pos.x) + fabsf(tip_tap_three_finger_pos_2.y - tip_tap_two_finger_pos.y);
						float d3 = fabsf(tip_tap_three_finger_pos_3.x - tip_tap_two_finger_pos.x) + fabsf(tip_tap_three_finger_pos_3.y - tip_tap_two_finger_pos.y);
						MTPoint tap_pos;
						if (d1 > d2 && d1 > d3) {
							tap_pos = tip_tap_three_finger_pos_1;
						} else if (d2 > d3) {
							tap_pos = tip_tap_three_finger_pos_2;
						} else {
							tap_pos = tip_tap_three_finger_pos_3;
						}
						float delta_x = tap_pos.x - tip_tap_two_finger_pos.x;
						float delta_y = tap_pos.y - tip_tap_two_finger_pos.y;
						float abs_dx = fabsf(delta_x);
						float abs_dy = fabsf(delta_y);
						if (abs_dx >= TIP_TAP_MIN_OFFSET &&
							abs_dx > abs_dy * TIP_TAP_AXIS_RATIO) {
							dispatch_gesture(delta_x < 0.0f ? Gesture_TipTapLeft : Gesture_TipTapRight);
							tip_tap_gesture_fired = true;
						}
						reset_tip_tap_state();
					}
				}
				break;
			}
		}
		return;
	}

	if (tip_tap_state != TipTapState_Idle) {
		reset_tip_tap_state();
	}

	float sum_x = 0.0f;
	float sum_y = 0.0f;
	float sum_vx = 0.0f;
	float sum_vy = 0.0f;
	int active = 0;
	int present = 0;

	for (size_t i = 0; i < nFingers; ++i) {
		if (touch_state_is_present(data[i].state)) {
			++present;
		}
		if (!touch_state_is_active(data[i].state)) continue;
		sum_x += data[i].normalizedVector.position.x;
		sum_y += data[i].normalizedVector.position.y;
		sum_vx += data[i].normalizedVector.velocity.x;
		sum_vy += data[i].normalizedVector.velocity.y;
		++active;
	}

	if (nFingers >= 3 && active != (int)nFingers) return;

	float inv = 1.0f / (float) nFingers;
	MTPoint avg_pos = {
		.x = sum_x * inv,
		.y = sum_y * inv
	};

	MTPoint avg_vel = {
		.x = sum_vx * inv,
		.y = sum_vy * inv
	};

	if (nFingers == 3) {
		if (!touch_tracking) {
			touch_tracking = true;
			touch_fired = false;
			touch_start_pos = avg_pos;
			touch_start_time = timestamp;
			touch_max_delta = 0.0f;
			return;
		}

		if (touch_fired) return;

		float delta_x = avg_pos.x - touch_start_pos.x;
		float delta_y = avg_pos.y - touch_start_pos.y;
		float abs_dx = fabsf(delta_x);
		float abs_dy = fabsf(delta_y);
		if (abs_dx > touch_max_delta) touch_max_delta = abs_dx;
		if (abs_dy > touch_max_delta) touch_max_delta = abs_dy;

		if (delta_x < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeLeft);
		} else if (delta_x > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeRight);
		} else if (delta_y < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeDown);
		} else if (delta_y > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeUp);
		}
	} else if (nFingers == 4) {
		if (!touch_tracking_four) {
			touch_tracking_four = true;
			touch_fired_four = false;
			touch_start_pos_four = avg_pos;
			touch_start_time_four = timestamp;
			touch_max_delta_four = 0.0f;
			return;
		}

		if (touch_fired_four) return;

		float delta_x = avg_pos.x - touch_start_pos_four.x;
		float delta_y = avg_pos.y - touch_start_pos_four.y;
		float abs_dx = fabsf(delta_x);
		float abs_dy = fabsf(delta_y);
		if (abs_dx > touch_max_delta_four) touch_max_delta_four = abs_dx;
		if (abs_dy > touch_max_delta_four) touch_max_delta_four = abs_dy;

		if (delta_x < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeLeft);
		} else if (delta_x > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeRight);
		} else if (delta_y < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeDown);
		} else if (delta_y > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeUp);
		}
	} else if (nFingers == 5) {
		if (!touch_tracking_five) {
			touch_tracking_five = true;
			touch_fired_five = false;
			touch_start_pos_five = avg_pos;
			touch_start_time_five = timestamp;
			touch_max_delta_five = 0.0f;
			return;
		}

		if (touch_fired_five) return;

		float delta_x = avg_pos.x - touch_start_pos_five.x;
		float delta_y = avg_pos.y - touch_start_pos_five.y;
		float abs_dx = fabsf(delta_x);
		float abs_dy = fabsf(delta_y);
		if (abs_dx > touch_max_delta_five) touch_max_delta_five = abs_dx;
		if (abs_dy > touch_max_delta_five) touch_max_delta_five = abs_dy;

		if (delta_x < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeLeft);
		} else if (delta_x > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.x > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeRight);
		} else if (delta_y < -THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y < -THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeDown);
		} else if (delta_y > THREE_FINGER_SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * THREE_FINGER_SWIPE_AXIS_RATIO &&
			avg_vel.y > THREE_FINGER_SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * THREE_FINGER_SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeUp);
		}
	}
}

bool touch_begin(struct table *mode_map, struct table *blacklst, struct mode **current_mode, struct carbon_event *carbon)
{
	touch_ctx.mode_map = mode_map;
	touch_ctx.blacklst = blacklst;
	touch_ctx.current_mode = current_mode;
	touch_ctx.carbon = carbon;

	if (!MTDeviceIsAvailable()) {
		warn("no multitouch device found.. touch gestures disabled\n");
		return false;
	}

	CFArrayRef device_list = MTDeviceCreateList();
	if (device_list) {
		CFIndex count = CFArrayGetCount(device_list);
		for (CFIndex i = 0; i < count; ++i) {
			MTDeviceRef candidate = (MTDeviceRef) CFArrayGetValueAtIndex(device_list, i);
			int width = 0;
			int height = 0;
			if (MTDeviceGetSensorSurfaceDimensions(candidate, &width, &height) != noErr) {
				continue;
			}
			if ((width == 23212 && height == 810) ||
				(width == 5152 && height == 9056)) {
                    debug("skipping non-touch multitouch device (touchbar, magic mouse) with dimensions %d x %d\n", width, height);
				continue;
			}
			touch_device = candidate;
			break;
		}
	}

	if (!touch_device) {
		warn("could not initialize multitouch device.. touch gestures disabled\n");
		return false;
	}

	MTRegisterContactFrameCallback(touch_device, touch_callback);
	MTDeviceStart(touch_device, 0);

	return true;
}

void touch_end(void)
{
	if (!touch_device) return;
	MTUnregisterContactFrameCallback(touch_device, touch_callback);
	MTDeviceStop(touch_device);
	MTDeviceRelease(touch_device);
	touch_device = NULL;
}
