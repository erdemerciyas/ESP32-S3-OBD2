# OBD2 Dashboard — LVGL PC Simulator

Bu klasor, ESP32 projesindeki UI'yi **Visual Studio** ile PC'de calistirmak icin kullanilir.
Araca gitmeden ekran tasarimini test edebilirsiniz.

## Gereksinimler

- **Visual Studio 2022** (17.13+) veya **Visual Studio 2026** (VS 2019 Build Tools da calisir)
- **Desktop development with C++** is yuku
- Platform: **x64** (ARM64 degil — ilk acilista platformu kontrol edin)

## Kurulum

Submoduller zaten cekilmis olmali. Degilse:

```powershell
cd simulator
git submodule update --init --recursive
```

## Hizli derleme (komut satiri)

```powershell
cd simulator
.\build_simulator.cmd
.\Output\Binaries\Release\x64\LVGL.Simulator.exe
```

## Calistirma

1. `simulator/LVGL.Simulator.sln` dosyasini Visual Studio ile acin
2. Solution Explorer'da **LVGL.Simulator** projesini sag tik → **Set as Startup Project**
3. Ust bardan platform olarak **x64** secin
4. **Local Windows Debugger** (F5) ile calistirin

480x480 pencere acilir; UI otomatik olarak sahte OBD verisiyle baslar.

## Simulator davranisi

| Ozellik | Davranis |
|---------|----------|
| Baglanti | Acilista otomatik simule baglanti (Tara butonu da calisir) |
| Canli veri | RPM, hiz, sicaklik vb. sinüs dalgasiyla guncellenir |
| DTC | Etkin degil (main projede DTC ekrani kaldirildi) |
| BLE/OBD | Stub — gercek adaptore baglanmaz |

## Dosya yapisi

```
simulator/
  LVGL.Simulator.sln          ← Visual Studio solution
  LVGL.Simulator/             ← LVGL 8.3 + Win32 driver
  obd2_dashboard/
    app_main.c                ← UI baslatma
    demo_feed.c               ← Sahte OBD verisi
    vehicle_data_sim.c        ← FreeRTOS'suz vehicle_data
    stubs/                    ← bsp, ble_obd, obd_dtc stub'lari
../main/ui/                   ← Gercek UI kaynaklari (paylasilir)
```

UI dosyalarinda (`main/ui/`) yaptiginiz degisiklikler simulator'de de gecerli olur — projeyi yeniden derlemeniz yeterli.

## Sorun giderme

- **Derleme hatasi (submodule eksik):** `git submodule update --init --recursive`
- **ARM64 secili:** Platformu **x64** yapin
- **Bellek hatasi:** `LVGL.Simulator/lv_conf.h` icinde `LV_MEM_SIZE` degerini artirin
