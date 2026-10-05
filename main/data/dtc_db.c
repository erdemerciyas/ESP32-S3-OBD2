#include "dtc_db.h"
#include <stddef.h>

/*
 * Türkçe DTC veritabanı — 2005 Chevrolet/Daewoo Kalos 1.4 (T200, F14S3 8V /
 * F14D3 16V) odaklı.
 *
 * Kaynaklar:
 *  - SAE J2012 / ISO 15031-6 genel P0xxx tanımları
 *  - GM servis kılavuzu, 2004 Chevrolet Aveo (T200) DTC dizini — Kalos ile
 *    aynı GM-Daewoo ECU ailesi (P1xxx üretici kodları buradan)
 *  - troublecodes.net/daewoo, Aveo/Kalos forumları (tipik nedenler)
 *
 * Not: EGR (P0401, P14xx), rölanti motoru (P151x) ve IMT (P0660) kodları
 * yalnız ilgili donanımı olan motorlarda (çoğunlukla 16V) görülür.
 * Fontta yalnız ASCII + Türkçe harfler var: başka özel karakter kullanma.
 */

typedef struct {
    uint16_t code;
    uint8_t  sev;
    const char *desc;
    const char *hint;
} dtc_row_t;

#define I DTC_SEV_INFO
#define W DTC_SEV_WARN
#define C DTC_SEV_CRIT

