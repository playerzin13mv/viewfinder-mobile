package com.viewfinder.mobile

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : Activity() {
    private lateinit var gl: GLSurfaceView
    private lateinit var overlay: TouchOverlay

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= 28) {
            val lp = window.attributes
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            window.attributes = lp
        }
        overlay = TouchOverlay(this)
        gl = GLSurfaceView(this)
        gl.setEGLContextClientVersion(3)
        gl.setEGLConfigChooser(8, 8, 8, 8, 16, 0)
        gl.setRenderer(object : GLSurfaceView.Renderer {
            private var last = 0L
            override fun onSurfaceCreated(g: GL10?, c: EGLConfig?) {
                Native.nativeCreate()
                last = System.nanoTime()
            }
            override fun onSurfaceChanged(g: GL10?, w: Int, h: Int) {
                Native.nativeResize(w, h)
            }
            override fun onDrawFrame(g: GL10?) {
                val now = System.nanoTime()
                val dt = ((now - last) / 1_000_000_000f).coerceIn(0f, 0.05f)
                last = now
                Native.nativeDraw(dt)
                while (true) {
                    val ev = Native.nativePoll()
                    if (ev == 0) break
                    overlay.post { overlay.onEvent(ev and 0xFF, ev shr 8) }
                }
            }
        })
        gl.renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        val root = FrameLayout(this)
        root.addView(gl)
        root.addView(overlay)
        setContentView(root)
        hideBars()
    }

    @Suppress("DEPRECATION")
    private fun hideBars() {
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideBars()
    }

    override fun onResume() { super.onResume(); gl.onResume() }
    override fun onPause() { gl.onPause(); super.onPause() }
}
