#include "wifi_obd.h"
#include "vehicle_data.h"
#include "app_log.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Klon WiFi adaptörlerinde gözlenen davranış (bkz. CHANGELOG):
 *  - Açık (şifresiz) AP, SSID genelde "WiFi_OBDII" / "OBDII" / "V-LINK".
 *  - ELM 192.168.0.10:35000'de; DHCP verir ama bazılarında DHCP bozuk →
 *    statik IP'ye düşülür.
 *  - Aynı anda tek TCP istemcisi kabul edilir; kopan oturum birkaç saniye
 *    "meşgul" kalabilir, bu yüzden yeniden denemeler aralıklıdır.
 *  - TCP akışı paket sınırı tanımaz; ELM ayrıştırıcısı zaten baytları
 *    biriktirip '>' bekliyor. */

static const char *TAG = "wifi_obd";
static const char *NVS_NS = "obd_wifi";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_LPORT = "lport";

#define DEFAULT_HOST        "192.168.0.10"
#define STATIC_IP_LAST      123         /* DHCP yoksa adaptör alt ağında bu adres */
#define MAX_SCAN_APS        16
#define ASSOC_TIMEOUT_MS    8000
#define DHCP_TIMEOUT_MS     5000
#define TCP_CONNECT_MS      4000        /* SYN yinelemesine (RTO 1.5 sn) yer kalsın */
#define LOCAL_PORT_BASE     35001
#define LOCAL_PORT_COUNT    8
#define RX_POLL_MS          100
#define TX_TIMEOUT_MS       1000
#define RETRY_MS            1500
#define BACKOFF_MAX_MS      10000
#define TCP_FAIL_MAX        3           /* sonra WiFi'yi baştan kur */
#define JOIN_FAIL_MAX       2           /* kayıtlı SSID'de sonra taramaya düş */
#define STOP_TIMEOUT_MS     11000
#define SILENCE_MS          15000       /* en uzun meşru bekleme 0100 = 12 sn */
#define DEAD_SESSION_MAX    2
#define TASK_STACK          4096
#define TASK_PRIO           6           /* elm327 (5) üstü: RX gecikmesi düşük kalsın */

#define BIT_ASSOC   BIT0
#define BIT_GOT_IP  BIT1
#define BIT_DISC    BIT2
#define BIT_RESCAN  BIT3
#define BIT_WAKE    BIT4            /* start/stop isteği */
#define BIT_IDLE    BIT5            /* görev durdu, radyo kapalı */
#define BIT_BREAK   (BIT_RESCAN | BIT_WAKE)

static const char *s_ssid_filters[] = {
    "OBD", "ELM", "V-LINK", "VLINK", "ICAR", "VGATE", "KONNWEI",
};

static wifi_obd_rx_cb_t s_rx_cb;
static EventGroupHandle_t s_ev;
static SemaphoreHandle_t s_tx_mutex;
static esp_netif_t *s_netif;
static volatile bool s_connected;
static volatile bool s_running;         /* seçili taşıma WiFi */
static bool s_wifi_started;             /* yalnız görev değiştirir */
static volatile TickType_t s_wait_since; /* yanıtsız ilk TX anı, 0 = yok */
static volatile bool s_session_rx;      /* bu TCP oturumunda veri geldi mi */
static int s_sock = -1;
static uint32_t s_gw;                   /* DHCP'den gelen ağ geçidi (ağ bayt sırası) */
static char s_ssid[33];                 /* şu an kullanılan SSID */
static char s_saved_ssid[33];
static uint16_t s_lport = LOCAL_PORT_BASE;   /* TCP yerel portu, NVS'de saklı */
static uint16_t s_saved_lport;
static bool s_force_scan;
static int s_join_fail;
static wifi_ap_record_t s_aps[MAX_SCAN_APS];

static bool ssid_matches(const char *ssid)
{
    for (size_t i = 0; i < sizeof(s_ssid_filters) / sizeof(s_ssid_filters[0]); i++) {
        size_t n = strlen(s_ssid_filters[i]);
        for (const char *p = ssid; *p; p++) {
            if (strncasecmp(p, s_ssid_filters[i], n) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void load_saved_ssid(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_saved_ssid);
        if (nvs_get_str(h, NVS_KEY_SSID, s_saved_ssid, &len) != ESP_OK) {
            s_saved_ssid[0] = '\0';
        }
        uint16_t p;
        if (nvs_get_u16(h, NVS_KEY_LPORT, &p) == ESP_OK &&
            p >= LOCAL_PORT_BASE && p < LOCAL_PORT_BASE + LOCAL_PORT_COUNT) {
            s_lport = p;
            s_saved_lport = p;
        }
        nvs_close(h);
    }
}

static void save_ssid(const char *ssid)
{
    if (strcmp(ssid, s_saved_ssid) == 0) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_commit(h);
        nvs_close(h);
        snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", ssid);
    }
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(s_ev, BIT_ASSOC);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d", e->reason);
        xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP);
        xEventGroupSetBits(s_ev, BIT_DISC);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_gw = e->ip_info.gw.addr;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
    }
}

