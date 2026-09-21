package dev.agentdeck.terrarium.renderer

internal const val EINK_FISH_COUNT = 12
internal const val EINK_FISH_PER_SCHOOL = 6

/** Persistent fish state for boids simulation. */
class EinkFish(
    var x: Float, var y: Float,
    var vx: Float, var vy: Float,
    val schoolId: Int,
    var heading: Float = if (vx >= 0f) 0f else 180f,
)

/**
 * Persistent boids-based fish school. 12 fish in 2 schools (6+6).
 * Lissajous school centers, separation/alignment/cohesion, wall repulsion.
 * Call [update] each animation frame before drawing. Its stepScale is elapsed
 * time relative to the 400ms B&W e-ink cadence, so faster color e-ink redraws
 * interpolate instead of increasing simulation speed.
 */
class EinkFishSchool {
    val fish: List<EinkFish>

    // Lissajous time accumulator (persistent across frames)
    private var time = 0f
    private var remainder = 0f
    private val previous = FloatArray(EINK_FISH_COUNT * 4)

    companion object {
        // Boids weights
        private const val SEPARATION_DIST = 0.10f
        private const val SEPARATION_WEIGHT = 0.006f
        // 50ms physics ticks make 100ms and 400ms display cadences agree.
        private const val STEP = 0.125f
        private const val MAX_ACCELERATION = 0.003f
        private const val ALIGNMENT_WEIGHT = 0.04f
        private const val COHESION_WEIGHT = 0.008f
        private const val SCHOOL_ATTRACTOR_WEIGHT = 0.4f
        private const val AGENT_PULL = 0.30f
        private const val CRAYFISH_PULL = 0.30f
        // Speed limits (normalized per frame at ~2.5fps)
        private const val MAX_SPEED_CIRCLING = 0.015f
        private const val MAX_SPEED_STREAMING = 0.025f
        // Boundaries (normalized 0..1)
        private const val MIN_X = 0.04f; private const val MAX_X = 0.96f
        private const val MIN_Y = 0.10f; private const val MAX_Y = 0.70f
        private const val WALL_MARGIN = 0.05f
        private const val WALL_FORCE = 0.003f
        private const val VY_DAMPING = 0.85f
    }

    init {
        val rng = java.util.Random(42)
        fish = List(EINK_FISH_COUNT) { i ->
            val sid = if (i < EINK_FISH_PER_SCHOOL) 0 else 1
            // Start with visible spacing and a shared heading, not an overlapping knot.
            val local = i % EINK_FISH_PER_SCHOOL
            val direction = if (sid == 0) 1f else -1f
            val baseX = if (sid == 0) 0.35f else 0.55f
            val baseY = if (sid == 0) 0.35f else 0.40f
            EinkFish(
                x = baseX + (local % 3 - 1) * 0.085f + (rng.nextFloat() - 0.5f) * 0.01f,
                y = baseY + (local / 3 - 0.5f) * 0.10f,
                vx = direction * (0.003f + rng.nextFloat() * 0.001f),
                vy = (rng.nextFloat() - 0.5f) * 0.0005f,
                schoolId = sid,
            )
        }
    }

    /**
     * Advance one frame. Call before drawing.
     * @param streaming true if STREAMING state (faster speed, agent pull)
     * @param agentSlots octopus positions (normalized). Empty = no agent pull.
     * @param crayfishRouting true if crayfish is ROUTING (additional pull)
     */
    fun update(
        streaming: Boolean,
        agentSlots: List<dev.agentdeck.terrarium.CreatureSlot>,
        crayfishRouting: Boolean,
        stepScale: Float = 1f,
        hovering: Boolean = false,
    ) {
        if (!stepScale.isFinite()) return
        remainder += stepScale.coerceIn(0f, 1.5f)
        while (remainder >= STEP) {
            advance(streaming, agentSlots, crayfishRouting, hovering)
            remainder -= STEP
        }
    }

