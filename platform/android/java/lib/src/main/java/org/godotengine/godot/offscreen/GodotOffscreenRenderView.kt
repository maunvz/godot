/**************************************************************************/
/*  GodotOffscreenRenderView.kt                                           */
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

package org.godotengine.godot.offscreen

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.util.Log
import android.view.Surface
import android.view.SurfaceView
import org.godotengine.godot.GodotLib
import org.godotengine.godot.GodotRenderView
import org.godotengine.godot.gl.GodotRenderer
import org.godotengine.godot.input.GodotInputHandler
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * A [GodotRenderView] with no [android.view.View] and no window: it drives Godot's
 * OpenGL ES (`opengl3` / Compatibility) renderer from its own thread, on an EGL context
 * backed by a pbuffer. This lets Godot run inside a [android.app.Service] (for example a
 * foreground service) with no Activity, no SurfaceView and no SurfaceControlViewHost.
 *
 * Godot's main viewport renders into the pbuffer (sized by [setSize]); use SubViewports and
 * their textures for the actual output, or attach an [android.view.Surface] with
 * [setOutputSurface] to have the main viewport presented into it (an ImageReader, a
 * MediaCodec input surface, a SurfaceTexture, a camera stream...). The GL context
 * survives output surface changes, so attaching and detaching never restarts the engine.
 *
 * Frame pacing: a pbuffer has no vsync, so the loop sleeps to [targetFps] (Godot's own
 * `Engine.max_fps` also applies; the lower rate wins).
 *
 * Enabled with the `--offscreen` command line argument (see Godot.kt), with optional
 * `--offscreen-size <width>x<height>` and `--offscreen-fps <fps>`. OpenGL only; the Vulkan
 * renderer needs a window surface for its swapchain.
 */