/* Yakındaki AP'lerden filtreye uyan en güçlüsünü seç. */
static bool scan_for_adapter(char *out, size_t out_len)
{
    vehicle_data_set_state(OBD_STATE_SCANNING, "Scanning WiFi...");
    esp_wifi_disconnect();

    wifi_scan_config_t sc = { 0 };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        app_log_warn(TAG, "WiFi scan failed to start");
        return false;
    }
    uint16_t n = MAX_SCAN_APS;
    if (esp_wifi_scan_get_ap_records(&n, s_aps) != ESP_OK) {
        return false;
    }

    int best = -1;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)s_aps[i].ssid;
        if (ssid[0] && ssid_matches(ssid) && (best < 0 || s_aps[i].rssi > s_aps[best].rssi)) {
            best = i;
        }
    }
    if (best < 0) {
        app_log_warn(TAG, "No OBD access point among %u APs", n);
        vehicle_data_set_state(OBD_STATE_DISCONNECTED, "Adapter not found");
        return false;
    }
    snprintf(out, out_len, "%s", (const char *)s_aps[best].ssid);
    app_log_info(TAG, "Found OBD AP: %s (ch %u, %d dBm, auth %d)", out,
                 s_aps[best].primary, s_aps[best].rssi, s_aps[best].authmode);
    return true;
}

/* DHCP'si çalışmayan klonlar için: adaptörün alt ağında sabit adres. */
static void apply_static_ip(void)
{
    const char *host = CONFIG_OBD_WIFI_HOST[0] ? CONFIG_OBD_WIFI_HOST : DEFAULT_HOST;
    esp_ip4_addr_t gw = { .addr = esp_ip4addr_aton(host) };
    uint8_t last = esp_ip4_addr4(&gw) == STATIC_IP_LAST ? STATIC_IP_LAST + 1 : STATIC_IP_LAST;

    esp_netif_ip_info_t info = { 0 };
    info.gw = gw;
    esp_netif_set_ip4_addr(&info.ip, esp_ip4_addr1(&gw), esp_ip4_addr2(&gw),
                           esp_ip4_addr3(&gw), last);
    esp_netif_set_ip4_addr(&info.netmask, 255, 255, 255, 0);

    esp_netif_dhcpc_stop(s_netif);
    if (esp_netif_set_ip_info(s_netif, &info) == ESP_OK) {
        s_gw = gw.addr;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        app_log_warn(TAG, "DHCP timeout, static IP " IPSTR " gw " IPSTR,
                     IP2STR(&info.ip), IP2STR(&info.gw));
    }
}

static bool join_adapter(void)
{
    char ssid[33];
    bool from_saved = false;

    if (CONFIG_OBD_WIFI_SSID[0]) {
        snprintf(ssid, sizeof(ssid), "%s", CONFIG_OBD_WIFI_SSID);
    } else if (!s_force_scan && s_saved_ssid[0]) {
        snprintf(ssid, sizeof(ssid), "%s", s_saved_ssid);
        from_saved = true;
    } else if (!scan_for_adapter(ssid, sizeof(ssid))) {
        return false;
    }
    s_force_scan = false;

    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    vehicle_data_set_adapter(ssid, "");
    vehicle_data_set_state(OBD_STATE_CONNECTING, "Joining WiFi...");

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", CONFIG_OBD_WIFI_PASSWORD);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* açık/WEP/WPA hepsini kabul et */

    s_gw = 0;
    esp_netif_dhcpc_start(s_netif);   /* önceki statik IP denemesinden dön */
    xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP | BIT_DISC);
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK || esp_wifi_connect() != ESP_OK) {
        app_log_error(TAG, "WiFi connect request failed");
        return false;
    }

    int64_t t0 = esp_timer_get_time();
    EventBits_t b = xEventGroupWaitBits(s_ev, BIT_ASSOC | BIT_DISC | BIT_BREAK, pdFALSE, pdFALSE,
                                        pdMS_TO_TICKS(ASSOC_TIMEOUT_MS));
    if (!(b & BIT_ASSOC)) {
        esp_wifi_disconnect();
        app_log_warn(TAG, "Join %s failed", ssid);
        vehicle_data_set_state(OBD_STATE_DISCONNECTED, "WiFi join failed");
        if (from_saved && ++s_join_fail >= JOIN_FAIL_MAX) {
            s_force_scan = true;   /* kayıt silinmez; tarama yeniden bulursa üzerine yazılır */
            s_join_fail = 0;
        }
        return false;
    }
    s_join_fail = 0;

    b = xEventGroupWaitBits(s_ev, BIT_GOT_IP | BIT_DISC | BIT_WAKE, pdFALSE, pdFALSE,
                            pdMS_TO_TICKS(DHCP_TIMEOUT_MS));
    if (b & (BIT_DISC | BIT_WAKE)) {
        return false;
    }
    if (!(b & BIT_GOT_IP)) {
        apply_static_ip();
    }
    if (!(xEventGroupGetBits(s_ev) & BIT_GOT_IP)) {
        return false;
    }

    save_ssid(ssid);
    esp_netif_ip_info_t ip = { 0 };
    esp_netif_get_ip_info(s_netif, &ip);
    app_log_info(TAG, "Joined %s in %ld ms, IP " IPSTR, ssid,
                 (long)((esp_timer_get_time() - t0) / 1000), IP2STR(&ip.ip));
    return true;
}

