// Copyright (C) 2009  Fajran Iman Rusadi <fajran@gmail.com>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#ifndef MULTITOUCH_H
#define MULTITOUCH_H

#ifdef __OBJC__
#include <Cocoa/Cocoa.h>
#endif
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		float x;
		float y;
	} MTPoint;
	
	typedef struct {
		MTPoint position;
		MTPoint velocity;
	} MTVector;
	
	enum {
		MTTouchStateNotTracking = 0,
		MTTouchStateStartInRange = 1,
		MTTouchStateHoverInRange = 2,
		MTTouchStateMakeTouch = 3,
		MTTouchStateTouching = 4,
		MTTouchStateBreakTouch = 5,
		MTTouchStateLingerInRange = 6,
		MTTouchStateOutOfRange = 7
	};
	typedef uint32_t MTTouchState;
	
	typedef struct {
		int32_t frame;
		double timestamp;
		int32_t pathIndex;	// "P" (~transducerIndex)
		MTTouchState state;
		int32_t fingerID;	// "F" (~identity)
		int32_t handID;		// "H" (always 1)
		MTVector normalizedVector;
		float zTotal;		// "ZTot" (~quality, multiple of 1/8 between 0 and 1)
		int32_t field9;		// always 0
		float angle;
		float majorAxis;
		float minorAxis;
		MTVector absoluteVector;	// "mm"
		int32_t field14;	// always 0
		int32_t field15;	// always 0
		float zDensity;		// "ZDen" (~density)
	} MTTouch;
	
	typedef void* MTDeviceRef;
	
	double MTAbsoluteTimeGetCurrent();
	bool MTDeviceIsAvailable();		// true if can create default device
	
	CFArrayRef MTDeviceCreateList();		// creates for driver types 0, 1, 4, 2, 3
	MTDeviceRef MTDeviceCreateDefault();
	MTDeviceRef MTDeviceCreateFromDeviceID(int64_t);
	MTDeviceRef MTDeviceCreateFromService(io_service_t);
	MTDeviceRef MTDeviceCreateFromGUID(uuid_t);		// GUID's compared by pointer, not value!
	void MTDeviceRelease(MTDeviceRef);
	
	CFRunLoopSourceRef MTDeviceCreateMultitouchRunLoopSource(MTDeviceRef);
	OSStatus MTDeviceScheduleOnRunLoop(MTDeviceRef, CFRunLoopRef, CFStringRef);
	
	OSStatus MTDeviceStart(MTDeviceRef, int);
	OSStatus MTDeviceStop(MTDeviceRef);
	bool MTDeviceIsRunning(MTDeviceRef);
	
	bool MTDeviceIsValid(MTDeviceRef);
	bool MTDeviceIsBuiltIn(MTDeviceRef) __attribute__ ((weak_import));	// no 10.5
	bool MTDeviceIsOpaqueSurface(MTDeviceRef);
	io_service_t MTDeviceGetService(MTDeviceRef);
	OSStatus MTDeviceGetSensorSurfaceDimensions(MTDeviceRef, int*, int*);
	OSStatus MTDeviceGetFamilyID(MTDeviceRef, int*);
	OSStatus MTDeviceGetDeviceID(MTDeviceRef, uint64_t*) __attribute__ ((weak_import));	// no 10.5
	OSStatus MTDeviceGetDriverType(MTDeviceRef, int*);
	OSStatus MTDeviceGetActualType(MTDeviceRef, int*);
	OSStatus MTDeviceGetGUID(MTDeviceRef, uuid_t*);
    void MTDeviceGetTransportMethod(MTDeviceRef, int *);
	
	typedef void (*MTFrameCallbackFunction)(MTDeviceRef device,
	MTTouch touches[], size_t numTouches,
	double timestamp, size_t frame);
	void MTRegisterContactFrameCallback(MTDeviceRef, MTFrameCallbackFunction);
	void MTUnregisterContactFrameCallback(MTDeviceRef, MTFrameCallbackFunction);
	
	typedef void (*MTPathCallbackFunction)(MTDeviceRef device, long pathID, long state, MTTouch* touch);
	//MTPathCallbackFunction MTPathPrintCallback;
	void MTRegisterPathCallback(MTDeviceRef, MTPathCallbackFunction);
	void MTUnregisterPathCallback(MTDeviceRef, MTPathCallbackFunction);
	
	/*
	 // callbacks never called (need different flags?)
	 typedef void (*MTImageCallbackFunction)(MTDeviceRef, void*, void*);
	 MTImageCallbackFunction MTImagePrintCallback;
	 void MTRegisterMultitouchImageCallback(MTDeviceRef, MTImageCallbackFunction);
	 */
	
	/*
	 // these log error
	 void MTVibratorRunForDuration(MTDeviceRef,long);
	 void MTVibratorStop(MTDeviceRef);
	 */
	
	inline const char*
	MTTouchStateName(MTTouchState ps) {
		switch (ps) {
			case MTTouchStateNotTracking: return "NotTracking" ;
			case MTTouchStateStartInRange: return "StartInRange" ;
			case MTTouchStateHoverInRange: return "HoverInRange" ;
			case MTTouchStateMakeTouch: return "MakeTouch" ;
			case MTTouchStateTouching: return "Touching" ;
			case MTTouchStateBreakTouch: return "BreakTouch" ;
			case MTTouchStateLingerInRange: return "LingerInRange" ;
			case MTTouchStateOutOfRange: return "OutOfRange" ;
			default: return "Unknown" ;
		}
	}
	
#ifdef __cplusplus
}
#endif

#endif