class GodotOffscreenRenderView(
	private val inputHandler: GodotInputHandler,
	initialWidth: Int,
	initialHeight: Int,
	targetFps: Int,
) : GodotRenderView {

	companion object {
		private val TAG = GodotOffscreenRenderView::class.java.simpleName

		const val DEFAULT_WIDTH = 1280
		const val DEFAULT_HEIGHT = 720
		const val DEFAULT_FPS = 60
	}

	private val renderer = GodotRenderer()
	private val events = ConcurrentLinkedQueue<Runnable>()
	private val lock = Object()

	private var thread: Thread? = null

	// Guarded by [lock].
	private var running = true // false while the host is stopped (thread parked)
	private var exitRequested = false
	private var exited = false
	private var pendingSize: Pair<Int, Int>? = null
	private var pendingSurface: Surface? = null
	private var surfaceChangeRequested = false
	private val surfaceChangeCallbacks = ArrayList<Runnable>()

	@Volatile
	var targetFps: Int = if (targetFps > 0) targetFps else DEFAULT_FPS

	// Render thread only.
	private var width = if (initialWidth > 0) initialWidth else DEFAULT_WIDTH
	private var height = if (initialHeight > 0) initialHeight else DEFAULT_HEIGHT
	private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
	private var config: EGLConfig? = null
	private var context: EGLContext = EGL14.EGL_NO_CONTEXT
	private var pbuffer: EGLSurface = EGL14.EGL_NO_SURFACE
	private var windowSurface: EGLSurface = EGL14.EGL_NO_SURFACE
	private var currentSurface: Surface? = null // the Surface windowSurface was created for

	/** No view: there is nothing to add to a layout, focus, or touch. */
	override fun getView(): SurfaceView? = null

	override fun startRenderer() {
		synchronized(lock) {
			if (thread != null) return
			thread = Thread(::renderLoop, "GodotOffscreenRender").apply { start() }
		}
	}

	override fun queueOnRenderThread(event: Runnable) {
		events.add(event)
		synchronized(lock) { lock.notifyAll() }
	}

	override fun onActivityPaused() {
		queueOnRenderThread {
			GodotLib.focusout()
			renderer.onActivityPaused()
		}
	}

	override fun onActivityStopped() {
		synchronized(lock) {
			running = false
			lock.notifyAll()
		}
	}

	override fun onActivityResumed() {
		queueOnRenderThread {
			renderer.onActivityResumed()
			GodotLib.focusin()
		}
	}

	override fun onActivityStarted() {
		synchronized(lock) {
			running = true
			lock.notifyAll()
		}
	}

	override fun blockingExitRenderer(blockingTimeInMs: Long): Boolean {
		val t: Thread
		synchronized(lock) {
			t = thread ?: return true
			exitRequested = true
			lock.notifyAll()
		}
		t.join(blockingTimeInMs)
		synchronized(lock) { return exited }
	}

	override fun getInputHandler() = inputHandler

	override fun configurePointerIcon(pointerType: Int, imagePath: String?, hotSpotX: Float, hotSpotY: Float) {}

	override fun setPointerIcon(pointerType: Int) {}

	override fun canCapturePointer() = false

	// Looked up by name through JNI (java_godot_view_wrapper.cpp), which View subclasses
	// inherit from android.view.View; there is no pointer to capture without a View.
	fun requestPointerCapture() {}

	fun releasePointerCapture() {}

	/** Resizes Godot's main window (the pbuffer, or the size reported for an output surface). */
	fun setSize(width: Int, height: Int) {
		if (width <= 0 || height <= 0) return
		synchronized(lock) {
			pendingSize = Pair(width, height)
			lock.notifyAll()
		}
	}

	/**
	 * Presents Godot's main viewport into [surface] at [width] x [height], or goes back to the
	 * pbuffer when [surface] is null. The caller keeps ownership of the Surface and must keep
	 * it valid until it has been replaced or cleared: this call returns before the switch, and
	 * [onApplied] runs on the render thread once it has happened (also if the thread exits
	 * first). A host losing a Surface (`SurfaceHolder.Callback.surfaceDestroyed`) can block on
	 * it, so Godot never draws into a destroyed Surface.
	 */
	@JvmOverloads
	fun setOutputSurface(surface: Surface?, width: Int = 0, height: Int = 0, onApplied: Runnable? = null) {
		synchronized(lock) {
			if (exited) {
				onApplied?.run()
				return
			}
			pendingSurface = surface
			surfaceChangeRequested = true
			if (onApplied != null) surfaceChangeCallbacks.add(onApplied)
			if (width > 0 && height > 0) pendingSize = Pair(width, height)
			lock.notifyAll()
		}
	}

	private fun renderLoop() {
		Log.i(TAG, "Offscreen render thread started (${width}x$height @ $targetFps fps)")
		try {
			if (!initEgl()) {
				Log.e(TAG, "EGL initialization failed; offscreen renderer not started")
				return
			}
			renderer.onSurfaceCreated(null, null)
			renderer.onSurfaceChanged(null, width, height)

			var nextFrameNs = System.nanoTime()
			while (true) {
				synchronized(lock) {
					// A surface change is applied even while stopped, so a host can detach.
					while (!running && !exitRequested && events.isEmpty() && !surfaceChangeRequested) lock.wait()
					if (exitRequested) return
				}
				runPendingEvents()
				applyPendingChanges()
				if (!running) continue

				val swap = renderer.onDrawFrame(null)
				if (swap && windowSurface != EGL14.EGL_NO_SURFACE) {
					if (!EGL14.eglSwapBuffers(display, windowSurface)) {
						Log.w(TAG, "eglSwapBuffers failed: 0x${Integer.toHexString(EGL14.eglGetError())}; detaching output surface")
						useSurface(null)
					}
				}

				// Pace to targetFps: a pbuffer has no vsync to block on.
				val frameNs = 1_000_000_000L / targetFps.coerceAtLeast(1)
				nextFrameNs += frameNs
				val now = System.nanoTime()
				if (nextFrameNs > now) {
					synchronized(lock) {
						val ms = (nextFrameNs - now) / 1_000_000
						val ns = ((nextFrameNs - now) % 1_000_000).toInt()
						if (!exitRequested && events.isEmpty()) lock.wait(ms, ns)
					}
				} else if (now - nextFrameNs > frameNs) {
					nextFrameNs = now // fell behind; don't try to catch up
				}
			}
		} catch (e: InterruptedException) {
			Log.w(TAG, "Offscreen render thread interrupted")
		} finally {
			if (context != EGL14.EGL_NO_CONTEXT) {
				runPendingEvents()
				renderer.onRenderThreadExiting()
			}
			releaseEgl()
			val callbacks: List<Runnable>
			synchronized(lock) {
				callbacks = ArrayList(surfaceChangeCallbacks)
				surfaceChangeCallbacks.clear()
				exited = true
				lock.notifyAll()
			}
			callbacks.forEach { it.run() }
			Log.i(TAG, "Offscreen render thread exited")
		}
	}

	private fun runPendingEvents() {
		while (true) {
			val event = events.poll() ?: break
			event.run()
		}
	}

	private fun applyPendingChanges() {
		var size: Pair<Int, Int>?
		var surfaceChange = false
		var surface: Surface? = null
		var callbacks: List<Runnable> = emptyList()
		synchronized(lock) {
			size = pendingSize
			pendingSize = null
			if (surfaceChangeRequested) {
				surfaceChange = true
				surface = pendingSurface
				pendingSurface = null
				surfaceChangeRequested = false
				callbacks = ArrayList(surfaceChangeCallbacks)
				surfaceChangeCallbacks.clear()
			}
		}
		val newSize = size
		if (newSize != null && (newSize.first != width || newSize.second != height)) {
			width = newSize.first
			height = newSize.second
			if (windowSurface == EGL14.EGL_NO_SURFACE) recreatePbuffer()
			renderer.onSurfaceChanged(null, width, height)
		}
		if (surfaceChange) {
			useSurface(surface)
			renderer.onSurfaceChanged(null, width, height)
			callbacks.forEach { it.run() }
		}
	}

	private fun initEgl(): Boolean {
		display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
		if (display == EGL14.EGL_NO_DISPLAY) return false
		val version = IntArray(2)
		if (!EGL14.eglInitialize(display, version, 0, version, 1)) return false

		// Pbuffer + window capable, so an output Surface can be attached later with the same
		// context. RGBA8 + depth, like the regular GL render view's preferred config.
		val attribs = intArrayOf(
			EGL14.EGL_RENDERABLE_TYPE, EGLExt.EGL_OPENGL_ES3_BIT_KHR,
			EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT or EGL14.EGL_WINDOW_BIT,
			EGL14.EGL_RED_SIZE, 8,
			EGL14.EGL_GREEN_SIZE, 8,
			EGL14.EGL_BLUE_SIZE, 8,
			EGL14.EGL_ALPHA_SIZE, 8,
			EGL14.EGL_DEPTH_SIZE, 24,
			EGL14.EGL_NONE
		)
		val configs = arrayOfNulls<EGLConfig>(1)
		val count = IntArray(1)
		if (!EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, count, 0) || count[0] < 1) {
			Log.e(TAG, "No pbuffer+window ES3 RGBA8 config")
			return false
		}
		config = configs[0]
		context = EGL14.eglCreateContext(
			display, config, EGL14.EGL_NO_CONTEXT,
			intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE), 0
		)
		if (context == EGL14.EGL_NO_CONTEXT) {
			Log.e(TAG, "eglCreateContext failed: 0x${Integer.toHexString(EGL14.eglGetError())}")
			return false
		}
		return recreatePbuffer()
	}

	private fun recreatePbuffer(): Boolean {
		val old = pbuffer
		pbuffer = EGL14.eglCreatePbufferSurface(
			display, config,
			intArrayOf(EGL14.EGL_WIDTH, width, EGL14.EGL_HEIGHT, height, EGL14.EGL_NONE), 0
		)
		if (pbuffer == EGL14.EGL_NO_SURFACE) {
			Log.e(TAG, "eglCreatePbufferSurface(${width}x$height) failed: 0x${Integer.toHexString(EGL14.eglGetError())}")
			pbuffer = old
			return false
		}
		val ok = EGL14.eglMakeCurrent(display, pbuffer, pbuffer, context)
		if (old != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, old)
		return ok
	}

	/** Switches the current draw surface to [surface] (or back to the pbuffer for null). */
	private fun useSurface(surface: Surface?) {
		// The same Surface again (typically a resize): keep its EGL surface. A second
		// eglCreateWindowSurface on a window that is still connected fails (EGL_BAD_ALLOC),
		// which would silently drop the output back to the pbuffer.
		if (surface != null && surface === currentSurface && windowSurface != EGL14.EGL_NO_SURFACE && surface.isValid) {
			Log.i(TAG, "Output surface unchanged; now ${width}x$height")
			return
		}
		// Release the old window surface before connecting a new one: a host may hand the
		// same native window over in a new Surface object.
		if (windowSurface != EGL14.EGL_NO_SURFACE) {
			EGL14.eglMakeCurrent(display, pbuffer, pbuffer, context)
			EGL14.eglDestroySurface(display, windowSurface)
			windowSurface = EGL14.EGL_NO_SURFACE
		}
		currentSurface = null
		if (surface != null && surface.isValid) {
			val s = EGL14.eglCreateWindowSurface(display, config, surface, intArrayOf(EGL14.EGL_NONE), 0)
			if (s != EGL14.EGL_NO_SURFACE && EGL14.eglMakeCurrent(display, s, s, context)) {
				EGL14.eglSwapInterval(display, 0)
				windowSurface = s
				currentSurface = surface
				Log.i(TAG, "Presenting into output surface ${width}x$height")
			} else {
				Log.e(TAG, "Output surface rejected: 0x${Integer.toHexString(EGL14.eglGetError())}")
				if (s != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, s)
			}
		}
		if (windowSurface == EGL14.EGL_NO_SURFACE) {
			if (width > 0 && height > 0) recreatePbuffer() else EGL14.eglMakeCurrent(display, pbuffer, pbuffer, context)
		}
	}

	private fun releaseEgl() {
		if (display == EGL14.EGL_NO_DISPLAY) return
		EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
		if (windowSurface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, windowSurface)
		currentSurface = null
		if (pbuffer != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, pbuffer)
		if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
		// No eglTerminate(): the display is process-wide and may be shared with other users.
		windowSurface = EGL14.EGL_NO_SURFACE
		pbuffer = EGL14.EGL_NO_SURFACE
		context = EGL14.EGL_NO_CONTEXT
		display = EGL14.EGL_NO_DISPLAY
	}
}