static const dtc_row_t s_rows[] = {
    /* ---- P00xx: hava/yakıt ölçümü, emisyon ---- */
    { 0x0010, W, "Eksantrik ayar aktüatörü devresi (Bank 1)", NULL },
    { 0x0011, W, "Eksantrik zamanlaması aşırı avans (Bank 1)", "Yağ seviyesi/kalitesi, OCV valfi" },
    { 0x0016, C, "Krank - eksantrik pozisyon uyumsuzluğu", "Triger kayışı atlamış olabilir" },
    { 0x0030, W, "Ön O2 sensörü ısıtıcı kontrol devresi", "Lambda ısıtıcısı / soket" },
    { 0x0036, W, "Arka O2 sensörü ısıtıcı kontrol devresi", "Lambda ısıtıcısı / soket" },
    { 0x0068, W, "MAP/MAF - gaz kelebeği konumu uyumsuz", "Emme tarafında vakum kaçağı" },

    /* ---- P01xx: yakıt ve hava ölçümü ---- */
    { 0x0100, W, "Hava kütle (MAF) sensörü devresi", NULL },
    { 0x0101, W, "Hava kütle (MAF) sensörü aralık/performans", "Kirli MAF, hava filtresi" },
    { 0x0102, W, "Hava kütle (MAF) sensörü düşük giriş", NULL },
    { 0x0103, W, "Hava kütle (MAF) sensörü yüksek giriş", NULL },
    { 0x0105, W, "Manifold basınç (MAP) sensörü devresi", "MAP soketi / kablo" },
    { 0x0106, W, "MAP sensörü aralık/performans hatası", "Vakum hortumu çatlak/kaçak" },
    { 0x0107, W, "MAP sensörü devresi düşük voltaj", "MAP soketi, 5V besleme" },
    { 0x0108, W, "MAP sensörü devresi yüksek voltaj", "MAP şase hattı kopuk" },
    { 0x0110, W, "Emme havası sıcaklık (IAT) sensörü devresi", NULL },
    { 0x0112, W, "Emme havası sıcaklık sensörü düşük voltaj", "IAT kablosu kısa devre" },
    { 0x0113, W, "Emme havası sıcaklık sensörü yüksek voltaj", "IAT soketi çıkmış/kopuk" },
    { 0x0115, W, "Motor suyu sıcaklık (ECT) sensörü devresi", "Hararet müşiri / soket" },
    { 0x0116, W, "Motor suyu sıcaklık sensörü performans", "Termostat açık kalıyor olabilir" },
    { 0x0117, W, "Motor suyu sıcaklık sensörü düşük voltaj", "Hararet müşiri kısa devre" },
    { 0x0118, W, "Motor suyu sıcaklık sensörü yüksek voltaj", "Hararet müşiri soketi/kablo" },
    { 0x0120, W, "Gaz kelebeği konum (TPS) sensörü devresi", NULL },
    { 0x0121, W, "Gaz kelebeği sensörü aralık/performans", "TPS aşınması, kirli kelebek" },
    { 0x0122, W, "Gaz kelebeği sensörü düşük voltaj", "TPS soketi / 5V besleme" },
    { 0x0123, W, "Gaz kelebeği sensörü yüksek voltaj", "TPS şase hattı" },
    { 0x0125, I, "Kapalı devre yakıt kontrolü için sıcaklık yetersiz", "Termostat açık kalıyor" },
    { 0x0128, W, "Termostat: motor suyu ısınmıyor", "Termostat açık kalmış" },
    { 0x0130, W, "Ön O2 sensörü (B1S1) devre arızası", "Lambda sondası / kablo" },
    { 0x0131, W, "Ön O2 sensörü (B1S1) düşük voltaj", "Egzoz kaçağı veya sensör" },
    { 0x0132, W, "Ön O2 sensörü (B1S1) yüksek voltaj", "Sensör kısa devre, zengin karışım" },
    { 0x0133, W, "Ön O2 sensörü (B1S1) yavaş tepki", "Yaşlanmış lambda sondası" },
    { 0x0134, W, "Ön O2 sensörü (B1S1) aktivite yok", "Sensör ölü veya kablo kopuk" },
    { 0x0135, W, "Ön O2 sensörü (B1S1) ısıtıcı devresi", "Lambda ısıtıcısı yanmış" },
    { 0x0136, W, "Arka O2 sensörü (B1S2) devre arızası", NULL },
    { 0x0137, W, "Arka O2 sensörü (B1S2) düşük voltaj", "Egzoz kaçağı veya sensör" },
    { 0x0138, W, "Arka O2 sensörü (B1S2) yüksek voltaj", NULL },
    { 0x0140, W, "Arka O2 sensörü (B1S2) aktivite yok", "Sensör ölü veya kablo kopuk" },
    { 0x0141, W, "Arka O2 sensörü (B1S2) ısıtıcı devresi", "Lambda ısıtıcısı yanmış" },
    { 0x0170, W, "Yakıt düzeltme arızası (Bank 1)", NULL },
    { 0x0171, W, "Karışım çok fakir (Bank 1)", "Vakum kaçağı, MAP hortumu, yakıt basıncı" },
    { 0x0172, W, "Karışım çok zengin (Bank 1)", "Enjektör kaçırma, lambda, MAP" },
    { 0x0174, W, "Karışım çok fakir (Bank 2)", NULL },
    { 0x0175, W, "Karışım çok zengin (Bank 2)", NULL },
    { 0x0180, W, "Yakıt sıcaklık sensörü devresi", NULL },
    { 0x0190, W, "Yakıt basınç sensörü devresi", NULL },

    /* ---- P02xx: enjektör devresi ---- */
    { 0x0200, W, "Enjektör devresi arızası", NULL },
    { 0x0201, W, "1. silindir enjektör devresi", "Enjektör bobini / soket" },
    { 0x0202, W, "2. silindir enjektör devresi", "Enjektör bobini / soket" },
    { 0x0203, W, "3. silindir enjektör devresi", "Enjektör bobini / soket" },
    { 0x0204, W, "4. silindir enjektör devresi", "Enjektör bobini / soket" },
    { 0x0217, C, "Motor aşırı ısınma durumu", "Hemen dur: su eksik, fan, termostat" },
    { 0x0219, W, "Motor aşırı devir durumu", NULL },
    { 0x0230, C, "Yakıt pompası rölesi/birincil devre", "Yakıt pompası rölesi, sigorta" },
    { 0x0261, W, "1. silindir enjektör devresi düşük", NULL },
    { 0x0262, W, "1. silindir enjektör devresi yüksek", NULL },
    { 0x0264, W, "2. silindir enjektör devresi düşük", NULL },
    { 0x0265, W, "2. silindir enjektör devresi yüksek", NULL },
    { 0x0267, W, "3. silindir enjektör devresi düşük", NULL },
    { 0x0268, W, "3. silindir enjektör devresi yüksek", NULL },
    { 0x0270, W, "4. silindir enjektör devresi düşük", NULL },
    { 0x0271, W, "4. silindir enjektör devresi yüksek", NULL },

    /* ---- P03xx: ateşleme / tekleme ---- */
    { 0x0300, C, "Rastgele / çoklu silindir teklemesi", "Buji kablosu, DIS bobin, bujiler" },
    { 0x0301, C, "1. silindirde tekleme algılandı", "Buji kablosu / buji / bobin A" },
    { 0x0302, C, "2. silindirde tekleme algılandı", "Buji kablosu / buji / bobin B" },
    { 0x0303, C, "3. silindirde tekleme algılandı", "Buji kablosu / buji / bobin B" },
    { 0x0304, C, "4. silindirde tekleme algılandı", "Buji kablosu / buji / bobin A" },
    { 0x0321, W, "Distribütör/devir giriş devresi performans", NULL },
    { 0x0325, W, "Vuruntu sensörü devresi", "Vuruntu sensörü / soket" },
    { 0x0326, W, "Vuruntu sensörü aralık/performans", "Sensör sıkma torku, soket" },
    { 0x0327, W, "Vuruntu sensörü düşük giriş", "Sensör soketi çıkmış" },
    { 0x0328, W, "Vuruntu sensörü yüksek giriş", NULL },
    { 0x0335, C, "Krank mili konum (CKP) sensörü devresi", "Krank sensörü: motor stop edebilir" },
    { 0x0336, C, "Krank sensörü aralık/performans", "Sensör boşluğu, tetik dişlisi" },
    { 0x0337, C, "Krank sensörü düşük giriş", NULL },
    { 0x0338, C, "Krank sensörü yüksek giriş", NULL },
    { 0x0340, W, "Eksantrik mili konum (CMP) sensörü devresi", "CMP sensörü / soket" },
    { 0x0341, W, "Eksantrik sensörü aralık/performans", "Triger ayarı kontrol edilmeli" },
    { 0x0342, W, "Eksantrik sensörü düşük giriş", NULL },
    { 0x0343, W, "Eksantrik sensörü yüksek giriş", NULL },
    { 0x0351, W, "Ateşleme bobini A devresi (silindir 1-4)", "DIS bobin arızası" },
    { 0x0352, W, "Ateşleme bobini B devresi (silindir 2-3)", "DIS bobin arızası" },

    /* ---- P04xx: yardımcı emisyon kontrolü ---- */
    { 0x0400, W, "EGR akış arızası", NULL },
    { 0x0401, W, "EGR akışı yetersiz", "EGR valfi karbonlanmış" },
    { 0x0402, W, "EGR akışı aşırı", "EGR valfi açık kalmış" },
    { 0x0403, W, "EGR selenoid devresi", NULL },
    { 0x0404, W, "EGR aralık/performans", NULL },
    { 0x0405, W, "EGR konum sensörü düşük", NULL },
    { 0x0406, W, "EGR konum sensörü yüksek", NULL },
    { 0x0410, W, "Sekonder hava enjeksiyon sistemi", NULL },
    { 0x0420, W, "Katalizör verimi eşik altında (Bank 1)", "Katalizör ömrü bitmiş, arka lambda" },
    { 0x0421, W, "Isınma katalizörü verimi düşük (Bank 1)", NULL },
    { 0x0440, W, "Buhar emisyon (EVAP) sistemi arızası", "Yakıt depo kapağı" },
    { 0x0441, W, "EVAP sistemi hatalı tahliye akışı", "Kanister valfi" },
    { 0x0442, W, "EVAP sistemi küçük kaçak", "Depo kapağı contası" },
    { 0x0443, W, "Kanister tahliye valfi devresi", "Kanister valfi / soket" },
    { 0x0444, W, "Kanister tahliye valfi devresi açık", "Kanister valfi soketi" },
    { 0x0445, W, "Kanister tahliye valfi devresi kısa", NULL },
    { 0x0446, W, "EVAP havalandırma kontrol devresi", NULL },
    { 0x0455, W, "EVAP sistemi büyük kaçak", "Depo kapağı açık/gevşek" },
    { 0x0460, I, "Yakıt seviye sensörü devresi", "Depo şamandırası" },
    { 0x0461, I, "Yakıt seviye sensörü aralık/performans", "Depo şamandırası" },
    { 0x0462, I, "Yakıt seviye sensörü düşük giriş", NULL },
    { 0x0463, I, "Yakıt seviye sensörü yüksek giriş", NULL },
    { 0x0480, W, "Radyatör fanı rölesi 1 kontrol devresi", "Fan rölesi / sigorta" },
    { 0x0481, W, "Radyatör fanı rölesi 2 kontrol devresi", "Fan rölesi / sigorta" },
    { 0x0482, W, "Radyatör fanı rölesi 3 kontrol devresi", NULL },

    /* ---- P05xx: hız, rölanti, yardımcı girişler ---- */
    { 0x0500, W, "Araç hız sensörü (VSS) arızası", "Şanzıman hız sensörü / kablo" },
    { 0x0501, W, "Araç hız sensörü aralık/performans", NULL },
    { 0x0502, W, "Araç hız sensörü düşük giriş", NULL },
    { 0x0505, W, "Rölanti kontrol sistemi arızası", "Rölanti motoru (IAC), kirli kelebek" },
    { 0x0506, W, "Rölanti devri beklenenden düşük", "Kirli kelebek gövdesi / IAC" },
    { 0x0507, W, "Rölanti devri beklenenden yüksek", "Vakum kaçağı / IAC" },
    { 0x0520, C, "Yağ basınç sensörü/müşiri devresi", "Yağ seviyesini hemen kontrol et" },
    { 0x0521, C, "Yağ basıncı aralık/performans", "Yağ seviyesi / yağ pompası" },
    { 0x0522, C, "Yağ basınç sensörü düşük voltaj", NULL },
    { 0x0523, W, "Yağ basınç sensörü yüksek voltaj", NULL },
    { 0x0530, I, "Klima gaz basınç sensörü devresi", NULL },
    { 0x0532, I, "Klima gaz basınç sensörü düşük", "Klima gazı eksik / sensör" },
    { 0x0533, I, "Klima gaz basınç sensörü yüksek", "Sensör / kondenser tıkalı" },
    { 0x0560, W, "Sistem voltajı arızası", "Akü / şarj sistemi" },
    { 0x0562, W, "Sistem voltajı düşük", "Alternatör / akü / kutup başı" },
    { 0x0563, W, "Sistem voltajı yüksek", "Alternatör konjektörü (regülatör)" },

    /* ---- P06xx: ECU ve çıkış devreleri ---- */
    { 0x0600, W, "Seri iletişim hattı arızası", NULL },
    { 0x0601, C, "ECU hafıza (ROM) toplam kontrol hatası", "Motor beyni arızası" },
    { 0x0602, C, "ECU programlama hatası", "ECU yazılımı" },
    { 0x0603, W, "ECU canlı hafıza (KAM) hatası", "Akü söküldüyse normal olabilir" },
    { 0x0604, C, "ECU RAM hatası", "Motor beyni arızası" },
    { 0x0605, C, "ECU ROM hatası", "Motor beyni arızası" },
    { 0x0606, C, "ECU işlemci arızası", "Motor beyni arızası" },
    { 0x0645, I, "Klima kompresör rölesi kontrol devresi", NULL },
    { 0x0650, I, "Arıza lambası (MIL) kontrol devresi", "Gösterge / kablo" },
    { 0x0654, I, "Motor devri çıkış sinyali devresi", "Devir saati kablosu" },
    { 0x0660, W, "Emme manifoldu ayar valfi (IMT) devresi", "IMT valfi (16V motor)" },

    /* ---- P07xx: otomatik şanzıman (Kalos AT) ---- */
    { 0x0700, W, "Şanzıman kontrol sistemi arızası", "Şanzıman beyninde kod var" },
    { 0x0705, W, "Vites konum (PRNDL) sensörü devresi", NULL },
    { 0x0710, W, "Şanzıman yağ sıcaklık sensörü devresi", NULL },
    { 0x0715, W, "Şanzıman giriş mili hız sensörü", NULL },
    { 0x0720, W, "Şanzıman çıkış mili hız sensörü", NULL },
    { 0x0730, W, "Yanlış dişli oranı", "Şanzıman yağı / kavrama" },
    { 0x0740, W, "Tork konvertörü kilitleme devresi", NULL },
    { 0x0750, W, "Vites selenoidi A arızası", NULL },
    { 0x0755, W, "Vites selenoidi B arızası", NULL },

    /* ---- P1xxx: GM-Daewoo (Aveo/Kalos T200) üretici kodları ---- */
    { 0x1106, W, "MAP sensörü devresi aralıklı yüksek voltaj", "MAP soketi / kablo teması" },
    { 0x1107, W, "MAP sensörü devresi aralıklı düşük voltaj", "MAP soketi / kablo teması" },
    { 0x1111, W, "Emme havası sıcaklık sensörü aralıklı yüksek", "IAT soketi gevşek" },
    { 0x1112, W, "Emme havası sıcaklık sensörü aralıklı düşük", "IAT kablosu kısa devre" },
    { 0x1114, W, "Hararet sensörü devresi aralıklı düşük", "Hararet müşiri / soket" },
    { 0x1115, W, "Hararet sensörü devresi aralıklı yüksek", "Hararet müşiri / soket" },
    { 0x1121, W, "Gaz kelebeği sensörü aralıklı yüksek voltaj", "TPS aşınması" },
    { 0x1122, W, "Gaz kelebeği sensörü aralıklı düşük voltaj", "TPS aşınması" },
    { 0x1133, W, "Ön O2 sensörü yetersiz zengin/fakir geçiş", "Yaşlanmış lambda sondası" },
    { 0x1134, W, "Ön O2 sensörü geçiş süresi oranı hatalı", "Yavaş lambda sondası" },
    { 0x1167, W, "Yakıt kesmede ön O2 sensörü yüksek voltaj", "Enjektör kaçırma / lambda" },
    { 0x1171, W, "Tam gazda ön O2 sensörü düşük (fakir)", "Yakıt pompası / filtre zayıf" },
    { 0x1230, C, "Yakıt pompası rölesi devresi düşük voltaj", "Röle / kablo arızası" },
    { 0x1231, C, "Yakıt pompası rölesi devresi yüksek voltaj", "Röle arızası" },
    { 0x1320, W, "Krank sensörü varyasyonu öğrenilmedi", "Krank öğrenme prosedürü gerekli" },
    { 0x1321, W, "Krank tetik dişlisi performans hatası", "Dişli hasarı / sensör boşluğu" },
    { 0x1336, W, "Krank sistemi varyasyonu öğrenilmedi", "ECU/sensör değişimi sonrası öğrenme" },
    { 0x1380, I, "Tekleme algılandı, bozuk yol verisi yok", "ABS sinyali yok" },
    { 0x1385, I, "Teker hız sensörü devresi (bozuk yol)", "ABS sensörü / kablo" },
    { 0x1391, I, "Bozuk yol sensörü performans hatası", NULL },
    { 0x1402, W, "EGR akışı yetersiz", "EGR valfi karbonlanma" },
    { 0x1403, W, "EGR selenoid kontrol devresi arızası", "EGR soketi / valf" },
    { 0x1404, W, "EGR kapalı konum performans hatası", "EGR valfi açık kalmış" },
    { 0x1504, W, "Şanzıman beyni iletişim performansı", "Yalnız otomatik vites" },
    { 0x1511, W, "Rölanti kontrol motoru devresi arızası", "Rölanti motoru / soket" },
    { 0x1512, W, "Rölanti kontrol sistemi arızası", "Kirli kelebek / rölanti motoru" },
    { 0x1513, W, "Rölanti kontrol sistemi performans hatası", "Kelebek temizliği + adaptasyon" },
    { 0x1601, W, "Şanzıman beyniyle iletişim kayıp", "Yalnız otomatik vites" },
    { 0x1607, W, "ECU kontak kapalı zamanlayıcı hatası", "ECU iç arızası" },
    { 0x1610, W, "Ana röle devresi yüksek voltaj", "Ana röle arızası" },
    { 0x1611, W, "Ana röle devresi düşük voltaj", "Ana röle / sigorta" },
    { 0x1626, C, "İmmobilizer yakıt izin sinyali alınmadı", "Anahtar çipi / anten halkası" },
    { 0x1628, C, "İmmobilizer iletişimi yok", "İmmobilizer modülü / kablo" },
    { 0x1629, W, "İmmobilizer yakıt izin sinyali hatalı", "Anahtar programlama" },
    { 0x1631, C, "İmmobilizerden geçersiz veri alındı", "Kodlanmamış anahtar / ECU" },
    { 0x1650, I, "Servis lambası (SVS) devresi yüksek voltaj", "Gösterge / kablo" },
    { 0x1651, I, "Servis lambası (SVS) devresi düşük voltaj", "Gösterge / kablo" },
    { 0x1655, C, "ECU iç performans hatası", "Motor beyni arızası" },
    { 0x1660, I, "Arıza lambası devresi yüksek voltaj", "Gösterge / kablo" },
    { 0x1661, I, "Arıza lambası devresi düşük voltaj", "Gösterge / kablo" },

    /* ---- P2xxx: genel (yeni nesil) ---- */
    { 0x2096, W, "Katalizör sonrası yakıt düzeltme çok fakir", "Egzoz kaçağı, arka lambda" },
    { 0x2097, W, "Katalizör sonrası yakıt düzeltme çok zengin", NULL },
    { 0x2177, W, "Rölanti dışında karışım çok fakir", "Vakum kaçağı / yakıt basıncı" },
    { 0x2187, W, "Rölantide karışım çok fakir", "Vakum kaçağı, PCV hortumu" },
    { 0x2188, W, "Rölantide karışım çok zengin", NULL },
    { 0x2195, W, "Ön O2 sensörü sürekli fakir gösteriyor", NULL },
    { 0x2196, W, "Ön O2 sensörü sürekli zengin gösteriyor", NULL },
};

