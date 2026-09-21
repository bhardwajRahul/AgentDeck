package dev.agentdeck

import android.app.Activity
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.view.Choreographer
import android.view.Gravity
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView
import com.google.android.filament.IndirectLight
import com.google.android.filament.utils.ModelViewer
import com.google.android.filament.utils.Utils
import java.nio.ByteBuffer

import android.content.Context
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.compose.ui.platform.LocalContext

/** Standalone compatibility entry; production dashboards embed AquariumSurface. */
class AquariumPreviewActivity : Activity() {
    private var surface: AquariumSurface? = null
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (dev.agentdeck.util.DeviceProfile.detect(this).isEink) { finish(); return }
        val container = FrameLayout(this)
        surface = AquariumSurface(this)
        container.addView(surface)
        container.addView(Button(this).apply {
            text = "Back to dashboard"
            setOnClickListener { finish() }
        }, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END))
        setContentView(container)
    }
    override fun onResume() { super.onResume(); surface?.resume() }
    override fun onPause() { surface?.pause(); super.onPause() }
    override fun onDestroy() { surface?.dispose(); surface = null; super.onDestroy() }
}

@Composable
fun AquariumBackground(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    val surface = remember(context) { AquariumSurface(context) }
    AndroidView(factory = { surface }, modifier = modifier)
    DisposableEffect(owner, surface) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> surface.resume()
                Lifecycle.Event.ON_PAUSE -> surface.pause()
                else -> Unit
            }
        }
        owner.lifecycle.addObserver(observer)
        if (owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) surface.resume()
        onDispose { owner.lifecycle.removeObserver(observer); surface.dispose() }
    }
}

class AquariumSurface(context: Context) : FrameLayout(context), Choreographer.FrameCallback {
    private var viewer: ModelViewer? = null
    private var fillLight: IndirectLight? = null
    private var active = false
    private var lastFrame = 0L
    private var reduceMotion = false
    private val power by lazy { context.getSystemService(PowerManager::class.java) }
    private var elapsedSeconds = 0f
    private val choreographer by lazy { Choreographer.getInstance() }
    private val root get() = this

    init {
        try {
            Utils.init()
            val surface = object : SurfaceView(context) {
                override fun onDetachedFromWindow() {
                    // ModelViewer's detach listener destroys the engine after this
                    // callback. Release our light before that listener runs.
                    pause()
                    releaseLighting()
                    viewer = null
                    super.onDetachedFromWindow()
                }
            }
            root.addView(surface, FrameLayout.LayoutParams(-1, -1))
            val model = ModelViewer(surface, manipulator = null)
            viewer = model
            model.autoPlayAnimations = false
            val bytes = context.assets.open("living-aquarium.glb").use { it.readBytes() }
            model.loadModelGlb(ByteBuffer.allocateDirect(bytes.size).apply { put(bytes); flip() })
            model.cameraFocalLength = 42f
            model.camera.lookAt(0.0, 4.8, 14.0, 0.0, 1.65, -0.7, 0.0, 1.0, 0.0)
            model.engine.lightManager.setDirection(
                model.engine.lightManager.getInstance(model.light), -0.5f, -1f, -0.6f)
            fillLight = IndirectLight.Builder().irradiance(1, floatArrayOf(0.8f, 0.9f, 1f))
                .intensity(25_000f).build(model.engine)
            model.scene.indirectLight = fillLight
            android.util.Log.i("Aquarium3D", "Native model loaded; animations=${model.animator?.animationCount}")
        } catch (error: Exception) {
            android.util.Log.e("Aquarium3D", "Could not open aquarium", error)
            root.addView(TextView(context).apply {
                text = "The 3D aquarium could not be opened. Your dashboard is still available."
                gravity = Gravity.CENTER
            }, FrameLayout.LayoutParams(-1, -1))
        }
    }

    fun resume() {
        if (active) return
        active = true
        lastFrame = 0L
        reduceMotion = Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f
        choreographer.postFrameCallback(this)
    }

    fun pause() {
        active = false
        choreographer.removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!active) return
        choreographer.postFrameCallback(this)
        val lowPower = power.isPowerSaveMode || power.currentThermalStatus >= PowerManager.THERMAL_STATUS_MODERATE
        if (lastFrame != 0L && frameTimeNanos - lastFrame < if (lowPower) 33_000_000L else 15_000_000L) return
        val dt = if (lastFrame == 0L) 0f else ((frameTimeNanos - lastFrame) / 1e9f).coerceAtMost(0.1f)
        lastFrame = frameTimeNanos
        if (!reduceMotion) elapsedSeconds += dt
        viewer?.let { model ->
            model.animator?.let { animator ->
                if (animator.animationCount > 0) {
                    animator.applyAnimation(0, elapsedSeconds % animator.getAnimationDuration(0).coerceAtLeast(1f))
                    animator.updateBoneMatrices()
                }
            }
            model.render(frameTimeNanos)
        }
    }

    private fun releaseLighting() {
        val light = fillLight ?: return
        fillLight = null
        viewer?.let { model ->
            model.scene.indirectLight = null
            model.engine.destroyIndirectLight(light)
        }
    }

    fun dispose() {
        pause()
        releaseLighting()
        viewer = null
        // The child surface owns ModelViewer's engine-detach lifecycle.
        root.removeAllViews()
    }
}
