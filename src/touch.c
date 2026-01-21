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

struct swipe_state
{
	bool tracking;
	bool fired;
	MTPoint start_pos;
	double start_time;
	float max_delta;
};

static struct touch_context touch_ctx;
static MTDeviceRef touch_device;
static struct swipe_state swipe_two;
static struct swipe_state swipe_three;
static struct swipe_state swipe_four;
static struct swipe_state swipe_five;

static void
reset_touch_state(void)
{
	swipe_two = (struct swipe_state) { 0 };
	swipe_three = (struct swipe_state) { 0 };
	swipe_four = (struct swipe_state) { 0 };
	swipe_five = (struct swipe_state) { 0 };
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

static inline void
handle_swipe_multi(struct swipe_state *state, double timestamp, MTPoint avg_pos, MTPoint avg_vel,
			    uint32_t gesture_left, uint32_t gesture_right,
			    uint32_t gesture_down, uint32_t gesture_up)
{
	if (!state->tracking) {
		state->tracking = true;
		state->fired = false;
		state->start_pos = avg_pos;
		state->start_time = timestamp;
		state->max_delta = 0.0f;
		return;
	}

	if (state->fired) return;

	float delta_x = avg_pos.x - state->start_pos.x;
	float delta_y = avg_pos.y - state->start_pos.y;
	float abs_dx = fabsf(delta_x);
	float abs_dy = fabsf(delta_y);
	if (abs_dx > state->max_delta) state->max_delta = abs_dx;
	if (abs_dy > state->max_delta) state->max_delta = abs_dy;

	if (delta_x < -SWIPE_MIN_DISTANCE &&
		fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
		avg_vel.x < -SWIPE_MIN_VELOCITY &&
		fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
		state->fired = true;
		dispatch_gesture(gesture_left);
	} else if (delta_x > SWIPE_MIN_DISTANCE &&
		fabsf(delta_x) > fabsf(delta_y) * SWIPE_AXIS_RATIO &&
		avg_vel.x > SWIPE_MIN_VELOCITY &&
		fabsf(avg_vel.x) > fabsf(avg_vel.y) * SWIPE_AXIS_RATIO) {
		state->fired = true;
		dispatch_gesture(gesture_right);
	} else if (delta_y < -SWIPE_MIN_DISTANCE &&
		fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
		avg_vel.y < -SWIPE_MIN_VELOCITY &&
		fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
		state->fired = true;
		dispatch_gesture(gesture_down);
	} else if (delta_y > SWIPE_MIN_DISTANCE &&
		fabsf(delta_y) > fabsf(delta_x) * SWIPE_AXIS_RATIO &&
		avg_vel.y > SWIPE_MIN_VELOCITY &&
		fabsf(avg_vel.y) > fabsf(avg_vel.x) * SWIPE_AXIS_RATIO) {
		state->fired = true;
		dispatch_gesture(gesture_up);
	}
}

static void
touch_callback(MTDeviceRef device, MTTouch *data, size_t nFingers, double timestamp, size_t frame)
{
	(void) device;
	(void) timestamp;
	(void) frame;

	if (nFingers != 2 && nFingers != 3 && nFingers != 4 && nFingers != 5) { // Fingers lifted
		if (swipe_two.tracking && !swipe_two.fired) {
			double duration = timestamp - swipe_two.start_time;
			if (duration <= TAP_MAX_DURATION &&
				swipe_two.max_delta <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_TwoFingerTap);
			}
		}
		if (swipe_three.tracking && !swipe_three.fired) {
			double duration = timestamp - swipe_three.start_time;
			if (duration <= TAP_MAX_DURATION &&
				swipe_three.max_delta <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_ThreeFingerTap);
			}
		}
		if (swipe_four.tracking && !swipe_four.fired) {
			double duration = timestamp - swipe_four.start_time;
			if (duration <= TAP_MAX_DURATION &&
				swipe_four.max_delta <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FourFingerTap);
			}
		}
		if (swipe_five.tracking && !swipe_five.fired) {
			double duration = timestamp - swipe_five.start_time;
			if (duration <= TAP_MAX_DURATION &&
				swipe_five.max_delta <= TAP_MAX_MOVEMENT) {
				dispatch_gesture(Gesture_FiveFingerTap);
			}
		}
		reset_touch_state();
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
		swipe_two.tracking = false;
		swipe_two.fired = false;
	}

	if (nFingers == 2) {
		handle_swipe_multi(&swipe_two, timestamp, avg_pos, avg_vel,
				    Gesture_TwoFingerSwipeLeft, Gesture_TwoFingerSwipeRight,
				    Gesture_TwoFingerSwipeDown, Gesture_TwoFingerSwipeUp);
	} else if (nFingers == 3) {
		handle_swipe_multi(&swipe_three, timestamp, avg_pos, avg_vel,
				     Gesture_ThreeFingerSwipeLeft, Gesture_ThreeFingerSwipeRight,
				     Gesture_ThreeFingerSwipeDown, Gesture_ThreeFingerSwipeUp);
	} else if (nFingers == 4) {
		handle_swipe_multi(&swipe_four, timestamp, avg_pos, avg_vel,
				     Gesture_FourFingerSwipeLeft, Gesture_FourFingerSwipeRight,
				     Gesture_FourFingerSwipeDown, Gesture_FourFingerSwipeUp);
	} else if (nFingers == 5) {
		handle_swipe_multi(&swipe_five, timestamp, avg_pos, avg_vel,
				     Gesture_FiveFingerSwipeLeft, Gesture_FiveFingerSwipeRight,
				     Gesture_FiveFingerSwipeDown, Gesture_FiveFingerSwipeUp);
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
