#!/usr/bin/env python3
"""ROLL F0 log analizi — telefon kaydı (roll_*.csv) üzerinden kaynak doğrulaması.

Kayıt telefonda: AURA Köprü → ROLL kayıtları → Dışa aktar (Download/AURA/).
PC'ye: adb pull /sdcard/Download/AURA .

Ne yapar:
  * Tüm örnekleri ESP zaman tabanında hizalar (G satırında esp_us yoksa en
    düşük RTT'li saat senkronu ofsetiyle telefon zamanından çevirir).
  * Üç hız eğrisi: GPS (Doppler, ~1 Hz), OBD (PID 0x0D, istek/yanıt ortası)
    ve FÜZYON (IMU 100 Hz ivmesi, GPS fix'lerine aralık başına bias
    düzeltmesiyle bağlanır — ileri/geri tutarlı, çalıştırma sonrası).
  * OBD ölçek hatası k ve gecikmesi tau (GPS'e göre) tahmini.
  * Hızlanma koşularını bulur; her aralık (0-100, 60-120 ...) için her
    kaynağın süresini, eğimi (GPS rakımı / IMU) yan yana verir.

Kullanım:
  py scripts/roll_analyze.py roll_20261007_101500.csv
  py scripts/roll_analyze.py log.csv --ranges 0-100,60-120,80-140 --plot
"""

import argparse
import bisect
import math
import statistics
import sys

G0 = 9.80665
LAUNCH_G = 0.08      # kalkış: boyuna ivme eşiği (g), 100 ms sürmeli
FLOOR_G = 0.02       # kalkış anı bu gürültü tabanından ayrıldığı ana geri çekilir


# --- okuma ------------------------------------------------------------------

def num(s):
    return float(s) if s not in ("", None) else None