static void save_local_port(uint16_t port)
{
    if (port == s_saved_lport) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, NVS_KEY_LPORT, port);
        nvs_commit(h);
        nvs_close(h);
        s_saved_lport = port;
    }
}

/* Sabit yerel port: ESP güç kaybıyla kapanınca adaptör eski oturumu açık
 * sanıp tek istemci yuvasını tutuyor. Yeniden açılışta aynı IP + aynı port
 * ile gelen SYN o ölü oturuma çarpar; adaptörün ACK'ine lwIP RST basar ve
 * SYN'i hemen yineler → adaptör eski oturumu bırakır, bağlantı kurulur. */
static int tcp_connect_to(uint32_t addr, int port, uint16_t lport, int *err_out)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        *err_out = errno;
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in la = {
        .sin_family = AF_INET,
        .sin_port = htons(lport),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *)&la, sizeof(la)) < 0) {
        *err_out = errno;
        close(fd);
        return -1;
    }
    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = addr,
    };

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    int err = rc < 0 ? errno : 0;
    if (rc < 0 && err == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = TCP_CONNECT_MS / 1000, .tv_usec = (TCP_CONNECT_MS % 1000) * 1000 };
        if (select(fd + 1, NULL, &wfds, NULL, &tv) == 1) {
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        } else {
            err = ETIMEDOUT;
        }
        rc = err ? -1 : 0;
    }
    if (rc < 0) {
        *err_out = err;
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);

    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));   /* "010C\r" hemen gitsin */
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    /* Gevşek tutuldu: bazı klon TCP yığınları keepalive yoklamasına cevap
     * vermiyor; 5 sn'lik ayar K-line 0100 beklemesinde oturumu öldürüyordu.
     * Ölü köprüyü asıl yakalayan, rx_loop'taki uygulama seviyesi bekçi. */
    int idle = 15, intvl = 5, cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
    struct timeval rto = { .tv_sec = 0, .tv_usec = RX_POLL_MS * 1000 };
    struct timeval sto = { .tv_sec = TX_TIMEOUT_MS / 1000, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sto, sizeof(sto));
    return fd;
}

/* Kconfig host → DHCP ağ geçidi → 192.168.0.10 sırasıyla dene. */
static bool tcp_open(void)
{
    uint32_t hosts[2];
    int count = 0;
    if (CONFIG_OBD_WIFI_HOST[0]) {
        hosts[count++] = esp_ip4addr_aton(CONFIG_OBD_WIFI_HOST);
    } else {
        uint32_t def = esp_ip4addr_aton(DEFAULT_HOST);
        if (s_gw && s_gw != def) {
            hosts[count++] = s_gw;
        }
        hosts[count++] = def;
    }

    vehicle_data_set_state(OBD_STATE_CONNECTING, "Opening TCP...");
    for (int i = 0; i < count; i++) {
        esp_ip4_addr_t a = { .addr = hosts[i] };
        char host[16];
        snprintf(host, sizeof(host), IPSTR, IP2STR(&a));
        int64_t t0 = esp_timer_get_time();
        int fd = -1, err = 0;
        for (int k = 0; k < LOCAL_PORT_COUNT; k++) {
            fd = tcp_connect_to(hosts[i], CONFIG_OBD_WIFI_PORT, s_lport, &err);
            if (fd >= 0 || err != EADDRINUSE) {
                break;
            }
            /* Kendi TIME_WAIT'imiz bu 4'lüyü tutuyor (temiz kapanış sonrası):
             * aralıktaki sonraki porta geç. */
            s_lport = LOCAL_PORT_BASE + (s_lport - LOCAL_PORT_BASE + 1) % LOCAL_PORT_COUNT;
        }
        if (fd < 0) {
            app_log_warn(TAG, "TCP %s:%d failed (local :%u, errno %d)", host,
                         CONFIG_OBD_WIFI_PORT, s_lport, err);
            continue;
        }
        save_local_port(s_lport);
        xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
        s_sock = fd;
        xSemaphoreGive(s_tx_mutex);
        s_wait_since = 0;
        s_session_rx = false;
        s_connected = true;
        vehicle_data_set_adapter(s_ssid, host);
        app_log_info(TAG, "TCP %s:%d connected (local :%u) in %ld ms", host, CONFIG_OBD_WIFI_PORT,
                     s_lport, (long)((esp_timer_get_time() - t0) / 1000));
        return true;
    }
    vehicle_data_set_state(OBD_STATE_ERROR, "Adapter TCP refused");
    return false;
}

