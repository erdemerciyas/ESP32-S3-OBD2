# Kullanim

## Ana ekran (acilis)

- Dogrudan **gosterge** acilir (RPM, hiz, sicaklik, voltaj).
- Kayitli WiFi varsa arka planda baglanir.

## WiFi menusu

- Ana ekranda **cift dokunun** → WiFi listesi.
- Ag secin, sifre, **Baglan** → kaydedilir.
- **Geri** → ana ekrana donus (baglanti kesilmez).

## OBD / CAN verisi

WiFi + ELM327 TCP baglantisi kurulunca:

1. ELM327 init (`ATZ`, `ATSP0`, …)
2. PID okuma (RPM, hiz, sicaklik, gaz, voltaj)
3. Gosterge guncellenir

Durum satirinda `WiFi: Bagli | OBD: Ready` gorunmeli.
