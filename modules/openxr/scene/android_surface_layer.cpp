/**************************************************************************/
/*  android_surface_layer.cpp                                             */
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

#include "../extensions/openxr_composition_layer_extension.h"
#include "../openxr_api.h"

#include "android_surface_layer.h"

AndroidSurfaceLayer::AndroidSurfaceLayer() {
	print_line("AndroidSurfaceLayer!");
	openxr_api = OpenXRAPI::get_singleton();
	openxr_layer_provider = memnew(ViewportCompositionLayerProvider);
	set_notify_transform(true);

	if (openxr_api != nullptr) {
		print_line("Setting up!");
		openxr_layer_provider->setup_for_type(XR_TYPE_COMPOSITION_LAYER_QUAD);
		openxr_layer_provider->update_swapchain(128, 128, true);
		openxr_api->register_composition_layer_provider(openxr_layer_provider);
	} else {
		print_line("No OpenXR available!");
	}
}

AndroidSurfaceLayer::~AndroidSurfaceLayer() {
	if (openxr_layer_provider != nullptr) {
		memdelete(openxr_layer_provider);
		openxr_layer_provider = nullptr;
	}

	if (openxr_api != nullptr) {
		// Remove our composition layer provider from our OpenXR API
		openxr_api->unregister_composition_layer_provider(openxr_layer_provider);
		openxr_layer_provider->free_swapchain();
	}
}

void AndroidSurfaceLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_supported"), &AndroidSurfaceLayer::is_supported);
	ClassDB::bind_method(D_METHOD("get_android_surface_handle"), &AndroidSurfaceLayer::get_android_surface_handle);
	ClassDB::bind_method(D_METHOD("set_layer_resolution"), &AndroidSurfaceLayer::set_layer_resolution);
	ClassDB::bind_method(D_METHOD("set_size_2d_meters"), &AndroidSurfaceLayer::set_size_2d_meters);
}

bool AndroidSurfaceLayer::is_supported() {
#ifdef ANDROID_ENABLED
	return openxr_layer_provider->is_supported();
#else
	return false;
#endif
}

int AndroidSurfaceLayer::get_android_surface_handle() {
	return openxr_layer_provider->get_android_surface_handle();
}

void AndroidSurfaceLayer::set_layer_resolution(int widthPx, int heightPx) {
	openxr_layer_provider->update_swapchain(widthPx, heightPx, true);
}

void AndroidSurfaceLayer::set_size_2d_meters(float p_width_m, float p_height_m) {
	openxr_layer_provider->set_size_2d_meters(p_width_m, p_height_m);
}

void AndroidSurfaceLayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSFORM_CHANGED: {
			openxr_layer_provider->set_transform(get_global_transform());
			break;
		}
	}
}
