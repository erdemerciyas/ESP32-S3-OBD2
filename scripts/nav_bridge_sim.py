"""Telefon köprüsü taklidi: PC Bluetooth ile ESP32 NAV moduna (OBD-Nav) bağlanıp
protokol v1 akışını oynatır. Windows ayarlarında eşleştirme gerekmez.

Kurulum:  python -m venv .venv && .venv\Scripts\pip install bleak
Kullanım: .venv\Scripts\python scripts/nav_bridge_sim.py [bekleme_dk=10]
(Cihazda NAV modu seçili olmalı.)"""
import asyncio, json, sys, time
from bleak import BleakClient, BleakScanner

SVC = "7c6a0001-2f4b-4b8e-9d3a-5e1f0c2a9b10"
RX = "7c6a0002-2f4b-4b8e-9d3a-5e1f0c2a9b10"
TX = "7c6a0003-2f4b-4b8e-9d3a-5e1f0c2a9b10"
WAIT_MIN = float(sys.argv[1]) if len(sys.argv) > 1 else 10

t0 = time.time()
def log(*a):
    print(f"[{time.time() - t0:6.1f}s]", *a, flush=True)

replies = []
def on_notify(_, data: bytearray):
    s = data.decode("utf-8", "replace")
    replies.append(json.loads(s))
    log("<-", s)

async def send(c, obj, rsp=False):
    b = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode()
    await c.write_gatt_char(RX, b, response=rsp)
    log("->", len(b), "B", b.decode())

async def find():
    deadline = time.time() + WAIT_MIN * 60
    while time.time() < deadline:
        try:
            d = await BleakScanner.find_device_by_filter(
                lambda d, ad: (d.name in ("AURA", "OBD-Nav")) or (SVC in [u.lower() for u in ad.service_uuids]),
                timeout=10)
        except Exception as e:
            log("tarayıcı hatası (PC Bluetooth kapalı?):", e)
            await asyncio.sleep(5)
            continue
        if d:
            return d
        log("OBD-Nav görünmüyor, bekleniyor...")
    return None

async def session(dev, n):
    async with BleakClient(dev, timeout=20) as c:
        log(f"#{n} bağlandı, MTU={c.mtu_size}")
        await c.start_notify(TX, on_notify)
        replies.clear()
        await send(c, {"t": "hello", "v": 1, "src": "pc-sim", "seq": 1}, rsp=True)
        await asyncio.sleep(1)
        ok_status = any(r.get("t") == "status" for r in replies)
        log("status yanıtı:", "OK" if ok_status else "YOK")

        await asyncio.sleep(3)   # "Rota yok" ekranı görünsün
        await send(c, {"t": "start", "dst": "Kızılay"}, rsp=True)
        steps = [("R", 820, "Ankara Caddesi", "Atatürk Bulvarı"),
                 ("SL", 400, "Atatürk Bulvarı", "Gazi Mustafa Kemal Bulvarı Çok Uzun İsimli Yol Şubesi Ğ"),
                 ("RB", 1300, "Gazi M. Kemal Blv.", "Kızılay Meydanı"),
                 ("U", 150, "Kızılay Meydanı", "Ziya Gökalp Caddesi"),
                 ("A", 60, "Ziya Gökalp Caddesi", "")]
        seq = 10
        for m, md, road, nxt in steps:
            for k in range(4):
                seq += 1
                msg = {"t": "upd", "seq": seq, "m": m, "md": max(md - k * 15, 0),
                       "rd": 5200 - seq * 40, "eta": 18 * 60 + 42, "spd": 52 + k}
                if k == 0:
                    msg.update({"road": road, "next": nxt})
                await send(c, msg)
                await asyncio.sleep(1)
        # yalnızca mesafe: yol adı ekranda korunmalı
        await send(c, {"t": "upd", "seq": 99, "md": 25})
        await asyncio.sleep(1)
        # bozuk mesaj: ESP yok saymalı
        await c.write_gatt_char(RX, b"{bozuk", response=False)
        log("-> bozuk mesaj")
        replies.clear()
        await send(c, {"t": "ping", "seq": 7}, rsp=True)
        await asyncio.sleep(1)
        log("pong:", "OK" if any(r.get("t") == "pong" and r.get("seq") == 7 for r in replies) else "YOK")
        log("7 sn sessizlik ('Veri eski' görünmeli)")
        await asyncio.sleep(7)
        await send(c, {"t": "stop"}, rsp=True)
        await asyncio.sleep(2)
    log(f"#{n} bağlantı kapatıldı")
    return ok_status

async def main():
    dev = await find()
    if not dev:
        log("SONUÇ: OBD-Nav bulunamadı"); return
    log("bulundu:", dev.address)
    r1 = await session(dev, 1)
    await asyncio.sleep(4)   # ESP yeniden reklama dönmeli
    dev = await find()
    r2 = await session(dev, 2) if dev else False
    log("SONUÇ:", "1. oturum", "OK" if r1 else "HATA", "| yeniden bağlanma", "OK" if r2 else "HATA")

asyncio.run(main())
