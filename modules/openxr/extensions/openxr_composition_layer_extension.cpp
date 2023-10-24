/**************************************************************************/
/*  openxr_composition_layer_extension.cpp                                */
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

#include "openxr_composition_layer_extension.h"

////////////////////////////////////////////////////////////////////////////
// OpenXRCompositionLayerExtension

OpenXRCompositionLayerExtension *OpenXRCompositionLayerExtension::singleton = nullptr;

OpenXRCompositionLayerExtension *OpenXRCompositionLayerExtension::get_singleton() {
	return singleton;
}

OpenXRCompositionLayerExtension::OpenXRCompositionLayerExtension() {
	singleton = this;
}

OpenXRCompositionLayerExtension::~OpenXRCompositionLayerExtension() {
	singleton = nullptr;
}

HashMap<String, bool *> OpenXRCompositionLayerExtension::get_requested_extensions() {
	HashMap<String, bool *> request_extensions;

	request_extensions[XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME] = &available[COMPOSITION_LAYER_EQUIRECT_EXT];
	request_extensions[XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME] = &available[COMPOSITION_LAYER_IMAGE_LAYOUT_EXT];

	return request_extensions;
}

bool OpenXRCompositionLayerExtension::is_available(CompositionLayerExtensions p_which) {
	ERR_FAIL_INDEX_V(p_which, COMPOSITION_LAYER_EXT_MAX, false);

	return available[p_which];
}

////////////////////////////////////////////////////////////////////////////
// ViewportCompositionLayerProvider

ViewportCompositionLayerProvider::ViewportCompositionLayerProvider() {
	openxr_api = OpenXRAPI::get_singleton();
	composition_layer_extension = OpenXRCompositionLayerExtension::get_singleton();
#ifdef ANDROID_ENABLED
	android_surface_swapchain_extension = OpenXRAndroidSurfaceSwapchainExtension::get_singleton();
#endif
	// Clear this.
	setup_for_type(XR_TYPE_UNKNOWN);
}

ViewportCompositionLayerProvider::~ViewportCompositionLayerProvider() {
	if (swapchain_info.swapchain != XR_NULL_HANDLE) {
		openxr_api->free_swapchain(swapchain_info);
	}
}

bool ViewportCompositionLayerProvider::is_supported() {
	if (openxr_api == nullptr || composition_layer_extension == nullptr) {
		// OpenXR not initialised or we're in the editor?
		return false;
	}

	switch (composition_layer.type) {
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
			return composition_layer_extension->is_available(OpenXRCompositionLayerExtension::COMPOSITION_LAYER_EQUIRECT_EXT);
		} break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD: {
			return true;
		} break;
		default: {
			return false;
		} break;
	}
}

void ViewportCompositionLayerProvider::setup_for_type(XrStructureType p_type) {
	// Note, we setup our type fully even if it's not supported on the current platform.
	// This allows us to set it up in the editor.
	switch (p_type) {
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
			memset(&equirect_layer, 0, sizeof(XrCompositionLayerEquirect2KHR));
			equirect_layer.type = XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR;
			equirect_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
			equirect_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

			// These needs to become configurable
			equirect_layer.pose.orientation.x = 0.0;
			equirect_layer.pose.orientation.y = 0.0;
			equirect_layer.pose.orientation.z = 0.0;
			equirect_layer.pose.orientation.w = 1.0;
			equirect_layer.pose.position.x = 0.0; // this should probably be centered on the player?? or we leave it up to the user
			equirect_layer.pose.position.y = 1.5;
			equirect_layer.pose.position.z = 0.0;
			equirect_layer.radius = 1.0;
			equirect_layer.centralHorizontalAngle = 90.0 * Math_PI / 180.0;
			equirect_layer.upperVerticalAngle = 25.0 * Math_PI / 180.0;
			equirect_layer.lowerVerticalAngle = 25.0 * Math_PI / 180.0;
		} break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD: {
			memset(&quad_layer, 0, sizeof(XrCompositionLayerQuad));
			quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
			quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
			quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

			quad_layer.pose.orientation.x = 0.0;
			quad_layer.pose.orientation.y = 0.0;
			quad_layer.pose.orientation.z = 0.0;
			quad_layer.pose.orientation.w = 1.0;
			quad_layer.pose.position.x = 0.0;
			quad_layer.pose.position.y = 1.5;
			quad_layer.pose.position.z = -2.0;
			quad_layer.size.width = 1.0;
			quad_layer.size.height = 1.0;
		} break;
		default: {
			memset(&composition_layer, 0, sizeof(XrCompositionLayerBaseHeader));
			composition_layer.type = XR_TYPE_UNKNOWN;
		} break;
	}
}

void ViewportCompositionLayerProvider::set_transform(const Transform3D& p_transform_3d) {
	auto rot = p_transform_3d.get_basis().get_rotation_quaternion();
	auto pos = p_transform_3d.get_origin();

	switch (composition_layer.type) {
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
			equirect_layer.pose.orientation.x = rot.x;
			equirect_layer.pose.orientation.y = rot.y;
			equirect_layer.pose.orientation.z = rot.z;
			equirect_layer.pose.orientation.w = rot.w;
			equirect_layer.pose.position.x = pos.x;
			equirect_layer.pose.position.y = pos.y;
			equirect_layer.pose.position.z = pos.z;
		} break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD: {
			quad_layer.pose.orientation.x = rot.x;
			quad_layer.pose.orientation.y = rot.y;
			quad_layer.pose.orientation.z = rot.z;
			quad_layer.pose.orientation.w = rot.w;
			quad_layer.pose.position.x = pos.x;
			quad_layer.pose.position.y = pos.y;
			quad_layer.pose.position.z = pos.z;
		} break;
		default: {
		} break;
	}
}

