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
fun AquariumBackground(modifier: Modifier = Modifier, state: dev.agentdeck.terrarium.TerrariumState? = null, focusedId: String? = null, onUnavailable: () -> Unit = {}) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    val surface = remember(context) { AquariumSurface(context) }
    AndroidView(factory = { surface }, modifier = modifier, update = { if (!it.available || (state != null && !it.sync(state, focusedId))) onUnavailable() })
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
    var available = false
        private set
    private var viewer: ModelViewer? = null
    private var waterEntities = intArrayOf()
    private var fillLight: IndirectLight? = null
    private var backdrop: com.google.android.filament.Skybox? = null
    private var residents: dev.agentdeck.terrarium.AquariumResidents? = null
    private val labels = object : android.view.View(context) {
        override fun onDraw(canvas: android.graphics.Canvas) { residents?.drawLabels(canvas) }
    }
    private var active = false
    private var lastFrame = 0L
    private var lastBudgetCheck = 0L
    private var constrained = false
    private var renderSurface: SurfaceView? = null
    private var bufferSize = 0 to 0
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
                    releaseResidents()
                    releaseLighting()
                    viewer = null
                    super.onDetachedFromWindow()
                }
            }
            renderSurface = surface
            root.addView(surface, FrameLayout.LayoutParams(-1, -1))
            val model = ModelViewer(surface, manipulator = null)
            viewer = model
            model.autoPlayAnimations = false
            // Keep the habitat affordable on older tablet GPUs. UI and labels
            // remain at display resolution; only the native surface is scaled.
            model.view.multiSampleAntiAliasingOptions = com.google.android.filament.View.MultiSampleAntiAliasingOptions().apply { enabled = false }
            model.view.ambientOcclusionOptions = com.google.android.filament.View.AmbientOcclusionOptions().apply { enabled = false }
            model.view.bloomOptions = com.google.android.filament.View.BloomOptions().apply { enabled = false }
            model.view.antiAliasing = com.google.android.filament.View.AntiAliasing.FXAA
            val lightManager = model.engine.lightManager
            val lightInstance = lightManager.getInstance(model.light)
            val intensity = lightManager.getIntensity(lightInstance)
            val color = lightManager.getColor(lightInstance, FloatArray(3))
            lightManager.destroy(model.light)
            com.google.android.filament.LightManager.Builder(com.google.android.filament.LightManager.Type.SUN)
                .color(color[0], color[1], color[2]).intensity(intensity)
                .castShadows(true)
                .shadowOptions(com.google.android.filament.LightManager.ShadowOptions().apply { mapSize = 512 })
                .build(model.engine, model.light)
            val bytes = context.assets.open("living-aquarium.glb").use { it.readBytes() }
            model.loadModelGlb(ByteBuffer.allocateDirect(bytes.size).apply { put(bytes); flip() })
            val water = dev.agentdeck.ui.theme.DesignTokens.Ink.s900
            backdrop = com.google.android.filament.Skybox.Builder()
                .color(water.red, water.green, water.blue, 1f).build(model.engine)
            model.scene.skybox = backdrop
            model.cameraFocalLength = 42f
            model.camera.lookAt(0.0, 4.8, 14.0, 0.0, 1.65, -0.7, 0.0, 1.0, 0.0)
            model.engine.lightManager.setDirection(
                model.engine.lightManager.getInstance(model.light), -0.5f, -1f, -0.6f)
            fillLight = IndirectLight.Builder().irradiance(1, floatArrayOf(0.8f, 0.9f, 1f))
                .intensity(25_000f).build(model.engine)
            model.scene.indirectLight = fillLight
            residents = dev.agentdeck.terrarium.AquariumResidents(context, model)
            root.addView(labels, FrameLayout.LayoutParams(-1, -1))
            waterEntities = model.asset?.entities?.filter { entity ->
                val name = model.asset?.getName(entity).orEmpty().lowercase()
                name.contains("garden") && name.contains("water")
            }?.toIntArray() ?: intArrayOf()
            available = true
            android.util.Log.i("Aquarium3D", "Native model loaded; animations=${model.animator?.animationCount}")
        } catch (error: Exception) {
            android.util.Log.e("Aquarium3D", "Could not open aquarium", error)
            root.addView(TextView(context).apply {
                text = "The 3D aquarium could not be opened. Your dashboard is still available."
                gravity = Gravity.CENTER
            }, FrameLayout.LayoutParams(-1, -1))
        }
    }

    fun sync(state: dev.agentdeck.terrarium.TerrariumState, focusedId: String?): Boolean {
        if (!available) return false
        return try {
            residents?.sync(state, focusedId)
            true
        } catch (error: Exception) {
            available = false
            android.util.Log.e("Aquarium3D", "Resident loading failed; using the standard dashboard", error)
            false
        }
    }

    fun resume() {
        if (active) return
        active = true
        lastFrame = 0L
        lastBudgetCheck = 0L
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
        // Power queries cross Binder; sample them outside the per-frame hot path.
        if (lastBudgetCheck == 0L || frameTimeNanos - lastBudgetCheck >= 2_000_000_000L) {
            lastBudgetCheck = frameTimeNanos
            constrained = power.isPowerSaveMode || power.currentThermalStatus >= PowerManager.THERMAL_STATUS_MODERATE
            val maxEdge = if (constrained) 960 else 1440
            val scale = minOf(1f, maxEdge.toFloat() / maxOf(width, height, 1))
            val next = (width * scale).toInt().coerceAtLeast(1) to (height * scale).toInt().coerceAtLeast(1)
            if (next != bufferSize) {
                bufferSize = next
                renderSurface?.holder?.setFixedSize(next.first, next.second)
            }
        }
        if (lastFrame != 0L && frameTimeNanos - lastFrame < if (constrained) 32_000_000L else 15_000_000L) return
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
            residents?.step(if (reduceMotion) 0f else dt, width.toFloat() / height.coerceAtLeast(1))
            model.render(frameTimeNanos)
            // The authored enclosure is finite; a wide tablet must not expose
            // its black exterior. The native skybox supplies continuous water.
            model.scene.removeEntities(waterEntities)
            labels.invalidate()
        }
    }

    private fun releaseResidents() {
        residents?.dispose()
        residents = null
    }

    private fun releaseLighting() {
        val light = fillLight
        fillLight = null
        viewer?.let { model ->
            model.scene.indirectLight = null
            if (light != null) model.engine.destroyIndirectLight(light)
            model.scene.skybox = null
            backdrop?.let { model.engine.destroySkybox(it) }
            backdrop = null
        }
    }

    fun dispose() {
        pause()
        releaseResidents()
        releaseLighting()
        viewer = null
        // The child surface owns ModelViewer's engine-detach lifecycle.
        root.removeAllViews()
    }
}