static void tcp_close(void)
{
    s_connected = false;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }
    xSemaphoreGive(s_tx_mutex);
}

typedef enum {
    RX_END_BREAK = 0,   /* durdurma / yeniden tarama isteği */
    RX_END_WIFI,        /* WiFi ilişkisi düştü */
    RX_END_CLOSED,      /* adaptör TCP'yi kapattı */
    RX_END_ERROR,       /* recv hatası (RST, keepalive) */
    RX_END_SILENT,      /* gönderdik, SILENCE_MS boyunca tek bayt gelmedi */
} rx_end_t;

/* Bağlantı düşene ya da yeniden tarama istenene kadar baytları ELM'e akıt. */
static rx_end_t rx_loop(void)
{
    uint8_t buf[256];
    while (1) {
        EventBits_t b = xEventGroupGetBits(s_ev);
        if (b & BIT_BREAK) {
            return RX_END_BREAK;
        }
        if (!(b & BIT_GOT_IP)) {
            return RX_END_WIFI;
        }
        TickType_t since = s_wait_since;
        if (since && xTaskGetTickCount() - since > pdMS_TO_TICKS(SILENCE_MS)) {
            app_log_warn(TAG, "Adapter silent %d s after TX", SILENCE_MS / 1000);
            return RX_END_SILENT;
        }
        int n = recv(s_sock, buf, sizeof(buf), 0);
        if (n > 0) {
            s_wait_since = 0;
            s_session_rx = true;
            if (s_rx_cb) {
                s_rx_cb(buf, (size_t)n);
            }
        } else if (n == 0) {
            app_log_warn(TAG, "TCP closed by adapter");
            return RX_END_CLOSED;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            app_log_warn(TAG, "TCP recv error %d", errno);
            return RX_END_ERROR;
        }
    }
}

/* Bekle; otomatik bağlanma kapalıysa elle yeniden tarama gelene kadar. */
static void wait_retry(uint32_t delay_ms)
{
    TickType_t ticks = vehicle_data_get()->auto_connect ? pdMS_TO_TICKS(delay_ms) : portMAX_DELAY;
    xEventGroupWaitBits(s_ev, BIT_BREAK, pdFALSE, pdFALSE, ticks);
}

static void handle_rescan(void)
{
    if (!(xEventGroupGetBits(s_ev) & BIT_RESCAN)) {
        return;
    }
    xEventGroupClearBits(s_ev, BIT_RESCAN);
    tcp_close();
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));   /* geç gelen DISCONNECTED olayı yeni denemeyi bozmasın */
    xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP);
    s_force_scan = !CONFIG_OBD_WIFI_SSID[0];
}

