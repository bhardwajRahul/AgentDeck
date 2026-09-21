package dev.agentdeck.terrarium.renderer

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import dev.agentdeck.R

/** Baked lighting costs one bitmap draw; residents and their labels remain live. */
internal class AquariumHabitat private constructor(private val bitmap: Bitmap) {
    private val source = Rect()
    private val destination = RectF()
    private val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    fun draw(canvas: Canvas, width: Int, height: Int) {
        // Center crop instead of stretching the rocks when the device rotates.
        val scale = maxOf(width.toFloat() / bitmap.width, height.toFloat() / bitmap.height)
        val cropWidth = (width / scale).toInt().coerceIn(1, bitmap.width)
        val cropHeight = (height / scale).toInt().coerceIn(1, bitmap.height)
        val left = (bitmap.width - cropWidth) / 2
        val top = (bitmap.height - cropHeight) / 2
        source.set(left, top, left + cropWidth, top + cropHeight)
        destination.set(0f, 0f, width.toFloat(), height.toFloat())
        canvas.drawBitmap(bitmap, source, destination, bitmapPaint)
    }

    companion object {
        fun load(context: Context, color: Boolean): AquariumHabitat? {
            val source = BitmapFactory.decodeResource(context.resources, R.drawable.aquarium_habitat)
                ?: return null
            if (color) return AquariumHabitat(source)
            val gray = Bitmap.createBitmap(source.width, source.height, Bitmap.Config.ARGB_8888)
            val paint = Paint().apply {
                colorFilter = ColorMatrixColorFilter(ColorMatrix().apply { setSaturation(0f) })
            }
            Canvas(gray).drawBitmap(source, 0f, 0f, paint)
            source.recycle()
            return AquariumHabitat(gray)
        }
    }
}

/** The canonical path, including its holes, receives the habitat's top-left light. */
internal fun drawAquariumMark(canvas: Canvas, paint: Paint, path: Path) {
    if (!paint.isAntiAlias || paint.shader != null) {
        canvas.drawPath(path, paint)
        return
    }
    val bounds = RectF()
    path.computeBounds(bounds, true)
    if (bounds.isEmpty) return
    fun shade(factor: Float): Int = Color.argb(
        255, // Paint already carries opacity; shader alpha must not dim dormant marks twice.
        (Color.red(paint.color) * factor).toInt().coerceIn(0, 255),
        (Color.green(paint.color) * factor).toInt().coerceIn(0, 255),
        (Color.blue(paint.color) * factor).toInt().coerceIn(0, 255),
    )
    paint.shader = LinearGradient(bounds.left, bounds.top, bounds.right, bounds.bottom,
        shade(1.4f), shade(0.72f), Shader.TileMode.CLAMP)
    canvas.drawPath(path, paint)
    paint.shader = null
}
