#pragma once

/** Known ELM327 Bluetooth / BLE adapter name substrings (case-insensitive). */
static const char *const bt_elm327_name_hints[] = {
    "OBDII",
    "OBD2",
    "OBD",
    "ELM327",
    "VGATE",
    "V-LINK",
    "IOS-VLINK",
    "OBDLink",
    "VEEPEAK",
    "LELINK",
    "ICAR",
    "KONNWEI",
    "ANCEL",
    "BT",
    "BLE",
    NULL,
};

static inline bool bt_name_looks_like_elm327(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (int i = 0; bt_elm327_name_hints[i] != NULL; i++) {
        const char *hint = bt_elm327_name_hints[i];
        const char *p = name;
        while (*p != '\0') {
            const char *a = p;
            const char *b = hint;
            bool match = true;
            while (*b != '\0') {
                if (*a == '\0') {
                    match = false;
                    break;
                }
                char ca = *a;
                char cb = *b;
                if (ca >= 'a' && ca <= 'z') {
                    ca -= 32;
                }
                if (cb >= 'a' && cb <= 'z') {
                    cb -= 32;
                }
                if (ca != cb) {
                    match = false;
                    break;
                }
                a++;
                b++;
            }
            if (match) {
                return true;
            }
            p++;
        }
    }
    return false;
}
