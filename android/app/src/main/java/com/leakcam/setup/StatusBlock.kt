package com.leakcam.setup

import android.view.View
import android.widget.TextView

/**
 * The status block under a screen's action row (res/layout/status_block.xml, DESIGN.md 5.3.4): the same on every
 * screen (user 2026-11-07: "status is below buttons", what happens now is status too). busy() shows the spinner line
 * while something runs (every wait has a spinner: the 15 s rule), text is the message. The setup screen has it at
 * the very bottom, under the found boards (user 2026-11-08).
 */
class StatusBlock(val root: View) {
    val text: TextView = root.findViewById(R.id.status_text)
    private val busyRow: View = root.findViewById(R.id.busy_row)
    private val busyText: TextView = root.findViewById(R.id.busy_text)

    /** the spinner line with what happens now; null: nothing runs, the line goes */
    fun busy(what: String?) {
        busyRow.visibility = if (what == null) View.GONE else View.VISIBLE
        busyText.text = what.orEmpty()
    }

    /** a problem (red) or plain help */
    fun error(on: Boolean) =
        root.setBackgroundColor(root.context.getColor(if (on) R.color.error_background else R.color.help_background))
}
