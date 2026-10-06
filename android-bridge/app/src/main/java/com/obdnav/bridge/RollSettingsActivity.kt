package com.obdnav.bridge

import android.app.Activity
import android.app.AlertDialog
import android.os.Bundle
import android.text.Editable
import android.text.InputFilter
import android.text.InputType
import android.text.TextWatcher
import android.view.View
import android.view.WindowManager
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import kotlin.math.roundToInt

/**
 * ROLL kişisel ayarları: her değişiklik anında kaydedilir ve (bağlıysa)
 * ESP'ye gönderilir. Hızlar her zaman km/h saklanır; mph yalnız gösterim.
 */
class RollSettingsActivity : Activity() {
    private val cfg get() = RollSettings.cfg
    /** Her değişiklikten sonra yenilenecek parçalar (özet, haplar, koşullu satırlar). */
    private val refreshers = mutableListOf<() -> Unit>()

    private lateinit var syncDot: View
    private lateinit var syncText: TextView

    private val spdPresets = listOf(0 to 50, 0 to 100, 0 to 150, 0 to 200, 60 to 100, 60 to 120, 80 to 120,
        100 to 150, 100 to 200, 60 to 150, 60 to 200, 80 to 250, 150 to 250, 200 to 300).map { SpeedRange(it.first, it.second) }
    private val dstPresets = listOf(18.288 to "60 ft", 100.0 to "100 m", 201.168 to "201 m · 1/8 mil",
        402.336 to "402 m · 1/4 mil", 804.672 to "1/2 mil", 1000.0 to "1000 m")
    private val brkPresets = listOf(60, 100, 200)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        RollSettings.init(this)
        Ui.applyWindow(this)
        build()
    }

    override fun onResume() {
        super.onResume()
        RollSettings.listener = { showSync() }
        showSync()
    }

    override fun onPause() {
        super.onPause()
        RollSettings.listener = null
    }

    private fun build() {
        refreshers.clear()
        val root = Ui.column(this)
        root.addView(Ui.topBar(this, "ROLL ayarları"))
        val col = Ui.column(this, 16).apply { isFocusableInTouchMode = true }   // ad alanı açılışta odak almasın
        col.addView(summary(), Ui.lp(this, bottomDp = 8))

        col.addView(Ui.section(this, "Ölçüm"))
        col.addView(source(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Hedefler"))
        col.addView(speedTargets(), Ui.lp(this, bottomDp = 12))
        col.addView(distanceTargets(), Ui.lp(this, bottomDp = 12))
        col.addView(brakeTargets(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Başlatma"))
        col.addView(startCard(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Doğrulama"))
        col.addView(validation(), Ui.lp(this, bottomDp = 12))
        col.addView(obdCorrection(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Araç"))
        col.addView(vehicle(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Ses ve ekran"))
        col.addView(soundScreen(), Ui.lp(this, bottomDp = 12))

        col.addView(Ui.section(this, "Kayıt"))
        col.addView(logging(), Ui.lp(this, bottomDp = 20))

        col.addView(Ui.button(this, "Varsayılanlara dön", Ui.Style.SECONDARY) { confirmReset() }
            .apply { setTextColor(Ui.CRIT) }, Ui.lp(this, bottomDp = 24))

        root.addView(ScrollView(this).apply { isFillViewport = true; addView(col) })
        setContentView(root)
        refreshAll()
        showSync()
    }

    /** Ayar değişti: kaydet + gönder (300 ms birleştirilir) + ekranı yenile. */
    private fun changed() {
        RollSettings.save()
        refreshAll()
    }

    private fun refreshAll() = refreshers.forEach { it() }

    // --- üst özet ------------------------------------------------------------

    private fun summary(): View = Ui.card(this, 0x5500E5FF).apply {
        val name = Ui.text(context, "", 22f, Ui.TEXT, true)
        val line = Ui.text(context, "", 13f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 4), 0, Ui.dp(context, 12)) }
        addView(Ui.text(context, "ROLL PROFİLİ", 11f, Ui.PRIMARY, true).apply { letterSpacing = 0.12f })
        addView(name)
        addView(line)
        syncDot = Ui.dot(context, Ui.DIM, 8)
        syncText = Ui.text(context, "", 13f, Ui.DIM)
        addView(Ui.row(context).apply {
            background = Ui.rounded(context, Ui.SURFACE_HI, 10)
            setPadding(Ui.dp(context, 12), Ui.dp(context, 8), Ui.dp(context, 12), Ui.dp(context, 8))
            addView(syncDot)
            addView(syncText, Ui.lp(context, 0, weight = 1f))
        })
        refreshers += {
            name.text = cfg.name
            val src = when (cfg.src) { "gps" -> "Yalnız GPS"; "obd" -> "Yalnız OBD"; else -> "GPS öncelikli" }
            line.text = "${cfg.mass} kg · ${driveName(cfg.drive)} · $src · ${unitName()}\n" +
                "${cfg.spd.size} hız / ${cfg.dst.size} mesafe / ${cfg.brk.size} fren hedefi"
        }
    }

    private fun showSync() {
        if (!::syncText.isInitialized) return
        val (color, s) = when (RollSettings.sync) {
            RollSettings.Sync.SYNCED -> Ui.OK to "ESP ile senkron ✓"
            RollSettings.Sync.SENDING -> Ui.PRIMARY to "Gönderiliyor…"
            RollSettings.Sync.FAILED -> Ui.CRIT to "ESP reddetti${RollSettings.why.takeIf { it.isNotEmpty() }?.let { " ($it)" } ?: ""}"
            RollSettings.Sync.NOREPLY -> Ui.WARN to "ESP yanıt vermedi — cihaz yazılımı güncel mi?"
            RollSettings.Sync.OFFLINE -> Ui.DIM to "ESP bağlı değil — bağlanınca gönderilecek"
        }
        Ui.setDot(syncDot, color)
        syncText.text = s
        syncText.setTextColor(if (color == Ui.DIM) Ui.DIM else Ui.TEXT)
    }

    // --- ölçüm ----------------------------------------------------------------

    private fun source(): View = Ui.group(this, "Ölçüm kaynağı", "Hız verisinin nereden okunacağı").apply {
        val keys = listOf("auto", "gps", "obd")
        val desc = Ui.text(context, "", 12f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 8), 0, 0) }
        addView(Ui.segmented(context, listOf("GPS öncelikli", "Yalnız GPS", "Yalnız OBD"), keys.indexOf(cfg.src).coerceAtLeast(0)) {
            cfg.src = keys[it]; changed()
        }, Ui.lp(context).apply { topMargin = Ui.dp(context, 12) })
        addView(desc)
        refreshers += {
            desc.text = when (cfg.src) {
                "gps" -> "Yalnız telefonun GNSS hızı kullanılır; GPS yoksa ölçüm başlamaz."
                "obd" -> "Yalnız aracın ECU hızı (OBD) kullanılır; GPS kapalı yerlerde işe yarar, hassasiyeti düşüktür."
                else -> "Telefon GPS'i hazırsa o kullanılır, sinyal zayıfsa araç ECU hızına (OBD) geçilir."
            }
        }

        addView(Ui.label(context, "Hız birimi", "Değerler her zaman km/h olarak saklanır"))
        addView(Ui.segmented(context, listOf("km/h", "mph"), if (cfg.unit == "mph") 1 else 0) {
            cfg.unit = if (it == 1) "mph" else "kmh"; changed()
        })
    }

    // --- hedefler ---------------------------------------------------------------

    private val mph get() = cfg.unit == "mph"
    private fun unitName() = if (mph) "mph" else "km/h"
    /** km/h → gösterim birimi (yuvarlanmış). */
    private fun show(kmh: Int): Int = if (mph) (kmh / 1.609344).roundToInt() else kmh
    /** gösterim birimi → km/h. */
    private fun toKmh(v: Int): Int = if (mph) (v * 1.609344).roundToInt() else v

    private class Chip(val label: String, val on: Boolean, val custom: Boolean, val tap: () -> Unit)

    /** Sayaçlı hap grubu: önayarlar aç/kapa, özel değerler ✕ ile silinir. */
    private fun chipGroup(title: String, helper: () -> String, max: Int, count: () -> Int,
                          chips: () -> List<Chip>, addLabel: String, onAdd: () -> Unit): View {
        val badge = Ui.badge(this)
        val help = Ui.text(this, "", 13f, Ui.DIM)
        return Ui.card(this).apply {
            addView(Ui.row(context).apply {
                addView(Ui.text(context, title, 17f, Ui.TEXT, true), Ui.lp(context, 0, weight = 1f))
                addView(badge)
            })
            addView(help.apply { setPadding(0, Ui.dp(context, 2), 0, Ui.dp(context, 12)) })
            val flow = Ui.FlowLayout(context)
            addView(flow)
            addView(Ui.button(context, "+  $addLabel", Ui.Style.GHOST) {
                if (count() >= max) toast("En çok $max hedef seçilebilir") else onAdd()
            }.apply { gravity = android.view.Gravity.START or android.view.Gravity.CENTER_VERTICAL; setPadding(0, paddingTop, 0, paddingBottom) },
                Ui.lp(context).apply { topMargin = Ui.dp(context, 6) })
            refreshers += {
                val n = count()
                badge.text = "$n/$max"
                badge.setTextColor(if (n >= max) Ui.WARN else Ui.PRIMARY)
                help.text = helper()
                flow.removeAllViews()
                for (c in chips()) flow.addView(Ui.chip(context, c.label, c.on, c.custom) { c.tap() })
            }
        }
    }

    /** Hedef listesi aç/kapa (sınır kontrolüyle). */
    private fun <T> toggle(list: MutableList<T>, v: T, max: Int) {
        if (v in list) list.remove(v)
        else if (list.size >= max) { toast("En çok $max hedef seçilebilir"); return }
        else list.add(v)
        changed()
    }

    private fun speedTargets() = chipGroup("Hız aralıkları",
        { "Dokunarak seç · ${unitName()} · en çok ${RollCfg.MAX_SPD}" }, RollCfg.MAX_SPD, { cfg.spd.size },
        {
            spdPresets.map { r -> Chip("${show(r.from)}-${show(r.to)}", r in cfg.spd, false) { toggle(cfg.spd, r, RollCfg.MAX_SPD) } } +
                cfg.spd.filter { it !in spdPresets }.sortedWith(compareBy({ it.from }, { it.to })).map { r ->
                    Chip("${show(r.from)}-${show(r.to)}", true, true) { cfg.spd.remove(r); changed() }
                }
        },
        "Özel aralık ekle") { addSpeedRange() }

    private fun distanceTargets() = chipGroup("Mesafe hedefleri",
        { "Duruştan hedef mesafeye süre ve çıkış hızı · en çok ${RollCfg.MAX_DST}" }, RollCfg.MAX_DST, { cfg.dst.size },
        {
            dstPresets.map { (m, l) -> Chip(l, cfg.dst.any { same(it, m) }, false) { toggleDst(m) } } +
                cfg.dst.filter { d -> dstPresets.none { same(it.first, d) } }.sorted().map { d ->
                    Chip("${RollCfg.m(d)} m", true, true) { cfg.dst.remove(d); changed() }
                }
        },
        "Özel mesafe ekle") { addDistance() }

    private fun same(a: Double, b: Double) = kotlin.math.abs(a - b) < 0.001

    private fun toggleDst(m: Double) {
        val cur = cfg.dst.firstOrNull { same(it, m) }
        if (cur != null) { cfg.dst.remove(cur); changed() } else toggle(cfg.dst, m, RollCfg.MAX_DST)
    }

    private fun brakeTargets() = chipGroup("Frenleme",
        { "Belirtilen hızdan tam duruşa süre ve mesafe · ${unitName()} · en çok ${RollCfg.MAX_BRK}" }, RollCfg.MAX_BRK, { cfg.brk.size },
        {
            brkPresets.sortedDescending().map { v -> Chip("${show(v)}-0", v in cfg.brk, false) { toggle(cfg.brk, v, RollCfg.MAX_BRK) } } +
                cfg.brk.filter { it !in brkPresets }.sortedDescending().map { v ->
                    Chip("${show(v)}-0", true, true) { cfg.brk.remove(v); changed() }
                }
        },
        "Özel fren hızı ekle") { addBrake() }

    // --- özel değer pencereleri -------------------------------------------------

    private fun numField(hint: String, decimal: Boolean = false) = Ui.input(this, "", hint).apply {
        inputType = InputType.TYPE_CLASS_NUMBER or (if (decimal) InputType.TYPE_NUMBER_FLAG_DECIMAL else 0)
        gravity = android.view.Gravity.CENTER
    }

    /** Doğrulamalı giriş penceresi: validate null dönerse kapanır, aksi halde hata gösterir. */
    private fun inputDialog(title: String, note: String, fields: List<EditText>, validate: () -> String?) {
        val body = Ui.column(this, 20).apply {
            addView(Ui.text(context, note, 13f, Ui.DIM), Ui.lp(context, bottomDp = 12))
            addView(Ui.row(context).apply {
                fields.forEachIndexed { i, f ->
                    if (i > 0) addView(Ui.text(context, "→", 18f, Ui.DIM).apply {
                        setPadding(Ui.dp(context, 10), 0, Ui.dp(context, 10), 0)
                    })
                    addView(f, Ui.lp(context, 0, weight = 1f))
                }
            })
        }
        val d = AlertDialog.Builder(this)
            .setTitle(title)
            .setView(body)
            .setPositiveButton("Ekle", null)
            .setNegativeButton("Vazgeç", null)
            .create()
        d.setOnShowListener {
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val err = validate()
                if (err == null) d.dismiss() else toast(err)
            }
            fields.firstOrNull()?.requestFocus()
        }
        d.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE)
        d.show()
    }

    private fun addSpeedRange() {
        val a = numField("Başlangıç")
        val b = numField("Bitiş")
        val u = unitName()
        inputDialog("Özel hız aralığı", "Başlangıç ve bitiş hızı ($u). 0 = duruştan.", listOf(a, b)) {
            val from = a.text.toString().toIntOrNull()
            val to = b.text.toString().toIntOrNull()
            if (from == null || to == null) return@inputDialog "İki hızı da girin"
            val r = SpeedRange(toKmh(from), toKmh(to))
            when {
                r.from < 0 -> "Başlangıç 0 veya üstü olmalı"
                r.from >= r.to -> "Başlangıç bitişten küçük olmalı"
                r.to > 400 -> "Bitiş en çok ${show(400)} $u olabilir"
                r in cfg.spd -> "Bu aralık zaten seçili"
                cfg.spd.size >= RollCfg.MAX_SPD -> "En çok ${RollCfg.MAX_SPD} hedef seçilebilir"
                else -> { cfg.spd.add(r); changed(); null }
            }
        }
    }

    private fun addDistance() {
        val f = numField("Metre", decimal = true)
        inputDialog("Özel mesafe", "Duruştan hedef mesafe (metre, ondalık olabilir). 1 mil = 1609.344 m.", listOf(f)) {
            val m = f.text.toString().replace(',', '.').toDoubleOrNull()
                ?.let { RollCfg.m(it).toDouble() } ?: return@inputDialog "Mesafeyi girin"
            when {
                m < 5 || m > 5000 -> "Mesafe 5 – 5000 m arasında olmalı"
                cfg.dst.any { same(it, m) } -> "Bu mesafe zaten seçili"
                cfg.dst.size >= RollCfg.MAX_DST -> "En çok ${RollCfg.MAX_DST} hedef seçilebilir"
                else -> { cfg.dst.add(m); changed(); null }
            }
        }
    }

    private fun addBrake() {
        val f = numField("Başlangıç hızı")
        val u = unitName()
        inputDialog("Özel frenleme", "Frenlemeye başlanan hız ($u) → 0.", listOf(f)) {
            val v = f.text.toString().toIntOrNull()?.let(::toKmh) ?: return@inputDialog "Hızı girin"
            when {
                v < 10 || v > 400 -> "Hız ${show(10)} – ${show(400)} $u arasında olmalı"
                v in cfg.brk -> "Bu hız zaten seçili"
                cfg.brk.size >= RollCfg.MAX_BRK -> "En çok ${RollCfg.MAX_BRK} hedef seçilebilir"
                else -> { cfg.brk.add(v); changed(); null }
            }
        }
    }

    // --- başlatma -----------------------------------------------------------------

    private fun startCard(): View = Ui.group(this, "Başlatma şekli", "Ölçümün nasıl başlayacağı").apply {
        val treeBox = Ui.column(context)
        val desc = Ui.text(context, "", 12f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 8), 0, 0) }
        addView(Ui.segmented(context, listOf("Otomatik", "Geri sayım ışıkları"), if (cfg.start == "tree") 1 else 0) {
            cfg.start = if (it == 1) "tree" else "auto"; changed()
        }, Ui.lp(context).apply { topMargin = Ui.dp(context, 12) })
        addView(desc)
        treeBox.addView(Ui.label(context, "Işık aralığı", "Sarılar arası süre; reaksiyon süreniz ayrıca ölçülür"))
        treeBox.addView(Ui.segmented(context, listOf("Pro · 0.4 sn", "Sportsman · 0.5 sn"), if (cfg.tree >= 0.45) 1 else 0) {
            cfg.tree = if (it == 1) 0.5 else 0.4; changed()
        })
        addView(treeBox)
        addView(Ui.divider(context))
        addView(Ui.switchRow(context, "1 ft rollout",
            "Drag pistindeki gibi ilk 30 cm süreye sayılmaz — pist sonuçlarıyla karşılaştırmak için", cfg.ro) {
            cfg.ro = it; changed()
        })
        refreshers += {
            treeBox.visibility = if (cfg.start == "tree") View.VISIBLE else View.GONE
            desc.text = if (cfg.start == "tree")
                "Araç durunca ekranda geri sayım ışıkları yanar; yeşilde kalkın. Erken kalkış kırmızı sayılır."
            else "Araç durup sabitlenince ölçüm kendiliğinden hazırlanır; harekete geçtiğiniz an süre başlar."
        }
    }

    // --- doğrulama ------------------------------------------------------------------

    private fun validation(): View = Ui.group(this, "Sonuç doğrulama",
        "Bu koşulları sağlamayan ölçümler “geçersiz” olarak işaretlenir").apply {
        val slopes = listOf(0.0, 0.5, 1.0, 1.5, 2.0)
        addView(Ui.label(context, "Eğim sınırı", "Yokuş aşağı ölçümler süreyi kısaltır"))
        addView(Ui.segmented(context, listOf("Kapalı", "%0.5", "%1", "%1.5", "%2"), slopes.indexOf(cfg.slope).coerceAtLeast(0)) {
            cfg.slope = slopes[it]; changed()
        })
        addView(Ui.switchRow(context, "Eğim düzeltmeli süreyi de göster",
            "Ölçülen eğimin etkisi hesaplanıp düz yol karşılığı ayrıca gösterilir", cfg.slc) {
            cfg.slc = it; changed()
        })
        val accs = listOf(0.5, 1.0, 1.5)
        addView(Ui.label(context, "GPS hız doğruluğu", "Bundan kötü hız doğruluğunda ölçüm geçersiz"))
        addView(Ui.segmented(context, accs.map { "±${Proto.f(it, 1)} m/s\n${Proto.f(it * 3.6, 1)} km/h" },
            accs.indexOf(cfg.gacc).coerceAtLeast(1)) {
            cfg.gacc = accs[it]; changed()
        })
        val satList = listOf(5, 6, 8, 10)
        addView(Ui.label(context, "En az uydu", "Daha fazla uydu = daha güvenilir hız"))
        addView(Ui.segmented(context, satList.map { "$it" }, satList.indexOf(cfg.sats).coerceAtLeast(1)) {
            cfg.sats = satList[it]; changed()
        })
    }

    private fun obdCorrection(): View = Ui.group(this, "OBD hız düzeltmesi",
        "Araç göstergesi/ECU hızı genelde gerçekten yüksektir; OBD ölçümleri bu oranla düzeltilir").apply {
        val manual = Ui.column(context)
        val value = TextView(context)
        addView(Ui.segmented(context, listOf("Otomatik (GPS'ten öğren)", "Elle"), if (cfg.obdk == 0.0) 0 else 1) {
            cfg.obdk = if (it == 0) 0.0 else 1.0; changed()
        }, Ui.lp(context).apply { topMargin = Ui.dp(context, 12) })
        manual.addView(Ui.label(context, "Düzeltme oranı", "+ = OBD hızı gerçeğinden düşük, artırılır"))
        manual.addView(Ui.stepper(context, value, { dir ->
            val pct = (((cfg.obdk - 1.0) * 100 * 2).roundToInt() + dir).coerceIn(-20, 20) / 2.0   // 0.5 % adım
            cfg.obdk = 1.0 + pct / 100.0
            changed()
        }))
        addView(manual)
        refreshers += {
            manual.visibility = if (cfg.obdk == 0.0) View.GONE else View.VISIBLE
            val pct = (cfg.obdk - 1.0) * 100
            value.text = "${if (pct >= 0) "+" else "−"}${Proto.f(kotlin.math.abs(pct), 1)} %   ·   ×${Proto.f(cfg.obdk, 3)}"
        }
    }

    // --- araç -------------------------------------------------------------------------

    private fun vehicle(): View = Ui.group(this, "Araç profili", "Sonuçlarda görünür; ağırlık ileride güç tahmini için").apply {
        addView(Ui.label(context, "Araç adı"))
        addView(Ui.input(context, cfg.name, "Aracım").apply {
            filters = arrayOf(InputFilter.LengthFilter(20), InputFilter { src, s, e, _, _, _ ->
                val f = src.subSequence(s, e).filter { it != '"' && it != '\\' && !it.isISOControl() }
                if (f.length == e - s) null else f
            })
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_WORDS
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) { }
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) { }
                override fun afterTextChanged(s: Editable?) {
                    val n = RollCfg.cleanName(s?.toString() ?: "")
                    if (n != cfg.name) { cfg.name = n; changed() }
                }
            })
        })

        val mass = TextView(context)
        addView(Ui.label(context, "Ağırlık (sürücü dahil)", "Basılı tutunca hızlı değişir · değere dokunup yazabilirsiniz"))
        addView(Ui.stepper(context, mass, { dir ->
            cfg.mass = ((cfg.mass / 10 + dir) * 10).coerceIn(500, 4000); changed()
        }) { editMass() })
        refreshers += { mass.text = "${cfg.mass} kg" }

        val drives = listOf("fwd", "rwd", "awd")
        addView(Ui.label(context, "Çekiş"))
        addView(Ui.segmented(context, listOf("Önden\nFWD", "Arkadan\nRWD", "4 çeker\nAWD"), drives.indexOf(cfg.drive).coerceAtLeast(0)) {
            cfg.drive = drives[it]; changed()
        })
    }

    private fun driveName(d: String) = when (d) { "rwd" -> "RWD"; "awd" -> "AWD"; else -> "FWD" }

    private fun editMass() {
        val f = numField("kg").apply { setText("${cfg.mass}"); selectAll() }
        inputDialog("Ağırlık", "Sürücü dahil araç ağırlığı (500 – 4000 kg).", listOf(f)) {
            val v = f.text.toString().toIntOrNull() ?: return@inputDialog "Ağırlığı girin"
            if (v !in 500..4000) "Ağırlık 500 – 4000 kg arasında olmalı"
            else { cfg.mass = v; changed(); null }
        }
    }

    // --- ses ve ekran / kayıt -------------------------------------------------------------

    private fun soundScreen(): View = Ui.group(this, "Ses ve ekran").apply {
        addView(Ui.switchRow(context, "Bip sesi", "Geri sayımda ve sonuç geldiğinde", cfg.beep) {
            cfg.beep = it; changed()
        })
        val holds = listOf(5, 10, 20, 0)
        addView(Ui.label(context, "Sonuç ekranda kalma süresi"))
        addView(Ui.segmented(context, listOf("5 sn", "10 sn", "20 sn", "Dokununca"), holds.indexOf(cfg.hold).coerceAtLeast(1)) {
            cfg.hold = holds[it]; changed()
        })
    }

    private fun logging(): View = Ui.group(this, "Telefondaki kayıtlar", "Bu ayarlar yalnız telefonda geçerlidir").apply {
        addView(Ui.switchRow(context, "Ham telemetri kaydet",
            "IMU ve OBD örnekleri de CSV'ye yazılır (analiz için); kapalıyken yalnız GPS ve senkron", cfg.rawlog) {
            cfg.rawlog = it; changed()
        })
        val days = listOf(0, 30, 90)
        addView(Ui.label(context, "Otomatik silme", "Eski ROLL kayıtları uygulama açılışında silinir"))
        addView(Ui.segmented(context, listOf("Asla", "30 gün", "90 gün"), days.indexOf(cfg.keepdays).coerceAtLeast(0)) {
            cfg.keepdays = days[it]; changed()
            Roll.purgeOld()
        })
    }

    private fun confirmReset() {
        AlertDialog.Builder(this)
            .setTitle("Varsayılanlara dön")
            .setMessage("Tüm ROLL ayarları (araç profili ve hedefler dahil) ilk hâline dönsün mü?")
            .setPositiveButton("Sıfırla") { _, _ ->
                RollSettings.reset()
                build()
                toast("Varsayılan ayarlar yüklendi")
            }
            .setNegativeButton("Vazgeç", null)
            .show()
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_SHORT).show()
}
