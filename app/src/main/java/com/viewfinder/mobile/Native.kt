package com.viewfinder.mobile

object Native {
    init { System.loadLibrary("vfengine") }
    external fun nativeCreate()
    external fun nativeResize(w: Int, h: Int)
    external fun nativeDraw(dt: Float)
    external fun nativeInput(mx: Float, my: Float, jump: Boolean)
    external fun nativeLook(dx: Float, dy: Float)
    external fun nativeAction(a: Int)
    external fun nativeSlots(r: FloatArray)
    external fun nativeState(): Int
    external fun nativePoll(): Int
}
