package com.obdnav.bridge

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView

/**
 * Ortak görünüm: ESP ekranıyla aynı palet (koyu zemin, camgöbeği vurgu),
 * yuvarlatılmış kartlar, dokunma dalgalı düğmeler. androidx olmadan.
 */
object Ui {
    const val BG = 0xFF05090E.toInt()
    const val SURFACE = 0xFF0D141D.toInt()
    const val SURFACE_HI = 0xFF152131.toInt()
    const val BORDER = 0xFF1F2D3F.toInt()
    const val PRIMARY = 0xFF00E5FF.toInt()
    const val ACCENT = 0xFFFF9100.toInt()
    const val OK = 0xFF00E676.toInt()
    const val WARN = 0xFFFFC400.toInt()
    const val CRIT = 0xFFFF5252.toInt()
    const val TEXT = 0xFFEEF3F8.toInt()
    const val DIM = 0xFF7C8DA3.toInt()

    enum class Style { PRIMARY, SECONDARY, GHOST }

    fun dp(ctx: Context, v: Number): Int = (v.toFloat() * ctx.resources.displayMetrics.density + 0.5f).toInt()

    fun rounded(ctx: Context, color: Int, radiusDp: Int, stroke: Int? = null, strokeDp: Int = 1) =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dp(ctx, radiusDp).toFloat()
            stroke?.let { setStroke(dp(ctx, strokeDp), it) }
        }

    fun applyWindow(a: Activity) {
        a.window.statusBarColor = BG
        a.window.navigationBarColor = BG
        a.window.decorView.setBackgroundColor(BG)
    }

    fun lp(ctx: Context, w: Int = ViewGroup.LayoutParams.MATCH_PARENT, h: Int = ViewGroup.LayoutParams.WRAP_CONTENT,
           bottomDp: Int = 0, weight: Float = 0f) =
        LinearLayout.LayoutParams(w, h, weight).apply { bottomMargin = dp(ctx, bottomDp) }

    fun column(ctx: Context, padDp: Int = 0) = LinearLayout(ctx).apply {
        orientation = LinearLayout.VERTICAL
        val p = dp(ctx, padDp)
        setPadding(p, p, p, p)
    }

    fun row(ctx: Context) = LinearLayout(ctx).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }

    fun card(ctx: Context, accentStroke: Int? = null) = column(ctx, 16).apply {
        background = rounded(ctx, SURFACE, 18, accentStroke ?: BORDER)
    }

    fun text(ctx: Context, s: CharSequence, sp: Float = 15f, color: Int = TEXT, bold: Boolean = false) =
        TextView(ctx).apply {
            text = s
            textSize = sp
            setTextColor(color)
            if (bold) typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
        }

    /** Küçük, aralıklı büyük harf bölüm başlığı. */
    fun section(ctx: Context, s: String) = text(ctx, s.uppercase(java.util.Locale("tr")), 11f, DIM, true).apply {
        letterSpacing = 0.12f
        setPadding(dp(ctx, 4), dp(ctx, 8), 0, dp(ctx, 8))
    }

    fun dot(ctx: Context, color: Int, sizeDp: Int = 10) = View(ctx).apply {
        background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(color) }
        layoutParams = LinearLayout.LayoutParams(dp(ctx, sizeDp), dp(ctx, sizeDp)).apply { marginEnd = dp(ctx, 10) }
    }

    fun setDot(v: View, color: Int) {
        (v.background as GradientDrawable).setColor(color)
    }

    fun button(ctx: Context, label: String, style: Style = Style.SECONDARY, onClick: () -> Unit) =
        TextView(ctx).apply {
            text = label
            textSize = 15f
            gravity = Gravity.CENTER
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            val (bg, fg, stroke) = when (style) {
                Style.PRIMARY -> Triple(PRIMARY, BG, null)
                Style.SECONDARY -> Triple(SURFACE_HI, TEXT, BORDER)
                Style.GHOST -> Triple(0x00000000, PRIMARY, null)
            }
            setTextColor(fg)
            val content = rounded(ctx, bg, 14, stroke)
            background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), content, rounded(ctx, 0xFFFFFFFF.toInt(), 14))
            val v = dp(ctx, if (style == Style.GHOST) 10 else 14)
            setPadding(dp(ctx, 16), v, dp(ctx, 16), v)
            isClickable = true
            isFocusable = true
            setOnClickListener { onClick() }
        }

    /** Ekran başlığı (geri ok + başlık) — alt ekranlar için. */
    fun topBar(a: Activity, title: String): LinearLayout = row(a).apply {
        setPadding(dp(a, 8), dp(a, 12), dp(a, 16), dp(a, 12))
        addView(text(a, "‹", 30f, PRIMARY).apply {
            setPadding(dp(a, 12), 0, dp(a, 16), dp(a, 4))
            isClickable = true
            setOnClickListener { a.finish() }
        })
        addView(text(a, title, 20f, TEXT, true), lp(a, 0, weight = 1f))
    }

    // --- ayar ekranı yapı taşları ---------------------------------------------

    /** Ayar grubu kartı: başlık (+ isteğe bağlı sağ rozet) ve gri açıklama. */
    fun group(ctx: Context, title: String, helper: String? = null, badge: TextView? = null) = card(ctx).apply {
        addView(row(ctx).apply {
            addView(text(ctx, title, 17f, TEXT, true), lp(ctx, 0, weight = 1f))
            badge?.let { addView(it) }
        })
        helper?.let { addView(text(ctx, it, 13f, DIM).apply { setPadding(0, dp(ctx, 2), 0, 0) }) }
    }

    /** Küçük sayaç rozeti ("3/10"). */
    fun badge(ctx: Context) = text(ctx, "", 12f, PRIMARY, true).apply {
        background = rounded(ctx, SURFACE_HI, 10, BORDER)
        setPadding(dp(ctx, 10), dp(ctx, 3), dp(ctx, 10), dp(ctx, 3))
    }

    /** Grup içi alt başlık (üstte boşlukla). */
    fun label(ctx: Context, s: String, sub: String? = null) = column(ctx).apply {
        setPadding(0, dp(ctx, 16), 0, dp(ctx, 8))
        addView(text(ctx, s, 14f, TEXT, true))
        sub?.let { addView(text(ctx, it, 12f, DIM).apply { setPadding(0, dp(ctx, 1), 0, 0) }) }
    }

    fun divider(ctx: Context) = View(ctx).apply {
        setBackgroundColor(BORDER)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 1).apply { topMargin = dp(ctx, 14) }
    }

    /** Bölümlü seçici (2–5 seçenek); seçili = camgöbeği dolgu. Etiket "\n" ile iki satır olabilir. */
    fun segmented(ctx: Context, labels: List<String>, selected: Int, onSelect: (Int) -> Unit): LinearLayout {
        val box = row(ctx).apply {
            background = rounded(ctx, SURFACE_HI, 12, BORDER)
            val p = dp(ctx, 3)
            setPadding(p, p, p, p)
        }
        val items = labels.map { l ->
            text(ctx, l, 13.5f, DIM).apply {
                gravity = Gravity.CENTER
                maxLines = 2
                setPadding(dp(ctx, 4), dp(ctx, 9), dp(ctx, 4), dp(ctx, 9))
                isClickable = true
            }
        }
        fun paint(sel: Int) = items.forEachIndexed { i, v ->
            val on = i == sel
            v.background = if (on) rounded(ctx, PRIMARY, 10) else null
            v.setTextColor(if (on) BG else DIM)
            v.typeface = Typeface.create("sans-serif-medium", if (on) Typeface.BOLD else Typeface.NORMAL)
        }
        var cur = selected
        items.forEachIndexed { i, v ->
            v.setOnClickListener { if (i != cur) { cur = i; paint(i); onSelect(i) } }
            box.addView(v, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f))
        }
        paint(selected)
        return box
    }

    /** Seçilebilir hap: açık = camgöbeği çerçeve + ton; removable = sağda ×. */
    fun chip(ctx: Context, label: String, on: Boolean, removable: Boolean = false, onClick: () -> Unit) =
        text(ctx, if (removable) "$label  ✕" else label, 14f, if (on) PRIMARY else DIM, on).apply {
            background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF),
                rounded(ctx, if (on) 0x2200E5FF else SURFACE_HI, 20, if (on) PRIMARY else BORDER),
                rounded(ctx, 0xFFFFFFFF.toInt(), 20))
            setPadding(dp(ctx, 14), dp(ctx, 8), dp(ctx, 14), dp(ctx, 8))
            isClickable = true
            setOnClickListener { onClick() }
        }

    /** Başlık + açıklama + anahtar; satırın tamamı dokunulabilir. */
    fun switchRow(ctx: Context, title: String, sub: String?, checked: Boolean, onChange: (Boolean) -> Unit) = row(ctx).apply {
        setPadding(0, dp(ctx, 14), 0, dp(ctx, 2))
        addView(column(ctx).apply {
            addView(text(ctx, title, 14f, TEXT, true))
            sub?.let { addView(text(ctx, it, 12f, DIM).apply { setPadding(0, dp(ctx, 1), dp(ctx, 12), 0) }) }
        }, lp(ctx, 0, weight = 1f))
        val sw = Switch(ctx).apply {
            isChecked = checked
            val states = arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf())
            thumbTintList = ColorStateList(states, intArrayOf(PRIMARY, DIM))
            trackTintList = ColorStateList(states, intArrayOf(0x8800E5FF.toInt(), BORDER))
            setOnCheckedChangeListener { _, b -> onChange(b) }
        }
        addView(sw)
        isClickable = true
        setOnClickListener { sw.toggle() }
    }

    /** − değer + adımlayıcı; basılı tutunca tekrarlar, değere dokununca onValue (ör. elle giriş). */
    fun stepper(ctx: Context, value: TextView, onStep: (Int) -> Unit, onValue: (() -> Unit)? = null) = row(ctx).apply {
        background = rounded(ctx, SURFACE_HI, 12, BORDER)
        val h = Handler(Looper.getMainLooper())
        fun btn(s: String, dir: Int) = text(ctx, s, 22f, PRIMARY, true).apply {
            gravity = Gravity.CENTER
            setPadding(dp(ctx, 20), dp(ctx, 6), dp(ctx, 20), dp(ctx, 8))
            val rep = object : Runnable {
                override fun run() { onStep(dir); h.postDelayed(this, 70) }
            }
            isClickable = true
            setOnClickListener { onStep(dir) }
            setOnLongClickListener { h.post(rep); true }
            setOnTouchListener { _, e ->
                if (e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL) h.removeCallbacks(rep)
                false
            }
        }
        addView(btn("−", -1))
        addView(value.apply {
            gravity = Gravity.CENTER
            textSize = 17f
            setTextColor(TEXT)
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            onValue?.let { f -> isClickable = true; setOnClickListener { f() } }
        }, lp(ctx, 0, weight = 1f))
        addView(btn("+", 1))
    }

    /** Koyu metin alanı. */
    fun input(ctx: Context, value: String, hint: String) = EditText(ctx).apply {
        setText(value)
        this.hint = hint
        textSize = 16f
        setTextColor(TEXT)
        setHintTextColor(DIM)
        isSingleLine = true
        background = rounded(ctx, SURFACE_HI, 12, BORDER)
        setPadding(dp(ctx, 14), dp(ctx, 12), dp(ctx, 14), dp(ctx, 12))
    }

    /** Hapları satıra sığdığınca dizen, taşanı alt satıra geçiren akış düzeni. */
    class FlowLayout(ctx: Context, private val gapDp: Int = 8) : ViewGroup(ctx) {
        private fun place(maxW: Int, apply: (View, Int, Int) -> Unit): Int {
            val gap = dp(context, gapDp)
            var x = 0; var y = 0; var rowH = 0
            for (i in 0 until childCount) {
                val c = getChildAt(i)
                if (c.visibility == GONE) continue
                if (x > 0 && x + c.measuredWidth > maxW) { x = 0; y += rowH + gap; rowH = 0 }
                apply(c, x, y)
                x += c.measuredWidth + gap
                rowH = maxOf(rowH, c.measuredHeight)
            }
            return y + rowH
        }

        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val w = MeasureSpec.getSize(widthMeasureSpec)
            val maxW = w - paddingLeft - paddingRight
            val spec = MeasureSpec.makeMeasureSpec(maxW, MeasureSpec.AT_MOST)
            for (i in 0 until childCount) getChildAt(i).measure(spec, MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED))
            setMeasuredDimension(w, place(maxW) { _, _, _ -> } + paddingTop + paddingBottom)
        }

        override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
            place(r - l - paddingLeft - paddingRight) { c, x, y ->
                c.layout(paddingLeft + x, paddingTop + y, paddingLeft + x + c.measuredWidth, paddingTop + y + c.measuredHeight)
            }
        }
    }
}
