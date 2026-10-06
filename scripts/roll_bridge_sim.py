"""ROLL telefon taklidi: PC Bluetooth ile ESP32'ye bağlanır, saat senkronu
(ping "m" / pong "e"), 1 Hz sentetik GNSS (0→100 km/h rampa) gönderir ve TEL
telemetrisini (IMU / OBD) sayar. Telefonsuz uçtan uca test içindir.

Kurulum:  python -m venv .venv && .venv\\Scripts\\pip install bleak
Kullanım: .venv\\Scripts\\python scripts/roll_bridge_sim.py [süre_sn=40]
(Cihaz ROLL modunda olmalı; NAV'da senkron + gnss yine test edilir, TEL akmaz.)"""
import asyncio, json, struct, sys, time
from bleak import BleakClient, BleakScanner

SVC = "7c6a0001-2f4b-4b8e-9d3a-5e1f0c2a9b10"
RX = "7c6a0002-2f4b-4b8e-9d3a-5e1f0c2a9b10"
TX = "7c6a0003-2f4b-4b8e-9d3a-5e1f0c2a9b10"
TEL = "7c6a0005-2f4b-4b8e-9d3a-5e1f0c2a9b10"
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 40

t0 = time.time()
mono = lambda: int(time.perf_counter_ns() // 1000)
def log(*a):
    print(f"[{time.time() - t0:6.1f}s]", *a, flush=True)

sync = []          # (rtt, offset)
stats = {"imu": 0, "obd": 0, "frames": 0, "mode": None}
last_imu = None

def offset():
    good = sorted(sync)[:5]
    return good[0][1] if len(good) >= 3 and good[0][0] < 150_000 else None

def on_tx(_, data: bytearray):
    t2 = mono()
    s = data.decode("utf-8", "replace")
    try:
        o = json.loads(s)
    except ValueError:
        log("<- ?", s)
        return
    if o.get("t") == "pong" and "m" in o and "e" in o:
        rtt = t2 - o["m"]
        sync.append((rtt, o["e"] - (o["m"] + t2) // 2))
        return
    if o.get("t") == "status":
        stats["mode"] = o.get("mode")
    log("<-", s)

def on_tel(_, data: bytearray):
    global last_imu
    typ, n = data[0], data[1]
    stats["frames"] += 1
    if typ == 1 and len(data) >= 2 + n * 14:
        stats["imu"] += n
        last_imu = struct.unpack_from("<Ihhhhh", data, 2 + (n - 1) * 14)
    elif typ == 2 and len(data) >= 2 + n * 9:
        stats["obd"] += n

async def send(c, obj):
    b = json.dumps(obj, separators=(",", ":")).encode()
    await c.write_gatt_char(RX, b, response=True)
    return len(b)

async def main():
    log("AURA aranıyor...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == "AURA" or SVC in [u.lower() for u in ad.service_uuids], timeout=20)
    if not dev:
        log("bulunamadı (NAV/ROLL modunda mı?)")
        return
    async with BleakClient(dev, timeout=20) as c:
        log(f"bağlandı, MTU={c.mtu_size}")
        await c.start_notify(TX, on_tx)
        try:
            await c.start_notify(TEL, on_tel)
        except Exception as e:
            log("TEL yok (eski firmware?):", e)
        await send(c, {"t": "hello", "v": 1, "src": "roll-sim", "seq": 1})
        seq, k, start = 1, 0, time.time()
        last_rep = time.time()
        while time.time() - start < DUR:
            seq += 1
            await send(c, {"t": "ping", "seq": seq, "m": mono()})
            off = offset()
            v = min(100.0, max(0.0, (k - 5) * 100 / 15)) / 3.6   # 5 sn dur, 15 sn rampa
            msg = {"t": "gnss", "v": round(v, 2), "sa": 0.3, "alt": 912.0, "va": 3.0, "ha": 3.5,
                   "hdg": 90.0, "sat": 14, "lat": 39.92, "lon": 32.85}
            if off is not None:
                msg["e"] = mono() + off
            n = await send(c, msg)
            k += 1
            if time.time() - last_rep >= 5:
                last_rep = time.time()
                dt = time.time() - start
                rtts = sorted(r for r, _ in sync)
                log(f"mod={stats['mode']} senk={'OK' if off is not None else 'yok'} "
                    f"RTT min/med={rtts[0] / 1000 if rtts else 0:.0f}/{rtts[len(rtts) // 2] / 1000 if rtts else 0:.0f} ms "
                    f"gnss={n}B v={v * 3.6:.0f} | TEL imu={stats['imu'] / dt:.0f}/s obd={stats['obd'] / dt:.1f}/s "
                    f"çerçeve={stats['frames']}")
                if last_imu:
                    t, ax, ay, az, gz, p = last_imu
                    log(f"   son IMU t={t} a=({ax / 1000:.2f},{ay / 1000:.2f},{az / 1000:.2f}) m/s² "
                        f"gz={gz / 100:.2f}°/s pitch={p / 100:.2f}°")
            await asyncio.sleep(1.0)
    log("bitti")

asyncio.run(main())
