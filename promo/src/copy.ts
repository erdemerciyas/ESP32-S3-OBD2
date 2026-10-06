import type { Lang } from "./theme";

export const COPY = {
  en: {
    tagline: "Smart car display · ESP32-S3",
    modes: {
      kicker: 'ESP32-S3 · 2.1" ROUND TOUCH',
      title: "One round screen.\nThree modes.",
      chips: ["OBD · car data", "NAV · phone navigation", "ROLL · performance timer", "Open source · MIT"],
    },
    obd: {
      kicker: "01 · OBD MODE",
      title: "Your car as a\ndigital cluster",
      chips: ["ELM327 · Bluetooth LE or WiFi", "RPM · speed · coolant · voltage", "Smooth 60 FPS gauges", "Shift lights & sensor ring"],
      labels: ["Dash", "Settings"],
    },
    diag: {
      kicker: "DIAGNOSTICS",
      title: "Diagnose\nlike a pro",
      chips: ["Live data for every supported PID", "Read & clear trouble codes", "Inclinometer & G-meter", "CAN · K-line · ISO 9141 · J1850"],
      labels: ["Live data", "Trouble codes", "Inclinometer"],
    },
    nav: {
      kicker: "02 · NAV MODE",
      title: "Turn-by-turn,\nright on your dash",
      chips: ["AURA Bridge · Android app", "Google Maps · Waze · Yandex", "Proximity ring & 3D road", "Speed-camera alerts"],
      labels: ["Guidance", "Turn now", "Cruising"],
    },
    roll: {
      kicker: "03 · ROLL MODE",
      title: "Your own\nperformance timer",
      chips: ["0–100 · 60–200 · custom ranges", "Phone GPS or ECU speed", "IMU fusion at 100 Hz", "Runs saved on phone · CSV"],
      labels: ["Live run"],
      stopwatch: "0–100 km/h",
    },
    clock: {
      kicker: "CLOCK",
      title: "Hold 4 seconds.\n3D holographic clock.",
      chips: ["Time & date synced from phone", "Opens from any screen", "Never interrupts navigation"],
      labels: ["Hold…", "3D clock"],
    },
    tech: {
      kicker: "UNDER THE HOOD",
      title: "Drawn in code.\nNo images. No video.",
      chips: ["LVGL 8", "fx3d 3D renderer", "ESP-IDF 5.3", "PSRAM canvases"],
    },
    outro: {
      line: "Free & open source",
      chips: ["Firmware", "Android app", "MIT license"],
      footer: "Build your own. Drive smarter.",
    },
    thumb: "DIY SMART\nCAR DISPLAY",
  },
  tr: {
    tagline: "Akıllı araç ekranı · ESP32-S3",
    modes: {
      kicker: 'ESP32-S3 · 2.1" YUVARLAK DOKUNMATİK',
      title: "Tek yuvarlak ekran.\nÜç mod.",
      chips: ["OBD · araç verisi", "NAV · telefon navigasyonu", "ROLL · performans ölçümü", "Açık kaynak · MIT"],
    },
    obd: {
      kicker: "01 · OBD MODU",
      title: "Aracınız artık\ndijital gösterge",
      chips: ["ELM327 · Bluetooth LE veya WiFi", "Devir · hız · su sıcaklığı · voltaj", "Akıcı 60 FPS göstergeler", "Vites ışıkları & sensör halkası"],
      labels: ["Gösterge", "Ayarlar"],
    },
    diag: {
      kicker: "TEŞHİS",
      title: "Usta gibi\nteşhis",
      chips: ["Desteklenen her PID canlı", "Arıza kodu okuma & silme", "Eğim ölçer & G-metre", "CAN · K-line · ISO 9141 · J1850"],
      labels: ["Canlı veri", "Arıza kodları", "Eğim ölçer"],
    },
    nav: {
      kicker: "02 · NAV MODU",
      title: "Yol tarifi,\ntam önünüzde",
      chips: ["AURA Köprü · Android uygulaması", "Google Haritalar · Waze · Yandex", "Yaklaşma halkası & 3D yol", "Radar uyarıları"],
      labels: ["Rehberlik", "Şimdi dön", "Seyir"],
    },
    roll: {
      kicker: "03 · ROLL MODU",
      title: "Kendi performans\nölçeriniz",
      chips: ["0–100 · 60–200 · özel aralıklar", "Telefon GPS'i veya ECU hızı", "100 Hz IMU füzyonu", "Koşular telefonda · CSV"],
      labels: ["Canlı ölçüm"],
      stopwatch: "0–100 km/h",
    },
    clock: {
      kicker: "SAAT",
      title: "4 saniye basılı tut.\n3D holografik saat.",
      chips: ["Saat & tarih telefondan", "Her ekrandan açılır", "Navigasyonu asla bölmez"],
      labels: ["Basılı tut…", "3D saat"],
    },
    tech: {
      kicker: "KAPUTUN ALTINDA",
      title: "Kodla çiziliyor.\nNe resim, ne video.",
      chips: ["LVGL 8", "fx3d 3D motoru", "ESP-IDF 5.3", "PSRAM tuvalleri"],
    },
    outro: {
      line: "Ücretsiz & açık kaynak",
      chips: ["Yazılım", "Android uygulaması", "MIT lisansı"],
      footer: "Kendin yap. Daha akıllı sür.",
    },
    thumb: "KENDİN YAP\nAKILLI ARAÇ EKRANI",
  },
} satisfies Record<Lang, unknown>;

export const REPO = "github.com/erdemerciyas/ESP32-S3-OBD2";