void ViewportCompositionLayerProvider::set_size_2d_meters(float p_width_m, float p_height_m) {
	switch (composition_layer.type) {
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
			// Only applies to QUAD
		} break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD: {
			quad_layer.size.width = p_width_m;
			quad_layer.size.height = p_height_m;
		} break;
		default: {
		} break;
	}
}

OpenXRCompositionLayerProvider::OrderedCompositionLayer ViewportCompositionLayerProvider::get_composition_layer() {
	if (openxr_api == nullptr || composition_layer_extension == nullptr) {
		// OpenXR not initialised or we're in the editor?
		return { nullptr, 0 };
	}

	if (!is_supported()) {
		// Selected type is not supported, ignore our layer.
		return { nullptr, 0 };
	}

	if (swapchain_info.swapchain == XR_NULL_HANDLE) {
		// Don't have a swapchain to display? Ignore our layer.
		return { nullptr, 0 };
	}

	if (swapchain_info.image_acquired) {
		openxr_api->release_image(swapchain_info);
	}

	switch (composition_layer.type) {
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR: {
			// Setup additional info for swapchain
			equirect_layer.subImage.swapchain = swapchain_info.swapchain;
			equirect_layer.subImage.imageArrayIndex = swapchain_info.image_index;
			equirect_layer.subImage.imageRect.offset.x = 0;
			equirect_layer.subImage.imageRect.offset.y = 0;
			equirect_layer.subImage.imageRect.extent.width = width;
			equirect_layer.subImage.imageRect.extent.height = height;

			equirect_layer.space = openxr_api->get_play_space();

			return { &composition_layer, sort_order };
		} break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD: {
			// HACK: Hardcode the flip byte
			if (composition_layer_extension->is_available(OpenXRCompositionLayerExtension::COMPOSITION_LAYER_IMAGE_LAYOUT_EXT)) {
				composition_layout_ext.type = XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB;
				composition_layout_ext.next = nullptr;
				composition_layout_ext.flags = XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB;
				quad_layer.next = &composition_layout_ext;
			}

			// Setup additional info for swapchain
			quad_layer.subImage.swapchain = swapchain_info.swapchain;
			quad_layer.subImage.imageArrayIndex = swapchain_info.image_index;
			quad_layer.subImage.imageRect.offset.x = 0;
			quad_layer.subImage.imageRect.offset.y = 0;
			quad_layer.subImage.imageRect.extent.width = width;
			quad_layer.subImage.imageRect.extent.height = height;

			quad_layer.space = openxr_api->get_play_space();

			return { &composition_layer, sort_order };
		} break;
		default: {
			return { nullptr, 0 };
		} break;
	}
}

bool ViewportCompositionLayerProvider::update_swapchain(uint32_t p_width, uint32_t p_height, bool p_android) {
	if (openxr_api == nullptr || composition_layer_extension == nullptr) {
		// OpenXR not initialised or we're in the editor?
		return false;
	}

	if (!is_supported()) {
		// Selected type is not supported?
		return false;
	}

	// See if our current swapchain is outdated
	if (swapchain_info.swapchain != XR_NULL_HANDLE) {
		if (width == p_width && height == p_height && !swapchain_info.android_surface_swapchain) {
			// We're all good! Just acquire it.
			openxr_api->acquire_image(swapchain_info);
			return true;
		}

		if (swapchain_info.android_surface_swapchain) {
#ifdef ANDROID_ENABLED
			print_line("Freeing the existing swapchain. Handle coming next.");
			android_surface_swapchain_extension->free_swapchain(swapchain_info);
#endif
		} else {
			openxr_api->free_swapchain(swapchain_info);
		}
	}

	// Create our new swap chain
	swapchain_info.android_surface_swapchain = p_android;
	int64_t swapchain_format = openxr_api->get_color_swapchain_format();
	if (!p_android && !openxr_api->create_swapchain(XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT, swapchain_format, p_width, p_height, 3, 1, swapchain_info.swapchain, &swapchain_info.swapchain_graphics_data)) {
		width = 0;
		height = 0;
		return false;
	}

#ifdef ANDROID_ENABLED
	if (p_android && !android_surface_swapchain_extension->create_android_surface_swapchain(
			XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT,
			p_width, p_height, swapchain_info.swapchain, &swapchain_info.swapchain_graphics_data)) {
		width = 0;
		height = 0;
		return false;
	}
#endif

	// Acquire our image so we can start rendering into it
	openxr_api->acquire_image(swapchain_info);

	width = p_width;
	height = p_height;
	return true;
}

void ViewportCompositionLayerProvider::free_swapchain() {
	if (swapchain_info.swapchain != XR_NULL_HANDLE) {
		openxr_api->free_swapchain(swapchain_info);
	}

	width = 0;
	height = 0;
}

RID ViewportCompositionLayerProvider::get_image() {
	if (openxr_api == nullptr) {
		return RID();
	}

	return openxr_api->get_image(swapchain_info);
}

int ViewportCompositionLayerProvider::get_android_surface_handle() {
	if (openxr_api == nullptr) {
		return -1;
	}

	if (!swapchain_info.android_surface_swapchain) {
		return -1;
	}

	return *((int*) swapchain_info.swapchain_graphics_data);
}
