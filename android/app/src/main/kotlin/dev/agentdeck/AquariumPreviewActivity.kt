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

/** Opt-in native rendering trial. The live dashboard stays available through Back. */
class AquariumPreviewActivity : Activity(), Choreographer.FrameCallback {
    private var viewer: ModelViewer? = null
    private var fillLight: IndirectLight? = null
    private var active = false
    private var lastFrame = 0L
    private var reduceMotion = false
    private val power by lazy { getSystemService(PowerManager::class.java) }
    private var elapsedSeconds = 0f
    private val choreographer by lazy { Choreographer.getInstance() }
    private lateinit var root: FrameLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (dev.agentdeck.util.DeviceProfile.detect(this).isEink) { finish(); return }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        root = FrameLayout(this)
        setContentView(root)
        val close = Button(this).apply {
            text = "Back to dashboard"
            setOnClickListener { finish() }
        }
        try {
            Utils.init()
            val surface = SurfaceView(this)
            root.addView(surface, FrameLayout.LayoutParams(-1, -1))
            val model = ModelViewer(surface, manipulator = null)
            viewer = model
            model.autoPlayAnimations = false
            val bytes = assets.open("living-aquarium.glb").use { it.readBytes() }
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
            root.addView(TextView(this).apply {
                text = "The 3D aquarium could not be opened. Your dashboard is still available."
                gravity = Gravity.CENTER
            }, FrameLayout.LayoutParams(-1, -1))
        }
        root.addView(close, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END))
    }

    override fun onResume() {
        super.onResume()
        active = true
        lastFrame = 0L
        reduceMotion = Settings.Global.getFloat(contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f
        choreographer.postFrameCallback(this)
    }

    override fun onPause() {
        active = false
        choreographer.removeFrameCallback(this)
        super.onPause()
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

    override fun onDestroy() {
        active = false
        choreographer.removeFrameCallback(this)
        viewer?.let { model ->
            model.scene.indirectLight = null
            fillLight?.let { model.engine.destroyIndirectLight(it) }
        }
        fillLight = null
        // ModelViewer destroys its engine when its SurfaceView detaches. Do not destroy twice.
        if (::root.isInitialized) root.removeAllViews()
        viewer = null
        super.onDestroy()
    }
}