def load(path):
    gps, imu, obd, sync, events, header = [], [], [], [], [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                header.append(line)
                continue
            p = line.split(",")
            k = p[0]
            try:
                if k == "G" and len(p) >= 12:
                    gps.append(dict(esp=num(p[1]), phone=num(p[2]), v=num(p[3]), sa=num(p[4]),
                                    alt=num(p[5]), va=num(p[6]), ha=num(p[7]), hdg=num(p[8]),
                                    sat=num(p[9]), lat=num(p[10]), lon=num(p[11])))
                elif k == "I" and len(p) >= 7:
                    imu.append((float(p[1]), float(p[2]) / 1000, float(p[3]) / 1000,
                                float(p[4]) / 1000, float(p[5]) / 100, float(p[6]) / 100))
                elif k == "O" and len(p) >= 4:
                    obd.append((float(p[1]), float(p[2]), float(p[3])))
                elif k == "S" and len(p) >= 4:
                    sync.append((float(p[1]), float(p[2]), float(p[3])))
                elif k == "M":
                    events.append(",".join(p[1:]))
            except ValueError:
                pass
    return gps, imu, obd, sync, events, header


# --- yardımcılar --------------------------------------------------------------

def interp(ts, vs, t):
    """Doğrusal ara değer; aralık dışında None."""
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(ts):
        if i < len(ts) and ts[i] == t:
            return vs[i]
        return None
    t0, t1 = ts[i - 1], ts[i]
    if t1 == t0:
        return vs[i]
    return vs[i - 1] + (vs[i] - vs[i - 1]) * (t - t0) / (t1 - t0)


def crossing(ts, vs, level, i0, i1):
    """[i0, i1) aralığında level'ı yukarı kesen ilk an (doğrusal ara değer)."""
    for i in range(max(i0, 1), i1):
        if vs[i - 1] < level <= vs[i]:
            return ts[i - 1] + (level - vs[i - 1]) * (ts[i] - ts[i - 1]) / (vs[i] - vs[i - 1])
    return None


def fmt(x, nd=2, unit=""):
    return "--" if x is None else f"{x:.{nd}f}{unit}"


# --- hız eğrileri -------------------------------------------------------------

def gps_series(gps, sync):
    best_off = None
    good = [s for s in sync if s[2] > 0]
    if good:
        best_off = min(good, key=lambda s: s[2])[1]
    ts, vs, alts = [], [], []
    for g in gps:
        t = g["esp"]
        if t is None and best_off is not None and g["phone"] is not None:
            t = g["phone"] + best_off
        if t is None or g["v"] is None or g["v"] < 0:
            continue
        ts.append(t / 1e6)
        vs.append(g["v"] * 3.6)
        alts.append(g["alt"])
    return ts, vs, alts


def imu_series(imu):
    """t (s), yerçekimsiz boyuna ivme (m/s²), pitch (°)."""
    ts, a, pitch = [], [], []
    for t, ax, ay, az, gz, p in imu:
        ts.append(t / 1e6)
        a.append(ax - G0 * math.sin(math.radians(p)))
        pitch.append(p)
    return ts, a, pitch


def obd_series(obd):
    return [((tx + rx) / 2) / 1e6 for tx, rx, k in obd], [k for tx, rx, k in obd]


def fuse(imu_t, imu_a, anc_t, anc_v):
    """IMU ivmesini bağlantı noktalarına (GPS fix'leri) bağla: her aralıkta
    integral uyuşmazlığını aralık boyunca doğrusal dağıt (sabit bias)."""
    if len(anc_t) < 2 or len(imu_t) < 2:
        return [], []
    out_t, out_v = [], []
    j = bisect.bisect_left(imu_t, anc_t[0])
    for k in range(len(anc_t) - 1):
        t0, t1 = anc_t[k], anc_t[k + 1]
        if t1 - t0 > 3.0:          # boşluk: köprüleme
            j = bisect.bisect_left(imu_t, t1)
            continue
        seg_t, seg_dv = [t0], [0.0]
        v = 0.0
        tp, ap = t0, interp(imu_t, imu_a, t0) or 0.0
        while j < len(imu_t) and imu_t[j] <= t1:
            v += 0.5 * (ap + imu_a[j]) * (imu_t[j] - tp)
            tp, ap = imu_t[j], imu_a[j]
            seg_t.append(tp)
            seg_dv.append(v)
            j += 1
        a1 = interp(imu_t, imu_a, t1) or ap
        v += 0.5 * (ap + a1) * (t1 - tp)
        seg_t.append(t1)
        seg_dv.append(v)
        err = (anc_v[k + 1] - anc_v[k]) / 3.6 - v   # m/s
        for t, dv in zip(seg_t[:-1], seg_dv[:-1]):
            frac = (t - t0) / (t1 - t0)
            out_t.append(t)
            out_v.append(anc_v[k] + (dv + err * frac) * 3.6)
    out_t.append(anc_t[-1])
    out_v.append(anc_v[-1])
    return out_t, out_v


def obd_calib(gt, gv, ot, ov):
    """OBD = k · GPS(t − tau) — tau taraması, k en küçük kareler (v > 30 km/h)."""
    best = None
    for tau_ms in range(-500, 1501, 10):
        tau = tau_ms / 1000
        num_, den, pairs = 0.0, 0.0, []
        for t, v in zip(ot, ov):
            g = interp(gt, gv, t - tau)
            if g is None or g < 30:
                continue
            pairs.append((v, g))
            num_ += v * g
            den += g * g
        if len(pairs) < 20 or den == 0:
            continue
        k = num_ / den
        rms = math.sqrt(sum((v - k * g) ** 2 for v, g in pairs) / len(pairs))
        if best is None or rms < best[2]:
            best = (k, tau, rms, len(pairs))
    return best


# --- koşular ------------------------------------------------------------------

def launches(imu_t, imu_a):
    """Durağan → kalkış anları (IMU)."""
    out, i, n = [], 0, len(imu_t)
    while i < n:
        if imu_a[i] / G0 > LAUNCH_G:
            j = i
            while j < n and imu_a[j] / G0 > LAUNCH_G and imu_t[j] - imu_t[i] < 0.1:
                j += 1
            if j < n and imu_t[j] - imu_t[i] >= 0.1:
                k = i
                while k > 0 and imu_a[k] / G0 > FLOOR_G and imu_t[i] - imu_t[k] < 1.0:
                    k -= 1
                out.append(imu_t[k])
                while i < n and imu_a[i] / G0 > 0:   # bu hızlanma bitene kadar atla
                    i += 1
                continue
        i += 1
    return out


def range_time(ts, vs, v1, v2, t_from, launch_t=None):
    """t_from'dan sonraki ilk v1→v2 geçişi; v1 = 0 ise başlangıç kalkış anı."""
    i0 = bisect.bisect_left(ts, t_from)
    if v1 <= 0:
        t1 = launch_t
    else:
        t1 = crossing(ts, vs, v1, i0, len(ts))
    if t1 is None:
        return None, None
    i1 = bisect.bisect_left(ts, t1)
    t2 = crossing(ts, vs, v2, i1, len(ts))
    if t2 is None:
        return None, None
    # arada v1'in belirgin altına düştüyse koşu bölünmüştür
    for i in range(i1, bisect.bisect_left(ts, t2)):
        if vs[i] < v1 - 3:
            return None, None
    return t2 - t1, (t1, t2)


def slope_gps(gt, alts, ft, fv, t1, t2):
    """Rakımın mesafeye göre eğimi (%), mesafe füzyon hızından integral."""
    pts = [(t, a) for t, a in zip(gt, alts) if a is not None and t1 - 1 <= t <= t2 + 1]
    if len(pts) < 3 or len(ft) < 2:
        return None
    def dist(t):
        d, i = 0.0, bisect.bisect_left(ft, t1)
        tp, vp = t1, interp(ft, fv, t1) or 0
        while i < len(ft) and ft[i] <= t:
            d += 0.5 * (vp + fv[i]) / 3.6 * (ft[i] - tp)
            tp, vp = ft[i], fv[i]
            i += 1
        return d
    xs = [dist(t) for t, _ in pts]
    ys = [a for _, a in pts]
    if max(xs) - min(xs) < 20:
        return None
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    return 100 * sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else None


def slope_imu(imu_t, pitch, t1, t2):
    """Ortalama araç pitch'i → % (montaj kalibrasyonuna ve yaylanmaya duyarlı)."""
    i0, i1 = bisect.bisect_left(imu_t, t1), bisect.bisect_left(imu_t, t2)
    if i1 - i0 < 10:
        return None
    return 100 * math.tan(math.radians(statistics.fmean(pitch[i0:i1])))


# --- ana akış -----------------------------------------------------------------

def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")   # Windows konsolu (cp1254)
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--ranges", default="0-50,0-100,60-100,80-120,100-140",
                    help="virgülle ayrılmış km/h aralıkları (varsayılan: %(default)s)")
    ap.add_argument("--plot", action="store_true", help="hız eğrilerini PNG olarak kaydet (matplotlib)")
    args = ap.parse_args()

    ranges = []
    for r in args.ranges.split(","):
        a, b = r.split("-")
        ranges.append((float(a), float(b)))

    gps, imu, obd, sync, events, header = load(args.log)
    for h in header[:1]:
        print(h.lstrip("# "))
    gt, gv, alts = gps_series(gps, sync)
    it, ia, ip = imu_series(imu)
    ot, ov = obd_series(obd)
    t0 = min([x[0] for x in (gt, it, ot) if x] or [0])

    def rate(ts):
        return (len(ts) - 1) / (ts[-1] - ts[0]) if len(ts) > 1 and ts[-1] > ts[0] else 0

    def gaps(ts, lim):
        return sum(1 for a, b in zip(ts, ts[1:]) if b - a > lim)

    print("\n== Kaynaklar ==")
    print(f"GPS : {len(gt):6d} fix   {rate(gt):5.2f} Hz   >1.5 s boşluk: {gaps(gt, 1.5)}")
    if gps:
        sa = [g["sa"] for g in gps if g["sa"] is not None and g["sa"] >= 0]
        sats = [g["sat"] for g in gps if g["sat"] is not None and g["sat"] >= 0]
        if sa:
            print(f"      hız doğruluğu medyan {statistics.median(sa) * 3.6:.2f} km/h"
                  f" (p90 {sorted(sa)[int(len(sa) * 0.9)] * 3.6:.2f})")
        if sats:
            print(f"      uydu medyan {statistics.median(sats):.0f}, en az {min(sats):.0f}")
    print(f"IMU : {len(it):6d} örnek {rate(it):5.1f} Hz   >50 ms boşluk: {gaps(it, 0.05)}")
    print(f"OBD : {len(ot):6d} örnek {rate(ot):5.2f} Hz   >1 s boşluk: {gaps(ot, 1.0)}")
    if obd:
        lat = [(rx - tx) / 1000 for tx, rx, _ in obd]
        print(f"      istek-yanıt medyan {statistics.median(lat):.0f} ms (p90 {sorted(lat)[int(len(lat) * 0.9)]:.0f} ms)")
    if sync:
        rtts = [s[2] / 1000 for s in sync]
        print(f"Senk: {len(sync)} örnek, RTT medyan {statistics.median(rtts):.0f} ms, en iyi {min(rtts):.0f} ms")

    ft, fv = fuse(it, ia, gt, gv)
    cal = obd_calib(gt, gv, ot, ov) if gt and ot else None
    if cal:
        k, tau, rms, n = cal
        print(f"\n== OBD / GPS ==\nölçek k = {k:.4f} (OBD %{(k - 1) * 100:+.1f})   gecikme tau = {tau * 1000:.0f} ms"
              f"   kalan RMS {rms:.2f} km/h  ({n} örnek, v>30)")

    # OBD'yi füzyon için ayrıca bağlantı noktası olarak kullan (GPS yoksa);
    # OBD-k: GPS'ten öğrenilen ölçek ve gecikmeyle düzeltilmiş OBD + IMU
    oft, ofv = fuse(it, ia, ot, ov) if ot else ([], [])
    kft, kfv = [], []
    if cal:
        kft, kfv = fuse(it, ia, [t - cal[1] for t in ot], [v / cal[0] for v in ov])

    ref_t, ref_v = (ft, fv) if ft else (oft, ofv)
    starts = []
    for s0 in (launches(it, ia) if it else []):
        v0 = interp(ref_t, ref_v, s0) if ref_t else None
        if v0 is None or v0 < 3:     # yalnız duruştan kalkış (vites geçişi değil)
            starts.append(s0)
    print(f"\n== Koşular ==  ({len(starts)} kalkış algılandı)")
    sources = [("GPS", gt, gv), ("FÜZYON", ft, fv), ("OBD", ot, ov), ("OBD+IMU", oft, ofv),
               ("OBD-k", kft, kfv)]
    hdr = f"{'aralık':>10} {'başlangıç':>9} " + " ".join(f"{n:>9}" for n, _, _ in sources) + f" {'eğim GPS':>9} {'eğim IMU':>9}"
    print(hdr)
    print("-" * len(hdr))
    rows = 0
    for v1, v2 in ranges:
        anchors = starts if v1 <= 0 else []
        if v1 > 0:
            i = 0
            while True:
                t = crossing(ref_t, ref_v, v1, i, len(ref_t))
                if t is None:
                    break
                anchors.append(t - 0.5)
                i = bisect.bisect_left(ref_t, t) + 1
                while i < len(ref_t) and ref_v[i] > v1 - 5:
                    i += 1
        for a in anchors:
            res, span = [], None
            for _, ts, vs in sources:
                dt, sp = range_time(ts, vs, v1, v2, a, launch_t=a if v1 <= 0 else None) if ts else (None, None)
                res.append(dt)
                span = span or sp
            if all(r is None for r in res):
                continue
            sg = slope_gps(gt, alts, ft or oft, fv or ofv, *span) if span else None
            si = slope_imu(it, ip, *span) if span else None
            print(f"{int(v1):>4}-{int(v2):<5} {a - t0:8.1f}s " + " ".join(f"{fmt(r, 2, 's'):>9}" for r in res)
                  + f" {fmt(sg, 2, '%'):>9} {fmt(si, 2, '%'):>9}")
            rows += 1
    if not rows:
        print("(tamamlanan aralık yok)")
    if events:
        print("\nOlaylar: " + "; ".join(events[:12]))

    if args.plot:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib yok: py -m pip install matplotlib", file=sys.stderr)
            return
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True, height_ratios=[3, 1])
        rel = lambda ts: [t - t0 for t in ts]
        if ft:
            ax1.plot(rel(ft), fv, lw=1, label="füzyon (GPS+IMU)")
        if gt:
            ax1.plot(rel(gt), gv, "o", ms=3, label="GPS")
        if ot:
            ax1.plot(rel(ot), ov, ".", ms=3, label="OBD")
        for s in starts:
            ax1.axvline(s - t0, color="red", lw=0.5)
        ax1.set_ylabel("km/h")
        ax1.legend()
        ax1.grid(alpha=0.3)
        if it:
            ax2.plot(rel(it), [a / G0 for a in ia], lw=0.5)
        ax2.set_ylabel("boyuna g")
        ax2.set_xlabel("s")
        ax2.grid(alpha=0.3)
        out = args.log.rsplit(".", 1)[0] + ".png"
        fig.tight_layout()
        fig.savefig(out, dpi=110)
        print(f"\nGrafik: {out}")


if __name__ == "__main__":
    main()