    private fun advance(
        streaming: Boolean,
        agentSlots: List<dev.agentdeck.terrarium.CreatureSlot>,
        crayfishRouting: Boolean,
        hovering: Boolean,
    ) {
        val dt = STEP
        time += 0.08f * dt // match previous time scale
        val maxSpeed = if (hovering) 0.008f else if (streaming) MAX_SPEED_STREAMING else MAX_SPEED_CIRCLING

        // Lissajous school centers
        val ampScale = if (streaming) 0.4f else 1.0f
        val baseXA = if (streaming) 0.42f else 0.35f
        val baseXB = if (streaming) 0.48f else 0.55f
        val baseYA = if (streaming) 0.38f else 0.35f
        val baseYB = if (streaming) 0.38f else 0.40f
        var cxA = baseXA + 0.18f * ampScale * kotlin.math.sin(time * 0.15f).toFloat()
        var cyA = baseYA + 0.12f * ampScale * kotlin.math.sin(time * 0.21f).toFloat()
        var cxB = baseXB + 0.18f * ampScale * kotlin.math.cos(time * 0.13f).toFloat()
        var cyB = baseYB + 0.12f * ampScale * kotlin.math.cos(time * 0.18f).toFloat()

        // Agent pull on school centers
        if (streaming && agentSlots.isNotEmpty()) {
            // Multi-agent: school A→agent[0], school B→agent[min(1, last)]
            val slotA = agentSlots[0]
            val slotB = if (agentSlots.size > 1) agentSlots[1] else agentSlots[0]
            cxA += (slotA.centerXFraction - cxA) * AGENT_PULL
            cyA += (slotA.centerYFraction - cyA) * AGENT_PULL
            cxB += (slotB.centerXFraction - cxB) * AGENT_PULL
            cyB += (slotB.centerYFraction - cyB) * AGENT_PULL
        } else if (!streaming && agentSlots.isNotEmpty()) {
            // CIRCLING: weak pull
            val pull = 0.15f
            val slot = agentSlots[0]
            cxA += (slot.centerXFraction - cxA) * pull
            cyA += (slot.centerYFraction - cyA) * pull
            cxB += (slot.centerXFraction - cxB) * pull
            cyB += (slot.centerYFraction - cyB) * pull
        }

        // Crayfish pull
        if (crayfishRouting) {
            cxA += (0.75f - cxA) * CRAYFISH_PULL
            cyA += (0.55f - cyA) * CRAYFISH_PULL
            cxB += (0.75f - cxB) * CRAYFISH_PULL
            cyB += (0.55f - cyB) * CRAYFISH_PULL
        }

        if (hovering) {
            cxA = 0.45f; cyA = 0.35f
            cxB = 0.55f; cyB = 0.45f
        }
        // All neighbours must come from the same simulation instant.
        fish.forEachIndexed { i, f ->
            previous[i * 4] = f.x; previous[i * 4 + 1] = f.y
            previous[i * 4 + 2] = f.vx; previous[i * 4 + 3] = f.vy
        }

        for (f in fish) {
            var ax = 0f; var ay = 0f

            // -- Separation (all fish) --
            for ((j, other) in fish.withIndex()) {
                if (other === f) continue
                val dx = f.x - previous[j * 4]; val dy = f.y - previous[j * 4 + 1]
                val dist = kotlin.math.sqrt(dx * dx + dy * dy)
                if (dist < SEPARATION_DIST && dist > 0.001f) {
                    ax += (dx / dist) * SEPARATION_WEIGHT * (1f - dist / SEPARATION_DIST)
                    ay += (dy / dist) * SEPARATION_WEIGHT * (1f - dist / SEPARATION_DIST)
                }
            }

            // -- Alignment + Cohesion (same school only) --
            var avgVx = 0f; var avgVy = 0f; var avgX = 0f; var avgY = 0f; var n = 0
            for ((j, other) in fish.withIndex()) {
                if (other === f || other.schoolId != f.schoolId) continue
                avgVx += previous[j * 4 + 2]; avgVy += previous[j * 4 + 3]
                avgX += previous[j * 4]; avgY += previous[j * 4 + 1]; n++
            }
            if (n > 0) {
                avgVx /= n; avgVy /= n; avgX /= n; avgY /= n
                // Alignment: steer toward average heading
                ax += (avgVx - f.vx) * ALIGNMENT_WEIGHT
                ay += (avgVy - f.vy) * ALIGNMENT_WEIGHT
                // Cohesion: steer toward center of school-mates
                ax += (avgX - f.x) * COHESION_WEIGHT
                ay += (avgY - f.y) * COHESION_WEIGHT
            }

            // -- School attractor (Lissajous center) --
            val centerX = if (f.schoolId == 0) cxA else cxB
            val centerY = if (f.schoolId == 0) cyA else cyB
            ax += (centerX - f.x) * SCHOOL_ATTRACTOR_WEIGHT * maxSpeed
            ay += (centerY - f.y) * SCHOOL_ATTRACTOR_WEIGHT * maxSpeed

            // -- Wall repulsion --
            if (f.x < MIN_X + WALL_MARGIN) ax += WALL_FORCE
            if (f.x > MAX_X - WALL_MARGIN) ax -= WALL_FORCE
            if (f.y < MIN_Y + WALL_MARGIN) ay += WALL_FORCE
            if (f.y > MAX_Y - WALL_MARGIN) ay -= WALL_FORCE

            // Close neighbours cannot reverse a fish in a single display frame.
            val acceleration = kotlin.math.hypot(ax, ay)
            if (acceleration > MAX_ACCELERATION) {
                ax *= MAX_ACCELERATION / acceleration
                ay *= MAX_ACCELERATION / acceleration
            }
            // Apply acceleration
            f.vx += ax * dt; f.vy += ay * dt
            // Vertical damping (fish prefer horizontal movement)
            val damping = (1f - (1f - VY_DAMPING) * dt).coerceIn(0f, 1f)
            f.vy *= damping

            // Speed limit
            val speed = kotlin.math.sqrt(f.vx * f.vx + f.vy * f.vy)
            if (speed > maxSpeed) {
                f.vx = f.vx / speed * maxSpeed
                f.vy = f.vy / speed * maxSpeed
            }

            if (speed > 0.0002f) {
                val target = Math.toDegrees(kotlin.math.atan2(f.vy, f.vx).toDouble()).toFloat()
                val turn = ((target - f.heading + 540f) % 360f) - 180f
                f.heading = (f.heading + turn.coerceIn(-30f * dt, 30f * dt) + 360f) % 360f
            }
            // Integrate position
            f.x = (f.x + f.vx * dt).coerceIn(MIN_X, MAX_X)
            f.y = (f.y + f.vy * dt).coerceIn(MIN_Y, MAX_Y)
        }
    }
}