static void wifi_obd_task(void *arg)
{
    (void)arg;
    uint32_t backoff = RETRY_MS;
    int tcp_fail = 0;
    int dead_sessions = 0;

    while (1) {
        /* stop() önce s_running'i düşürür sonra WAKE kurar: temizledikten
         * sonra yeniden okumak isteğin kaçmamasını garanti eder. */
        xEventGroupClearBits(s_ev, BIT_WAKE);
        if (!s_running) {
            tcp_close();
            if (s_wifi_started) {
                esp_wifi_stop();
                s_wifi_started = false;
                xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP);
                app_log_info(TAG, "WiFi stopped");
            }
            xEventGroupSetBits(s_ev, BIT_IDLE);
            xEventGroupWaitBits(s_ev, BIT_WAKE, pdFALSE, pdFALSE, portMAX_DELAY);
            continue;
        }
        if (!s_wifi_started) {
            esp_wifi_start();
            /* Modem-sleep her istek/yanıta DTIM kadar (~100-300 ms) gecikme ekler. */
            esp_wifi_set_ps(WIFI_PS_NONE);
            s_wifi_started = true;
            backoff = RETRY_MS;
            tcp_fail = 0;
        }
        handle_rescan();

        if (!(xEventGroupGetBits(s_ev) & BIT_GOT_IP) && !join_adapter()) {
            wait_retry(backoff);
            backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
            continue;
        }

        if (!tcp_open()) {
            if (++tcp_fail >= TCP_FAIL_MAX) {
                tcp_fail = 0;
                esp_wifi_disconnect();   /* adaptör köprüsü takılmış olabilir */
                xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP);
            }
            wait_retry(backoff);
            backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
            continue;
        }
        tcp_fail = 0;
        backoff = RETRY_MS;

        rx_end_t why = rx_loop();
        tcp_close();

        /* Veri alamadan biten oturum: adaptör eski (kopuk) oturumu tutuyor ya
         * da köprü takıldı. İkincisinde WiFi'yi baştan kur — çoğu klon istemci
         * ayrılınca TCP sunucusunu sıfırlıyor. */
        if (why != RX_END_BREAK && (why == RX_END_SILENT || !s_session_rx)) {
            if (++dead_sessions >= DEAD_SESSION_MAX) {
                dead_sessions = 0;
                app_log_warn(TAG, "No data in %d sessions, rejoining WiFi", DEAD_SESSION_MAX);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(100));
                xEventGroupClearBits(s_ev, BIT_ASSOC | BIT_GOT_IP);
            }
        } else {
            dead_sessions = 0;
        }

        static const char *const why_msg[] = {
            [RX_END_BREAK]  = "Disconnected",
            [RX_END_WIFI]   = "WiFi lost, rejoining",
            [RX_END_CLOSED] = "Adapter closed link",
            [RX_END_ERROR]  = "Link error, retrying",
            [RX_END_SILENT] = "No reply from adapter",
        };
        vehicle_data_set_state(OBD_STATE_DISCONNECTED, why_msg[why]);
        wait_retry(RETRY_MS);
    }
}

/* İlk start'ta bir kez: netif + WiFi sürücüsü (radyo esp_wifi_start'a kadar kapalı). */
static bool wifi_stack_init(void)
{
    s_ev = xEventGroupCreate();
    s_tx_mutex = xSemaphoreCreateMutex();
    load_saved_ssid();

    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "netif/event init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_net_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_net_event, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    return true;
}

void wifi_obd_start(void)
{
    static bool s_inited;
    static TaskHandle_t s_task;

    if (!s_inited) {
        if (!wifi_stack_init()) {
            return;
        }
        s_inited = true;
    }
    if (s_running) {
        return;
    }
    s_running = true;
    if (!s_task) {
        xTaskCreatePinnedToCore(wifi_obd_task, "wifi_obd", TASK_STACK, NULL, TASK_PRIO, &s_task, 0);
    } else {
        xEventGroupClearBits(s_ev, BIT_IDLE);
        xEventGroupSetBits(s_ev, BIT_WAKE);
    }
}

void wifi_obd_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    xEventGroupSetBits(s_ev, BIT_WAKE);
    /* Tarama (~2.5 sn) ve TCP connect (2×4 sn) kesilemez; en kötü durumu bekle. */
    if (!(xEventGroupWaitBits(s_ev, BIT_IDLE, pdFALSE, pdFALSE,
                              pdMS_TO_TICKS(STOP_TIMEOUT_MS)) & BIT_IDLE)) {
        app_log_warn(TAG, "WiFi stop timed out");
    }
}

void wifi_obd_rescan(void)
{
    if (s_ev) {
        xEventGroupSetBits(s_ev, BIT_RESCAN);
    }
}

bool wifi_obd_send(const uint8_t *data, size_t len)
{
    if (!s_connected) {
        return false;
    }
    size_t off = 0;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    while (s_sock >= 0 && off < len) {
        int n = send(s_sock, data + off, len - off, 0);
        if (n <= 0) {
            break;
        }
        off += (size_t)n;
    }
    xSemaphoreGive(s_tx_mutex);
    if (off == len && !s_wait_since) {
        s_wait_since = xTaskGetTickCount() | 1;   /* 0 = beklenen yanıt yok */
    }
    return off == len;
}

bool wifi_obd_is_connected(void)
{
    return s_connected;
}

void wifi_obd_set_rx_callback(wifi_obd_rx_cb_t cb)
{
    s_rx_cb = cb;
}