#undef I
#undef W
#undef C

static const struct {
    uint16_t lo, hi;
    const char *system;
    const char *desc;
} s_groups[] = {
    { 0x0000, 0x00FF, "Yakıt/Hava",  "Yakıt-hava ölçümü ve yardımcı emisyon" },
    { 0x0100, 0x01FF, "Yakıt/Hava",  "Yakıt ve hava ölçümü arızası" },
    { 0x0200, 0x02FF, "Yakıt",       "Yakıt / enjektör devresi arızası" },
    { 0x0300, 0x03FF, "Ateşleme",    "Ateşleme sistemi veya tekleme arızası" },
    { 0x0400, 0x04FF, "Emisyon",     "Yardımcı emisyon kontrol arızası" },
    { 0x0500, 0x05FF, "Rölanti/Hız", "Araç hızı, rölanti veya yardımcı giriş" },
    { 0x0600, 0x06FF, "ECU",         "Motor beyni veya çıkış devresi arızası" },
    { 0x0700, 0x09FF, "Şanzıman",    "Şanzıman arızası" },
    { 0x1000, 0x1FFF, "Üretici",     "GM/Daewoo üreticiye özel motor kodu" },
    { 0x2000, 0x2FFF, "Motor",       "Yakıt/hava veya emisyon arızası (genel)" },
    { 0x3000, 0x3FFF, "Motor",       "Motor kontrol arızası (üretici/genel)" },
};

