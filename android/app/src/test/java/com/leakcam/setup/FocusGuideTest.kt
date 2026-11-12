package com.leakcam.setup

import com.leakcam.setup.FocusGuide.Say
import com.leakcam.setup.FocusGuide.Turn
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/**
 * The lens guide against a simulated lens (DESIGN.md 5.3.17, tests/test146 step 1): sharpness peaks at `peak` (the
 * lens position in degrees of rotation), the person turns `step` degrees per score the way the guide says and stops
 * when it says STOP; the board's score has `noise` relative noise per frame.
 */
class FocusGuideTest {
    private fun lens(pos: Double, peak: Double) = 100.0 / (1 + ((pos - peak) / 30.0).let { it * it }) + 10.0

    /** runs until STOP held for 10 scores or `limit` scores; the lens position at the end and what was said */
    private fun run(peak: Double, noise: Double, step: Double = 2.0, limit: Int = 3000, seed: Int = 1,
                    scene: (Double) -> Double = { lens(it, peak) }): Pair<Double, List<Say>> {
        val g = FocusGuide()
        val rnd = Random(seed)
        var pos = 0.0
        val said = mutableListOf(g.say)
        var held = 0
        repeat(limit) {
            if (g.add(scene(pos) * (1 + noise * (rnd.nextDouble() * 2 - 1))) && g.say != said.last())
                said += g.say
            when (g.turn) {
                Turn.CCW -> pos += step
                Turn.CW -> pos -= step
                Turn.HOLD -> if (++held >= 10) return pos to said
            }
            if (g.turn != Turn.HOLD) held = 0
        }
        return pos to said
    }

    @Test fun peakTheFirstWay() {
        val (pos, said) = run(peak = 120.0, noise = 0.0)
        assertEquals(Say.STOP, said.last())
        assertTrue("at $pos, peak 120", kotlin.math.abs(pos - 120.0) < 12)
        assertTrue(Say.WRONG_WAY !in said)
        assertTrue(Say.TOO_FAR in said)        // the peak is found by passing it once
    }

    @Test fun peakTheOtherWayIsWrongWayFirst() {
        val (pos, said) = run(peak = -150.0, noise = 0.0)
        assertEquals(Say.WRONG_WAY, said.first { it != Say.START })
        assertEquals(Say.STOP, said.last())
        assertTrue("at $pos, peak -150", kotlin.math.abs(pos + 150.0) < 12)
    }

    @Test fun noisyBoardStillConverges() {
        for (seed in 1..20) {
            val (pos, said) = run(peak = 90.0, noise = 0.03, seed = seed)
            assertEquals("seed $seed: $said", Say.STOP, said.last())
            assertTrue("seed $seed at $pos", kotlin.math.abs(pos - 90.0) < 15)
        }
    }

    @Test fun anyHandSpeedAndNoise() {               // a slow or quick hand (1-4 deg per score), noise up to 5 %
        for (step in listOf(1.0, 2.0, 4.0)) for (noise in listOf(0.0, 0.03, 0.05)) for (peak in listOf(-100.0, 70.0)) {
            val (pos, said) = run(peak = peak, noise = noise, step = step, seed = (step * 100 + noise * 1000).toInt())
            assertEquals("step $step noise $noise peak $peak: $said", Say.STOP, said.last())
            assertTrue("step $step noise $noise peak $peak at $pos", kotlin.math.abs(pos - peak) < 15)
        }
    }

    @Test fun startingAtThePeak() {
        val (pos, said) = run(peak = 0.0, noise = 0.01)
        assertEquals(Say.STOP, said.last())
        assertTrue("at $pos", kotlin.math.abs(pos) < 12)
    }

    @Test fun nothingToFocusOn() {
        val (_, said) = run(peak = 0.0, noise = 0.03, limit = 400, scene = { 10.0 })   // flat: only noise
        assertTrue("$said", Say.NO_DETAIL in said)
    }

    @Test fun tooFastIsTold() {
        val g = FocusGuide()
        repeat(5) { g.add(50.0) }
        repeat(3) { g.add(100.0) }            // the median jumps 50 -> 100 in one update
        assertTrue(g.slower)
    }
}
