/**************************************************************************/
/*  openxr_android_surface_swapchain_extension.cpp                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "openxr_android_surface_swapchain_extension.h"

#include "../openxr_api.h"

#include "java_godot_wrapper.h"
#include "os_android.h"
#include "thread_jandroid.h"

#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace {

static int deliverSurfaceToJava(jobject surface) {
		JNIEnv *env = get_jni_env();

		// Java lookups
		jclass surfaceStorageClass = env->FindClass("org/godotengine/godot/xr/SurfaceStorage");
		if (surfaceStorageClass == nullptr) {
			print_line("JNI: Failed to find SurfaceStorage class");
			return -1;
		}
		jmethodID storeSurfaceMid = env->GetStaticMethodID(surfaceStorageClass, "storeSurfaceForSwapchain", "(Landroid/view/Surface;)I");
		if (storeSurfaceMid == nullptr) {
			print_line("JNI: Failed to find storeSurfaceForSwapchain method");
			return -1;
		}

		// Execute
		jint handle = env->CallStaticIntMethod(surfaceStorageClass, storeSurfaceMid, surface);
		if (env->ExceptionOccurred()) {
			print_line("JNI: Exception ocurred!");
			return -1;
		}

		print_line("Success! Stored new surface with handle ", (int) handle);
		return handle;
}

static void releaseSurfaceFromJava(int handle) {
		JNIEnv *env = get_jni_env();

		// Java lookups
		jclass surfaceStorageClass = env->FindClass("org/godotengine/godot/xr/SurfaceStorage");
		if (surfaceStorageClass == nullptr) {
			print_line("JNI: Failed to find SurfaceStorage class");
			return;
		}
		jmethodID releaseSurfaceMid = env->GetStaticMethodID(surfaceStorageClass, "releaseSurface", "(I)V");
		if (releaseSurfaceMid == nullptr) {
			print_line("JNI: Failed to find storeSurfaceForSwapchain method");
			return;
		}

		// Execute
		env->CallStaticVoidMethod(surfaceStorageClass, releaseSurfaceMid, handle);
		if (env->ExceptionOccurred()) {
			print_line("JNI: Exception ocurred!");
			return;
		}

		print_line("Success! Deleted surface with handle ", (int) handle);
}

} // anonymous namespace

OpenXRAndroidSurfaceSwapchainExtension *OpenXRAndroidSurfaceSwapchainExtension::singleton = nullptr;

OpenXRAndroidSurfaceSwapchainExtension *OpenXRAndroidSurfaceSwapchainExtension::get_singleton() {
	return singleton;
}

OpenXRAndroidSurfaceSwapchainExtension::OpenXRAndroidSurfaceSwapchainExtension() {
	singleton = this;
}

HashMap<String, bool *> OpenXRAndroidSurfaceSwapchainExtension::get_requested_extensions() {
	HashMap<String, bool *> request_extensions;

	request_extensions[XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME] = &android_surface_swapchain_extension_available;
	request_extensions[XR_FB_ANDROID_SURFACE_SWAPCHAIN_CREATE_EXTENSION_NAME] = &android_surface_swapchain_create_extension_available;

	return request_extensions;
}

OpenXRAndroidSurfaceSwapchainExtension::~OpenXRAndroidSurfaceSwapchainExtension() {
	singleton = nullptr;
}

void OpenXRAndroidSurfaceSwapchainExtension::on_instance_created(const XrInstance p_instance) {
	openxr_api = OpenXRAPI::get_singleton();
	EXT_TRY_INIT_XR_FUNC(xrDestroySwapchain);
	if (XR_FAILED(EXT_TRY_INIT_XR_FUNC(xrCreateSwapchainAndroidSurfaceKHR))) {
		// android_surface_swapchain_create_extension_available not supported on this platform
		return;
	}
	android_surface_swapchain_create_extension_available = true;
}

bool OpenXRAndroidSurfaceSwapchainExtension::create_android_surface_swapchain(
		XrSwapchainUsageFlags p_usage_flags,
		uint32_t p_width,
		uint32_t p_height,
		XrSwapchain &r_swapchain,
		void **r_swapchain_graphics_data) {
	ERR_FAIL_COND_V(openxr_api == nullptr, false);
	ERR_FAIL_COND_V(!android_surface_swapchain_create_extension_available, false);
	ERR_FAIL_COND_V(!android_surface_swapchain_extension_available, false);

	XrAndroidSurfaceSwapchainCreateInfoFB swapchainExt;
	swapchainExt.type = XR_TYPE_ANDROID_SURFACE_SWAPCHAIN_CREATE_INFO_FB;
	swapchainExt.createFlags = 0;
	swapchainExt.createFlags |= XR_ANDROID_SURFACE_SWAPCHAIN_SYNCHRONOUS_BIT_FB;

	XrSwapchainCreateInfo swapchain_create_info = {
		XR_TYPE_SWAPCHAIN_CREATE_INFO, // type
		&swapchainExt, // next
		0, // createFlags
		p_usage_flags, // usageFlags
		0, // format (required by xrCreateSwapchainAndroidSurfaceKHR)
		0, // sampleCount (required by xrCreateSwapchainAndroidSurfaceKHR)
		p_width, // width
		p_height, // height
		0, // faceCount (required by xrCreateSwapchainAndroidSurfaceKHR)
		0, // arraySize (required by xrCreateSwapchainAndroidSurfaceKHR)
		0 // mipCount (required by xrCreateSwapchainAndroidSurfaceKHR)
	};

	jobject surfaceObj;
	XrSwapchain new_swapchain;
	XrResult result = xrCreateSwapchainAndroidSurfaceKHR(openxr_api->get_session(), &swapchain_create_info, &new_swapchain, &surfaceObj);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get swapchain [", openxr_api->get_error_string(result), "]");
		return false;
	}

	int handle = deliverSurfaceToJava(surfaceObj);
	if (handle == -1) {
		print_line("OpenXR: Failed to store surface");
		return false;
	}

	r_swapchain = new_swapchain;
	int *pointerStorage = (int*) malloc(sizeof(int32_t));
	*pointerStorage = handle;
	*r_swapchain_graphics_data = pointerStorage;

	return true;
}

void OpenXRAndroidSurfaceSwapchainExtension::free_swapchain(OpenXRAPI::OpenXRSwapChainInfo &p_swapchain) {
	// Clean up Java references
	releaseSurfaceFromJava(*((int*)p_swapchain.swapchain_graphics_data));

	// Clean up actual resource
	if (p_swapchain.swapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(p_swapchain.swapchain);
		p_swapchain.swapchain = XR_NULL_HANDLE;
	}

	// Clean up handle
 	free(p_swapchain.swapchain_graphics_data);
	p_swapchain.swapchain_graphics_data = nullptr;
}