static const char *row_system(uint16_t code)
{
    for (size_t i = 0; i < sizeof(s_groups) / sizeof(s_groups[0]); i++) {
        if (code >= s_groups[i].lo && code <= s_groups[i].hi) {
            return s_groups[i].system;
        }
    }
    return "Motor";
}

void dtc_db_lookup(uint16_t code, dtc_info_t *out)
{
    uint16_t num = code & 0x3FFF;
    unsigned sys = code >> 14;   /* 0=P 1=C 2=B 3=U */

    out->hint = NULL;
    out->known = false;
    out->sev = DTC_SEV_WARN;
    out->oem = (num & 0x3000) == 0x1000 || (num & 0x3000) == 0x3000;

    if (sys == 0) {
        for (size_t i = 0; i < sizeof(s_rows) / sizeof(s_rows[0]); i++) {
            if (s_rows[i].code == num) {
                out->desc = s_rows[i].desc;
                out->hint = s_rows[i].hint;
                out->sev = (dtc_sev_t)s_rows[i].sev;
                out->system = row_system(num);
                out->known = true;
                return;
            }
        }
        out->system = row_system(num);
        out->desc = "Motor kontrol arızası";
        for (size_t i = 0; i < sizeof(s_groups) / sizeof(s_groups[0]); i++) {
            if (num >= s_groups[i].lo && num <= s_groups[i].hi) {
                out->desc = s_groups[i].desc;
                break;
            }
        }
        return;
    }

    switch (sys) {
    case 1:
        out->system = "Şasi";
        out->desc = "Şasi arızası (ABS / fren / direksiyon)";
        out->hint = "ABS beyni ile detaylı tarama gerekli";
        break;
    case 2:
        out->system = "Gövde";
        out->desc = "Gövde arızası (hava yastığı / klima / konfor)";
        out->hint = "Hava yastığı ise servise götür";
        break;
    default:
        out->system = "Ağ";
        out->desc = "Modüller arası iletişim arızası";
        out->hint = "Soket, şase ve akü voltajını kontrol et";
        break;
    }
}
