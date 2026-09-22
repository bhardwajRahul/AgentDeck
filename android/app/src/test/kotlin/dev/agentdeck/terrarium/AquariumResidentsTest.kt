package dev.agentdeck.terrarium

import dev.agentdeck.net.AgentState
import dev.agentdeck.state.DashboardState
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import android.content.Context
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.json.JSONObject

@RunWith(RobolectricTestRunner::class)
class AquariumResidentsTest {
    @Test fun `Kiro gets its own model while unknown types never borrow another mark`() {
        for (kind in listOf("kiro-cli", "kiro-ide")) {
            val state = DashboardState(agentState = AgentState.PROCESSING, agentType = kind,
                sessionId = "self").toTerrariumState()
            assertEquals("kiro", aquariumResidents(state).single().kind)
        }
        val unknown = DashboardState(agentState = AgentState.PROCESSING, agentType = "future-agent",
            sessionId = "self").toTerrariumState()
        assertTrue(aquariumResidents(unknown).isEmpty())
    }

    @Test fun `crowds remain bounded but focus and waiting sessions stay reachable`() {
        val idle = (0..47).map { AquariumResident("s$it", "codex", "Same project", OctopusVisualState.FLOATING) }
        val waiting = idle[20].copy(state = OctopusVisualState.ASKING)
        val items = idle.map { if (it.id == waiting.id) waiting else it }
        val visible = visibleAquariumResidents(items, "s47")
        assertEquals(TerrariumRules.NATIVE_RESIDENT_LIMIT, visible.size)
        assertEquals("s47", visible.first().id)
        assertEquals(waiting, visible[1])
        assertEquals(visible, visibleAquariumResidents(items.reversed(), "s47"))
        assertEquals(48, items.size) // no project-name merging of independent sessions
    }

    @Test fun `exported templates contain only their original character hierarchy`() {
        val context = RuntimeEnvironment.getApplication()
        for (kind in listOf("claudecode", "codex", "openclaw", "opencode", "antigravity", "kiro")) {
            val bytes = context.assets.open("residents/$kind.glb").use { it.readBytes() }
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals(0x46546C67, buffer.int)
            buffer.position(12)
            val length = buffer.int
            assertEquals(0x4E4F534A, buffer.int)
            val json = JSONObject(String(bytes, 20, length, Charsets.UTF_8))
            val nodes = json.getJSONArray("nodes")
            val names = (0 until nodes.length()).map { nodes.getJSONObject(it).optString("name") }
            assertTrue(names.contains("resident_$kind"))
            assertFalse("Blender default scene leaked into $kind", names.contains("Cube"))
            assertEquals(1, json.getJSONArray("scenes").getJSONObject(0).getJSONArray("nodes").length())
            assertTrue(json.getJSONArray("meshes").length() > 0)
        }
    }
}
