package com.viewfinder.mobile

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

class TouchOverlay(ctx: Context) : View(ctx) {
    private val d = resources.displayMetrics.density
    private val teal = Color.parseColor("#5FE3D0")

    private class B(val id: Int, val label: String) {
        var cx = 0f
        var cy = 0f
        var r = 0f
    }

    private val jump = B(0, "PULAR")
    private val shot = B(1, "FOTO")
    private val place = B(2, "COLAR")
    private val tp = B(3, "TELEPORTE")
    private val undo = B(4, "DESFAZER")
    private val btns = listOf(jump, shot, place, tp, undo)
    private val slots = Array(4) { RectF() }

    private val role = IntArray(32) { -9 }
    private val lx = FloatArray(32)
    private val ly = FloatArray(32)
    private var movePid = -1
    private var ox = 0f
    private var oy = 0f
    private var kx = 0f
    private var ky = 0f
    private var jumpHeld = false
    private val pressed = BooleanArray(5)

    private var toastText = ""
    private var toastUntil = 0L
    private var flashA = 0f
    private var flashC = Color.WHITE
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)

    init {
        setBackgroundColor(Color.TRANSPARENT)
        showToast("Fotografe a escada, segure a foto, alinhe e cole perto da parede")
    }

    private fun vis(b: B, s: Int): Boolean = when (b.id) {
        2, 3 -> (s and 1) != 0
        4 -> (s and 2) != 0
        else -> true
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        jump.cx = w - 86 * d; jump.cy = h - 86 * d; jump.r = 48 * d
        shot.cx = w - 200 * d; shot.cy = h - 64 * d; shot.r = 36 * d
        place.cx = w - 72 * d; place.cy = h - 200 * d; place.r = 38 * d
        tp.cx = w - 168 * d; tp.cy = h - 170 * d; tp.r = 30 * d
        undo.cx = w - 252 * d; undo.cy = h - 142 * d; undo.r = 26 * d
        val sh = 52 * d
        val sw = sh * w / h
        val gap = 10 * d
        val arr = FloatArray(16)
        for (i in 0..3) {
            val x = w - 14 * d - (4 - i) * sw - (3 - i) * gap
            slots[i].set(x, 14 * d, x + sw, 14 * d + sh)
            arr[i * 4] = x; arr[i * 4 + 1] = 14 * d; arr[i * 4 + 2] = sw; arr[i * 4 + 3] = sh
        }
        Native.nativeSlots(arr)
    }

    fun showToast(s: String) {
        toastText = s
        toastUntil = System.currentTimeMillis() + 2800
    }

    private fun flash(c: Int, a: Float) { flashC = c; flashA = a }

    fun onEvent(code: Int, arg: Int) {
        when (code) {
            1 -> {
                showToast(if (arg > 0) "Foto tirada · $arg objetos" else "Foto tirada · nenhum objeto no quadro")
                flash(Color.WHITE, 0.85f)
            }
            2 -> { showToast("$arg objetos viraram realidade"); flash(teal, 0.45f) }
            3 -> showToast("Sem espaço aqui. Ande um pouco e tente de novo")
            4 -> { showToast("Teleportado para o local da foto"); flash(teal, 0.6f) }
            5 -> showToast("Última foto colada foi recolhida")
            6 -> { showToast("Você chegou ao objetivo!"); flash(Color.parseColor("#FFE066"), 0.5f) }
        }
    }

    private fun push() {
        val r = 60 * d
        Native.nativeInput(kx / r, ky / r, jumpHeld)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                down(e.getPointerId(i), e.getX(i), e.getY(i))
            }
            MotionEvent.ACTION_MOVE -> for (i in 0 until e.pointerCount) move(e.getPointerId(i), e.getX(i), e.getY(i))
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> up(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> for (i in 0 until 32) up(i)
        }
        return true
    }

    private fun down(pid: Int, x: Float, y: Float) {
        if (pid >= 32) return
        val s = Native.nativeState()
        for (b in btns) {
            if (!vis(b, s)) continue
            if (hypot(x - b.cx, y - b.cy) <= b.r * 1.2f) {
                role[pid] = 100 + b.id
                pressed[b.id] = true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                if (b.id == 0) {
                    jumpHeld = true
                    push()
                } else {
                    Native.nativeAction(b.id)
                }
                return
            }
        }
        for (i in 0..3) {
            if (slots[i].contains(x, y)) {
                role[pid] = 200 + i
                Native.nativeAction(10 + i)
                return
            }
        }
        if (x < width * 0.45f && movePid < 0) {
            role[pid] = -1
            movePid = pid
            ox = x; oy = y; kx = 0f; ky = 0f
            push()
            return
        }
        role[pid] = -2
        lx[pid] = x; ly[pid] = y
    }

    private fun move(pid: Int, x: Float, y: Float) {
        if (pid >= 32) return
        when (role[pid]) {
            -1 -> {
                val r = 60 * d
                val dx = x - ox
                val dy = y - oy
                val m = hypot(dx, dy)
                val k = if (m > r) r / m else 1f
                kx = dx * k; ky = dy * k
                push()
            }
            -2 -> {
                Native.nativeLook((x - lx[pid]) / d, (y - ly[pid]) / d)
                lx[pid] = x; ly[pid] = y
            }
        }
    }

    private fun up(pid: Int) {
        if (pid >= 32) return
        val r = role[pid]
        if (r == -9) return
        if (r == -1) {
            movePid = -1; kx = 0f; ky = 0f
            push()
        } else if (r in 100..104) {
            val id = r - 100
            pressed[id] = false
            if (id == 0) {
                jumpHeld = false
                push()
            }
        }
        role[pid] = -9
    }

    // ------------------------------------------------------------ desenho
    override fun onDraw(c: Canvas) {
        val s = Native.nativeState()
        val holding = (s and 1) != 0
        val count = (s shr 4) and 15
        val w = width.toFloat()
        val h = height.toFloat()
        if (!holding) drawFrame(c, w, h) else drawHint(c, w, "Alinhe a foto com o mundo e toque em COLAR")
        for (i in 0..3) if (i >= count) drawDashed(c, slots[i], i + 1)
        for (b in btns) if (vis(b, s)) drawBtn(c, b)
        drawStick(c, h)
        drawHud(c)
        drawToast(c, w, h)
        if (flashA > 0f) {
            val a = (flashA * 255).toInt().coerceIn(0, 255)
            c.drawColor(Color.argb(a, Color.red(flashC), Color.green(flashC), Color.blue(flashC)))
            flashA -= 0.045f
        }
        postInvalidateOnAnimation()
    }

    private fun drawFrame(c: Canvas, w: Float, h: Float) {
        p.style = Paint.Style.STROKE
        p.strokeWidth = 2.5f * d
        p.strokeCap = Paint.Cap.ROUND
        p.color = Color.argb(225, 255, 255, 255)
        val l = w * 0.16f; val t = h * 0.16f; val r = w * 0.84f; val b = h * 0.84f; val n = 28 * d
        c.drawLine(l, t, l + n, t, p); c.drawLine(l, t, l, t + n, p)
        c.drawLine(r, t, r - n, t, p); c.drawLine(r, t, r, t + n, p)
        c.drawLine(l, b, l + n, b, p); c.drawLine(l, b, l, b - n, p)
        c.drawLine(r, b, r - n, b, p); c.drawLine(r, b, r, b - n, p)
        p.style = Paint.Style.FILL
        c.drawCircle(w / 2, h / 2, 2.4f * d, p)
    }

    private fun drawHint(c: Canvas, w: Float, s: String) {
        p.style = Paint.Style.FILL
        p.textAlign = Paint.Align.CENTER
        p.textSize = 13 * d
        p.typeface = Typeface.DEFAULT_BOLD
        val tw = p.measureText(s)
        p.color = Color.argb(150, 11, 18, 24)
        c.drawRoundRect(RectF(w / 2 - tw / 2 - 14 * d, 84 * d, w / 2 + tw / 2 + 14 * d, 114 * d), 15 * d, 15 * d, p)
        p.color = Color.WHITE
        c.drawText(s, w / 2, 104 * d, p)
    }

    private fun drawDashed(c: Canvas, r: RectF, n: Int) {
        p.style = Paint.Style.STROKE
        p.strokeWidth = 2 * d
        p.color = Color.argb(130, 255, 255, 255)
        p.pathEffect = DashPathEffect(floatArrayOf(8 * d, 6 * d), 0f)
        c.drawRoundRect(r, 6 * d, 6 * d, p)
        p.pathEffect = null
        p.style = Paint.Style.FILL
        p.textAlign = Paint.Align.CENTER
        p.textSize = 14 * d
        p.typeface = Typeface.DEFAULT_BOLD
        c.drawText(n.toString(), r.centerX(), r.centerY() + 5 * d, p)
    }

    private fun drawBtn(c: Canvas, b: B) {
        val accent = b.id == 2 || b.id == 3
        p.style = Paint.Style.FILL
        p.color = if (pressed[b.id]) Color.argb(150, 95, 227, 208) else Color.argb(55, 255, 255, 255)
        c.drawCircle(b.cx, b.cy, b.r, p)
        p.style = Paint.Style.STROKE
        p.strokeWidth = 2.2f * d
        p.color = if (accent) teal else Color.argb(185, 255, 255, 255)
        c.drawCircle(b.cx, b.cy, b.r, p)
        drawIcon(c, b.id, b.cx, b.cy, b.r * 0.38f)
        p.style = Paint.Style.FILL
        p.textAlign = Paint.Align.CENTER
        p.textSize = 9.5f * d
        p.typeface = Typeface.DEFAULT_BOLD
        p.letterSpacing = 0.1f
        p.color = Color.argb(230, 255, 255, 255)
        c.drawText(b.label, b.cx, b.cy + b.r + 13 * d, p)
        p.letterSpacing = 0f
    }

    private fun drawIcon(c: Canvas, id: Int, x: Float, y: Float, s: Float) {
        p.color = Color.WHITE
        p.strokeWidth = 2.6f * d
        p.strokeCap = Paint.Cap.ROUND
        p.strokeJoin = Paint.Join.ROUND
        p.style = Paint.Style.STROKE
        when (id) {
            0 -> {
                val pa = Path()
                pa.moveTo(x - s, y + s * .5f); pa.lineTo(x, y - s * .5f); pa.lineTo(x + s, y + s * .5f)
                c.drawPath(pa, p)
            }
            1 -> {
                c.drawRoundRect(RectF(x - s * 1.2f, y - s * .7f, x + s * 1.2f, y + s * .8f), s * .3f, s * .3f, p)
                c.drawCircle(x, y + s * .05f, s * .45f, p)
                c.drawLine(x - s * .5f, y - s * .7f, x - s * .3f, y - s, p)
                c.drawLine(x - s * .3f, y - s, x + s * .3f, y - s, p)
                c.drawLine(x + s * .3f, y - s, x + s * .5f, y - s * .7f, p)
            }
            2 -> {
                c.drawLine(x, y - s, x, y + s * .5f, p)
                c.drawLine(x - s * .6f, y - s * .1f, x, y + s * .5f, p)
                c.drawLine(x + s * .6f, y - s * .1f, x, y + s * .5f, p)
                c.drawLine(x - s * .9f, y + s, x + s * .9f, y + s, p)
            }
            3 -> {
                p.style = Paint.Style.FILL
                val pa = Path()
                pa.moveTo(x, y - s * 1.1f)
                pa.quadTo(x, y, x + s * 1.1f, y)
                pa.quadTo(x, y, x, y + s * 1.1f)
                pa.quadTo(x, y, x - s * 1.1f, y)
                pa.quadTo(x, y, x, y - s * 1.1f)
                pa.close()
                c.drawPath(pa, p)
            }
            4 -> {
                c.drawArc(RectF(x - s, y - s, x + s, y + s), -40f, 270f, false, p)
                val tx = x - 0.643f * s
                val ty = y - 0.766f * s
                c.drawLine(tx, ty, tx - 0.6f * s * 0.996f, ty + 0.6f * s * 0.087f, p)
                c.drawLine(tx, ty, tx - 0.6f * s * 0.258f, ty + 0.6f * s * 0.967f, p)
            }
        }
        p.style = Paint.Style.FILL
    }

    private fun drawStick(c: Canvas, h: Float) {
        p.style = Paint.Style.FILL
        if (movePid >= 0) {
            p.color = Color.argb(35, 255, 255, 255)
            c.drawCircle(ox, oy, 60 * d, p)
            p.style = Paint.Style.STROKE
            p.strokeWidth = 2 * d
            p.color = Color.argb(140, 255, 255, 255)
            c.drawCircle(ox, oy, 60 * d, p)
            p.style = Paint.Style.FILL
            p.color = Color.argb(70, 95, 227, 208)
            c.drawCircle(ox + kx, oy + ky, 34 * d, p)
            p.color = Color.argb(225, 255, 255, 255)
            c.drawCircle(ox + kx, oy + ky, 24 * d, p)
        } else {
            p.style = Paint.Style.STROKE
            p.strokeWidth = 2 * d
            p.color = Color.argb(45, 255, 255, 255)
            c.drawCircle(120 * d, h - 110 * d, 60 * d, p)
            p.style = Paint.Style.FILL
        }
    }

    private fun drawHud(c: Canvas) {
        p.style = Paint.Style.FILL
        p.textAlign = Paint.Align.LEFT
        p.typeface = Typeface.DEFAULT_BOLD
        p.letterSpacing = 0.14f
        p.textSize = 14 * d
        p.color = teal
        c.drawText("VIEWFINDER", 28 * d, 30 * d, p)
        p.textSize = 10 * d
        p.color = Color.argb(215, 255, 255, 255)
        c.drawText("V0.2 ALPHA · BASEPLATE", 28 * d, 46 * d, p)
        p.letterSpacing = 0f
    }

    private fun drawToast(c: Canvas, w: Float, h: Float) {
        val left = toastUntil - System.currentTimeMillis()
        if (left <= 0) return
        val a = if (left < 400) left / 400f else 1f
        p.style = Paint.Style.FILL
        p.textAlign = Paint.Align.CENTER
        p.textSize = 14 * d
        p.typeface = Typeface.DEFAULT_BOLD
        val tw = p.measureText(toastText)
        val cy = h * 0.22f
        p.color = Color.argb((190 * a).toInt(), 11, 18, 24)
        c.drawRoundRect(RectF(w / 2 - tw / 2 - 18 * d, cy - 20 * d, w / 2 + tw / 2 + 18 * d, cy + 14 * d), 17 * d, 17 * d, p)
        p.color = Color.argb((255 * a).toInt(), 255, 255, 255)
        c.drawText(toastText, w / 2, cy + 4 * d, p)
    }
}
