#include <math.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Carbon/Carbon.h>

#include "multitouch.h"
#include "touch.h"
#include "log.h"
#include "hotkey.h"
#include "carbon.h"
#include "hashtable.h"

#define SWIPE_MIN_DISTANCE 0.05f
#define SWIPE_MIN_VELOCITY 0.05f
#define SWIPE_AXIS_RATIO   1.45f
#define TAP_MAX_DURATION   0.18
#define TAP_MAX_MOVEMENT   0.04f

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
static bool touch_tracking_two;
static bool touch_fired_two;
static MTPoint touch_start_pos_two;
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

static void
reset_touch_state(void)
{
	touch_tracking = false;
	touch_fired = false;
	touch_start_pos = (MTPoint) { 0 };
	touch_start_time = 0.0;
	touch_max_delta = 0.0f;

	touch_tracking_two = false;
	touch_fired_two = false;
	touch_start_pos_two = (MTPoint) { 0 };

	touch_tracking_four = false;
	touch_fired_four = false;
	touch_start_pos_four = (MTPoint) { 0 };
	touch_start_time_four = 0.0;
	touch_max_delta_four = 0.0f;

	touch_tracking_five = false;
	touch_fired_five = false;
	touch_start_pos_five = (MTPoint) { 0 };
	touch_start_time_five = 0.0;
	touch_max_delta_five = 0.0f;
}

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

static void
touch_callback(MTDeviceRef device, MTTouch *data, size_t nFingers, double timestamp, size_t frame)
{
	(void) device;
	(void) timestamp;
	(void) frame;

	if (nFingers != 2 && nFingers != 3 && nFingers != 4 && nFingers != 5) {
		if (touch_tracking && !touch_fired) {
			double duration = timestamp - touch_start_time;
			if (duration <= TAP_MAX_DURATION &&
				touch_max_delta <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_ThreeFingerTap);
			}
		}
		if (touch_tracking_four && !touch_fired_four) {
			double duration = timestamp - touch_start_time_four;
			if (duration <= TAP_MAX_DURATION &&
				touch_max_delta_four <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FourFingerTap);
			}
		}
		if (touch_tracking_five && !touch_fired_five) {
			double duration = timestamp - touch_start_time_five;
			if (duration <= TAP_MAX_DURATION &&
				touch_max_delta_five <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FiveFingerTap);
			}
		}
		touch_tracking = false;
		touch_fired = false;
		touch_tracking_two = false;
		touch_fired_two = false;
		touch_tracking_four = false;
		touch_fired_four = false;
		touch_tracking_five = false;
		touch_fired_five = false;
		return;
	}

	float sum_x = 0.0f;
	float sum_y = 0.0f;
	float sum_vx = 0.0f;
	float sum_vy = 0.0f;
	int active = 0;

	for (size_t i = 0; i < nFingers; ++i) {
		if (!touch_state_is_active(data[i].state)) continue;
		sum_x += data[i].normalizedVector.position.x;
		sum_y += data[i].normalizedVector.position.y;
		sum_vx += data[i].normalizedVector.velocity.x;
		sum_vy += data[i].normalizedVector.velocity.y;
		++active;
	}

	if (nFingers >= 2 && active != (int)nFingers) return;

	float inv = 1.0f / (float) nFingers;
	MTPoint avg_pos = {
		.x = sum_x * inv,
		.y = sum_y * inv
	};

	MTPoint avg_vel = {
		.x = sum_vx * inv,
		.y = sum_vy * inv
	};

	if (nFingers != 2) {
		touch_tracking_two = false;
		touch_fired_two = false;
	}

	if (nFingers == 2) {
		if (!touch_tracking_two) {
			touch_tracking_two = true;
			touch_fired_two = false;
			touch_start_pos_two = avg_pos;
			return;
		}

		if (touch_fired_two) return;

		float delta_x = avg_pos.x - touch_start_pos_two.x;
		float delta_y = avg_pos.y - touch_start_pos_two.y;

		if (delta_x < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_two = true;
			dispatch_gesture(Gesture_TwoFingerSwipeLeft);
		} else if (delta_x > SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_two = true;
			dispatch_gesture(Gesture_TwoFingerSwipeRight);
		} else if (delta_y < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
			touch_fired_two = true;
			dispatch_gesture(Gesture_TwoFingerSwipeDown);
		} else if (delta_y > SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
			touch_fired_two = true;
			dispatch_gesture(Gesture_TwoFingerSwipeUp);
		}
	} else if (nFingers == 3) {
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

		if (delta_x < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeLeft);
		} else if (delta_x > SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeRight);
		} else if (delta_y < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
			touch_fired = true;
			dispatch_gesture(Gesture_ThreeFingerSwipeDown);
		} else if (delta_y > SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
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

		if (delta_x < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeLeft);
		} else if (delta_x > SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeRight);
		} else if (delta_y < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
			touch_fired_four = true;
			dispatch_gesture(Gesture_FourFingerSwipeDown);
		} else if (delta_y > SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
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

		if (delta_x < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeLeft);
		} else if (delta_x > SWIPE_MIN_DISTANCE &&
			fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
			avg_vel.x > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeRight);
		} else if (delta_y < -SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y < -SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
			touch_fired_five = true;
			dispatch_gesture(Gesture_FiveFingerSwipeDown);
		} else if (delta_y > SWIPE_MIN_DISTANCE &&
			fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
			avg_vel.y > SWIPE_MIN_VELOCITY &&
			fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
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
	if (!MTDeviceIsValid(touch_device)) {
		warn("failed to register multitouch callback.. touch gestures disabled\n");
		MTDeviceRelease(touch_device);
		touch_device = NULL;
		reset_touch_state();
		return false;
	}

	OSStatus start_status = MTDeviceStart(touch_device, 0);
	if (start_status != noErr) {
		warn("could not start multitouch device (%d).. touch gestures disabled\n", (int) start_status);
		MTUnregisterContactFrameCallback(touch_device, touch_callback);
		MTDeviceRelease(touch_device);
		touch_device = NULL;
		reset_touch_state();
		return false;
	}

	if (device_list) CFRelease(device_list);
	return true;
}

void touch_end(void)
{
	reset_touch_state();
	if (!touch_device) return;
	MTUnregisterContactFrameCallback(touch_device, touch_callback);
	MTDeviceStop(touch_device);
	MTDeviceRelease(touch_device);
	touch_device = NULL;
}
