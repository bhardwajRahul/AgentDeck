package dev.agentdeck.terrarium.renderer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.hypot

class EinkAnimationTimingTest {

    @Test
    fun `color e-ink animation uses video-like cadence`() {
        assertEquals(400L, einkAnimationFrameIntervalMs(colorEink = false))
        assertEquals(100L, einkAnimationFrameIntervalMs(colorEink = true))
    }

    @Test
    fun `animation frame advance is elapsed-time based and bounded`() {
        assertEquals(0.25f, einkAnimationFrameAdvance(100L), 0.001f)
        assertEquals(1.0f, einkAnimationFrameAdvance(400L), 0.001f)
        assertEquals(1.5f, einkAnimationFrameAdvance(5_000L), 0.001f)
    }

    @Test
    fun `fish simulation scales movement for partial color frames`() {
        val fullStepSchool = EinkFishSchool()
        val partialStepSchool = EinkFishSchool()
        val initial = fullStepSchool.fish.map { it.x to it.y }

        fullStepSchool.update(
            streaming = false,
            agentSlots = emptyList(),
            crayfishRouting = false,
            stepScale = 1f,
        )
        partialStepSchool.update(
            streaming = false,
            agentSlots = emptyList(),
            crayfishRouting = false,
            stepScale = 0.25f,
        )

        val fullDistance = totalDistance(initial, fullStepSchool)
        val partialDistance = totalDistance(initial, partialStepSchool)

        assertTrue(fullDistance > 0f)
        assertTrue("partial frames should interpolate instead of sprinting", partialDistance < fullDistance * 0.5f)
    }

    @Test
    fun `fish trajectory is identical across display cadences`() {
        val mono = EinkFishSchool()
        val color = EinkFishSchool()
        repeat(300) {
            mono.update(false, emptyList(), false, 1f)
            repeat(4) { color.update(false, emptyList(), false, 0.25f) }
        }
        mono.fish.zip(color.fish).forEach { (a, b) ->
            assertEquals(a.x, b.x, 0.000001f)
            assertEquals(a.y, b.y, 0.000001f)
            assertEquals(a.heading, b.heading, 0.000001f)
        }
    }

    @Test
    fun `fish turn gradually and hovering preserves positions`() {
        val school = EinkFishSchool()
        repeat(600) { frame ->
            val before = school.fish.map { Triple(it.x, it.y, it.heading) }
            school.update(frame < 200, emptyList(), false, hovering = frame in 200..399)
            school.fish.zip(before).forEach { (fish, old) ->
                assertTrue(hypot(fish.x - old.first, fish.y - old.second) <= 0.0251f)
                val turn = ((fish.heading - old.third + 540f) % 360f) - 180f
                assertTrue("turn must not flip at display cadence", kotlin.math.abs(turn) <= 30.001f)
                assertTrue(fish.x in 0.04f..0.96f && fish.y in 0.10f..0.70f)
            }
        }
    }

    private fun totalDistance(initial: List<Pair<Float, Float>>, school: EinkFishSchool): Float =
        school.fish.zip(initial).sumOf { (fish, start) ->
            hypot((fish.x - start.first).toDouble(), (fish.y - start.second).toDouble())
        }.toFloat()
}
