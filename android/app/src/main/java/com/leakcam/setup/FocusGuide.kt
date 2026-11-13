package com.leakcam.setup

/**
 * Which way to turn a camera's lens (DESIGN.md 5.3.17, user 2026-11-12: "help user with direction where to rotate
 * lens", "if focus gets worse swap direction"): a hill climb on the board's sharpness score, the person's hand the
 * motor. Pure logic (no Android), fed the K230's scores in order (FOC lines, once a second:
 * user 2026-11-13 "once per second no need 15 times"; the live video shows the rest); FocusActivity shows what
 * it says. Unit tests: src/test/.../FocusGuideTest.kt.
 *
 *   START      turn one way, picked at random (user 2026-11-13: "rotation direction randomly initially"): no way is
 *              likelier right than the other
 *   KEEP_GOING this way makes it sharper
 *   WRONG_WAY  the score fell from where this direction began and never rose: turn the other way
 *   TOO_FAR    it rose, then fell past the best: the peak is behind, turn back slowly
 *   STOP       back within GOOD of the best after a peak was found: hold, sharpest here
 *   NO_DETAIL  the score neither rose nor fell for FLAT updates: nothing in view to focus on (a blank wall, the lens
 *              covered)
 * Once the peak was passed, a fall in either direction means it was passed again (TOO_FAR), and the best is the
 * last pass's: a noise-high best from an earlier pass would make STOP unreachable.
 * `slower`: the score jumped between two updates, the hand turns too fast for the camera to follow.
 *
 * Each score is one frame's (no smoothing: at one a second a median would lag seconds behind the hand); DROP and RISE
 * stay well above the board's noise (2-3 %).
 */
class FocusGuide(first: Turn = if (kotlin.random.Random.nextBoolean()) Turn.CCW else Turn.CW) {
    enum class Turn { CCW, CW, HOLD }
    enum class Say { START, KEEP_GOING, WRONG_WAY, TOO_FAR, STOP, NO_DETAIL }

    companion object {
        const val SMOOTH = 1            // scores in the median (one a second: none)
        const val DROP = 0.08           // a fall this far below the reference counts (noise of the median ~2-3 %)
        const val RISE = 0.08           // a rise this far above the start counts as "getting sharper"
        const val GOOD = 0.97           // within 3 % of the best seen: sharp
        const val JUMP = 0.30           // the score moved this much in one second: turning too fast
        const val FLAT = 10             // updates (~10 s) without a rise or a fall: nothing to focus on
    }

    var turn = first; private set
    var say = Say.START; private set
    var slower = false; private set
    var best = 0.0; private set         // the best smoothed score so far (the bar's marker; Done keeps it)
    var score = 0.0; private set        // the smoothed score now (the bar)
    var peaks = 0; private set          // times the peak was passed
    val sharp get() = say == Say.STOP

    private val window = ArrayDeque<Double>()
    private var start = 0.0             // the score where this direction began
    private var rose = false            // this direction made it sharper by RISE at least
    private var passBest = 0.0          // the best of this direction (the next pass's reference once the peak is known)
    private var quiet = 0               // updates since the score last rose or fell
    private var hint = Say.START        // the current direction's own message (shown while nothing else applies)
    private var last = first            // the direction before STOP

    /** one score from the board; true when what to show changed */
    fun add(raw: Double): Boolean {
        window.addLast(raw)
        if (window.size > SMOOTH)
            window.removeFirst()
        if (window.size < SMOOTH)
            return false                // no direction from a partial window
        val prev = score
        score = window.sorted()[SMOOTH / 2]
        if (start == 0.0) {             // the first full window: where the person starts
            start = score
            best = score
            passBest = score
            return true
        }
        val before = Triple(say, turn, slower)
        best = maxOf(best, score)
        passBest = maxOf(passBest, score)
        if (score >= start * (1 + RISE))
            rose = true
        val fell = score < (if (rose || peaks > 0) passBest else start) * (1 - DROP)
        quiet = if (rose || fell) 0 else quiet + 1
        slower = prev > 0 && kotlin.math.abs(score - prev) > JUMP * prev
        say = when {
            peaks > 0 && score >= best * GOOD -> {
                if (turn != Turn.HOLD) last = turn
                turn = Turn.HOLD
                Say.STOP
            }
            turn == Turn.HOLD -> {      // drifted off the sharp point: most likely on past it, so back
                turn = other(last)
                begin(Say.TOO_FAR)
            }
            fell && (rose || peaks > 0) -> {     // the peak is behind
                peaks++
                best = passBest              // this pass held the peak: its best is the one to come back to
                turn = other(turn)
                begin(Say.TOO_FAR)
            }
            fell -> {
                turn = other(turn)
                begin(Say.WRONG_WAY)
            }
            rose -> Say.KEEP_GOING
            peaks == 0 && quiet >= FLAT -> Say.NO_DETAIL
            else -> hint
        }
        return Triple(say, turn, slower) != before
    }

    private fun other(t: Turn) = if (t == Turn.CCW) Turn.CW else Turn.CCW

    /** a new direction from here: its start is the score now, the best stays */
    private fun begin(why: Say): Say {
        start = score
        passBest = score
        rose = false
        hint = why
        return why
    }
}
