# Chevrolet Kalos 2005 — OBD2 PID notları

## Protokol

- Beklenen: **ISO 9141-2** veya **KWP2000** (ATSP0 otomatik)
- ELM327 init: `elm327_session.c` (ATE0, ATST32, ATSP0)

## Hızlı poll (40 ms)

| PID | Alan |
|-----|------|
| 0x0C | RPM |
| 0x0D | Hız |
| 0x11 | Gaz |

## Yavaş poll (2 s)

| PID | Alan |
|-----|------|
| 0x05 | Antifriz |
| 0x2F | Yakıt seviyesi |
| 0x04 | Motor yükü |
| 0x0F | Emme sıcaklığı |
| 0x10 | MAF (yakıt tüketimi türevi) |
| 0x42 | Modül voltajı (desteklenmiyorsa `--`) |

## DTC

- Servis 03, periyodik 30 s
- UI: P0xxx string listesi + gösterge sayacı

## Bench test

1. WiFi ELM327 → pill **OBD hazır**
2. RPM > 0 çalışırken hız/antifriz valid
3. Kopunca 3 sn içinde `--` ve kırmızı pill
