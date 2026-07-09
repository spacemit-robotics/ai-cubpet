#define _DEFAULT_SOURCE

/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cubpet_peripheral_config.h>
#include <imu.h>
#include <led.h>
#include <light_sensor.h>
#include <misc_io.h>
#include <motor.h>
#include <nfc.h>
#include <pm.h>
#include <wifi.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define LINKD_AP_SSID_MAX_LEN 32

struct rohs_motor_info {
    uint8_t motor_index;
    uint8_t step_gpio;
    uint8_t dir_gpio;
    uint8_t enable_gpio;
    uint8_t stop_gpio;

    int current_position;
    int constant_range;
    int gpio_max_steps;

    bool enable_gpio_level;
    bool dir_gpio_left_level;
    bool stop_gpio_active_level;

    int range_steps;
};

struct pwm_generic_info {
    uint32_t period;
    uint32_t duty_cycle;
};

struct led_ws2812_spi_args {
    const char *dev_path;
    uint32_t num_leds;
    uint32_t spi_speed_hz;
    uint32_t reset_bytes;
};

static struct cubpet_peripheral_config g_peripheral_config;
static char g_linkd_ap_ssid[LINKD_AP_SSID_MAX_LEN + 1] = "AI_CUBPET_AP_LINK";

static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s misc_io [get|watch] [count]\n", prog);
    printf("  %s pm [count]\n", prog);
    printf("  %s wifi <scan|state|info|list|on|off|connect|disconnect|remove|mac|linkd> [args...]\n", prog);
    printf("    wifi linkd [timeout_sec]  # soft-AP provisioning via http://192.168.1.1:8000\n");
    printf("  %s nfc [count]\n", prog);
    printf("  %s motor [all|NAME] [speed]\n", prog);
    printf("  %s fan [speed_percent] [seconds]\n", prog);
    printf("  %s light_sensor [count]\n", prog);
    printf("  %s g_sensor [count] [rate_hz] [shake_delta]\n", prog);
    printf("  %s led [loops] [tick_ms]\n", prog);
}

static int parse_int(const char *text, int fallback)
{
    char *end = NULL;
    long value;

    if (!text)
        return fallback;

    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || end == text || *end != '\0')
        return fallback;

    return (int)value;
}

static int load_peripheral_config(void)
{
    static bool loaded;
    static int load_rc;

    if (loaded)
        return load_rc;

    cubpet_peripheral_config_init_defaults(&g_peripheral_config);
    load_rc = cubpet_peripheral_config_load_auto(&g_peripheral_config);
    loaded = true;
    if (load_rc != 0) {
        fprintf(stderr, "failed to load peripheral config: %d\n", load_rc);
        return load_rc;
    }
    printf("[config] board=%s\n", g_peripheral_config.board_name);
    return 0;
}

static struct rohs_motor_info rohs_info_from_config(
    const struct cubpet_motor_config *config)
{
    struct rohs_motor_info info = {
        .motor_index = config->motor_index,
        .step_gpio = config->step_gpio,
        .dir_gpio = config->dir_gpio,
        .enable_gpio = config->enable_gpio,
        .stop_gpio = config->stop_gpio,
        .current_position = config->current_position,
        .constant_range = config->constant_range,
        .gpio_max_steps = config->gpio_max_steps,
        .enable_gpio_level = config->enable_gpio_level != 0,
        .dir_gpio_left_level = config->dir_gpio_left_level != 0,
        .stop_gpio_active_level = config->stop_gpio_active_level != 0,
        .range_steps = config->range_steps,
    };
    return info;
}

static void misc_event_cb(struct misc_dev *dev, enum misc_event ev,
    uint64_t timestamp_us, void *args)
{
    (void)dev;

    printf("[misc_io] %s event=%s timestamp_us=%" PRIu64 "\n",
        args ? (const char *)args : "(unknown)",
        ev == MISC_EV_ACTIVE ? "ACTIVE" : "INACTIVE",
        timestamp_us);
}

static int run_misc_io(int argc, char **argv)
{
    const char *mode = argc > 0 ? argv[0] : "get";
    int count = parse_int(argc > 1 ? argv[1] : NULL, 5);

    if (load_peripheral_config() != 0)
        return 1;

    if (strcmp(mode, "watch") == 0 || strcmp(mode, "trigger") == 0) {
        struct misc_dev *devs[CUBPET_PERIPHERAL_MAX_GPIO_INPUTS] = {0};
        int ret = 0;

        for (size_t i = 0; i < g_peripheral_config.gpio_input_count; ++i) {
            const struct cubpet_gpio_input_config *input =
                &g_peripheral_config.gpio_inputs[i];
            struct misc_gpiod_ctx ctx = {
                .chip_name = input->chip_name,
                .line_offset = input->line_offset,
                .consumer = "ai-cubpet",
            };

            devs[i] = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
            if (!devs[i]) {
                fprintf(stderr, "[misc_io] alloc failed: %s\n", input->name);
                ret = 1;
                continue;
            }
            misc_io_config(devs[i],
                input->active_high ? MISC_ACTIVE_HIGH : MISC_ACTIVE_LOW, 0);
            misc_io_trigger(devs[i], misc_event_cb, (void *)input->name);
        }
        for (int i = 0; i < count; ++i)
            sleep(1);
        for (size_t i = 0; i < g_peripheral_config.gpio_input_count; ++i)
            misc_io_free(devs[i]);
        return ret;
    }

    if (strcmp(mode, "get") != 0) {
        fprintf(stderr, "unknown misc_io mode: %s\n", mode);
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        for (size_t j = 0; j < g_peripheral_config.gpio_input_count; ++j) {
            const struct cubpet_gpio_input_config *input =
                &g_peripheral_config.gpio_inputs[j];
            struct misc_gpiod_ctx ctx = {
                .chip_name = input->chip_name,
                .line_offset = input->line_offset,
                .consumer = "ai-cubpet",
            };
            struct misc_dev *dev = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
            if (!dev) {
                fprintf(stderr, "[misc_io] alloc failed: %s\n", input->name);
                continue;
            }
            misc_io_config(dev,
                input->active_high ? MISC_ACTIVE_HIGH : MISC_ACTIVE_LOW, 0);
            printf("[misc_io] %s=%d\n", input->name, misc_io_get(dev));
            misc_io_free(dev);
        }
        sleep(1);
    }
    return 0;
}

static void pm_event_cb(struct pm_dev *dev, const struct pm_state *state, void *ctx)
{
    (void)dev;
    (void)ctx;

    if (!state)
        return;

    printf("[pm] callback SOC=%.1f%% status=%d\n",
        state->percentage, state->status);
}

static const char *pm_status_name(enum pm_status status)
{
    switch (status) {
    case PM_STATUS_DISCHARGING:
        return "Discharging";
    case PM_STATUS_CHARGING:
        return "Charging";
    case PM_STATUS_FULL:
        return "Full";
    case PM_STATUS_FAULT:
        return "Fault";
    case PM_STATUS_SLEEP:
        return "Sleep";
    case PM_STATUS_UNKNOWN:
    default:
        return "Unknown";
    }
}

static int run_pm(int argc, char **argv)
{
    int count = parse_int(argc > 0 ? argv[0] : NULL, 5);
    struct pm_dev *batt;
    int ret;

    if (load_peripheral_config() != 0)
        return 1;

    batt = pm_alloc_generic("main_batt",
        g_peripheral_config.pm.charger_node,
        g_peripheral_config.pm.capacity_node, NULL);
    if (!batt) {
        fprintf(stderr, "pm_alloc_generic failed\n");
        return 1;
    }

    ret = pm_init(batt, NULL);
    if (ret < 0) {
        fprintf(stderr, "pm_init failed: %d\n", ret);
        pm_free(batt);
        return 1;
    }

    pm_set_callback(batt, pm_event_cb, NULL);
    for (int i = 0; i < count; ++i) {
        struct pm_state state;
        ret = pm_get_state(batt, &state);
        if (ret == 0) {
            printf("[pm] SOC=%.1f%% status=%s voltage=%.2fV current=%.2fA\n",
                state.percentage, pm_status_name(state.status),
                state.voltage, state.current);
        } else {
            printf("[pm] read failed: %d\n", ret);
        }
        sleep(1);
    }

    pm_free(batt);
    return 0;
}

static void print_mac(const uint8_t *mac)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static char hex_lower(unsigned int value)
{
    value &= 0x0f;
    return (char)(value < 10 ? ('0' + value) : ('a' + value - 10));
}

static void build_linkd_ap_ssid(char *ssid, size_t ssid_size,
    const char *base, const uint8_t mac[6])
{
    if (!ssid || ssid_size == 0 || !base || !mac)
        return;

    snprintf(ssid, ssid_size, "%s_%c%c", base,
        hex_lower(mac[5] >> 4), hex_lower(mac[5]));
}

static enum wifi_mode parse_wifi_mode(const char *text)
{
    if (!text)
        return WIFI_MODE_UNKNOWN;
    if (strcmp(text, "station") == 0 || strcmp(text, "sta") == 0 || strcmp(text, "1") == 0)
        return WIFI_MODE_STATION;
    if (strcmp(text, "ap") == 0 || strcmp(text, "2") == 0)
        return WIFI_MODE_AP;
    if (strcmp(text, "station_ap") == 0 || strcmp(text, "sta_ap") == 0 || strcmp(text, "3") == 0)
        return WIFI_MODE_STATION_AP;
    return WIFI_MODE_UNKNOWN;
}

static void set_default_linkd_ap_config(struct wifi_ap_config *config)
{
    static const char ssid_base[] = "AI_CUBPET_AP_LINK";
    static char psk[] = "12345678";
    uint8_t mac[6] = {0};

    if (!config)
        return;

    memset(config, 0, sizeof(*config));
    snprintf(g_linkd_ap_ssid, sizeof(g_linkd_ap_ssid), "%s", ssid_base);
    if (wifi_get_mac(NULL, mac) == WIFI_STATUS_SUCCESS) {
        build_linkd_ap_ssid(g_linkd_ap_ssid, sizeof(g_linkd_ap_ssid),
            ssid_base, mac);
    } else {
        printf("[wifi][linkd] failed to get MAC, using SSID=%s\n",
            g_linkd_ap_ssid);
    }

    config->ssid = g_linkd_ap_ssid;
    config->psk = psk;
    config->sec = WIFI_SEC_WPA2_PSK;
    config->ip_addr[0] = 192;
    config->ip_addr[1] = 168;
    config->ip_addr[2] = 1;
    config->ip_addr[3] = 1;
    config->gw_addr[0] = 192;
    config->gw_addr[1] = 168;
    config->gw_addr[2] = 1;
    config->gw_addr[3] = 1;
}

static void linkd_connect_cb(struct wifi_msg_data *msg)
{
    struct wifi_linkd_result *result;
    struct wifi_sta_connect_param param;
    enum wifi_status ret;

    if (!msg)
        return;

    result = (struct wifi_linkd_result *)msg->private_data;
    if (!result || !result->ssid)
        return;

    printf("[wifi][linkd] ssid=%s psk=%s\n",
        result->ssid ? result->ssid : "(null)",
        result->psk ? result->psk : "(null)");

    memset(&param, 0, sizeof(param));
    param.ssid = result->ssid;
    if (result->psk && *result->psk)
        param.password = result->psk;

    ret = wifi_sta_remove_networks(g_linkd_ap_ssid);
    printf("[wifi][linkd] remove temp AP profile %s: %d\n",
        g_linkd_ap_ssid, ret);

    ret = wifi_on(WIFI_MODE_STATION);
    if (ret != WIFI_STATUS_SUCCESS)
        printf("[wifi][linkd] wifi_on station failed: %d\n", ret);
    ret = wifi_sta_connect(&param);
    printf("[wifi][linkd] wifi_sta_connect: %d\n", ret);
}

static int run_wifi_linkd(int argc, char **argv)
{
    struct wifi_ap_config config;
    int timeout_sec = parse_int(argc > 0 ? argv[0] : NULL, 300);
    enum wifi_status ret;
    enum wifi_status disable_ret;

    if (timeout_sec <= 0)
        timeout_sec = 300;

    set_default_linkd_ap_config(&config);
    ret = wifi_on(WIFI_MODE_AP);
    if (ret != WIFI_STATUS_SUCCESS)
        printf("[wifi][linkd] wifi_on ap failed: %d\n", ret);

    ret = wifi_ap_enable(&config);
    printf("[wifi][linkd] wifi_ap_enable: %d\n", ret);
    if (ret != WIFI_STATUS_SUCCESS)
        return 1;

    printf("[wifi][linkd] connect to AP SSID=%s password=%s\n",
        config.ssid, config.psk);
    printf("[wifi][linkd] open http://192.168.1.1:8000 and submit target Wi-Fi\n");
    printf("[wifi][linkd] waiting for credentials, timeout=%ds\n", timeout_sec);

    ret = wifi_linkd_protocol(WIFI_LINKD_MODE_SOFTAP,
        linkd_connect_cb, NULL, timeout_sec);
    printf("[wifi][linkd] wifi_linkd_protocol: %d\n", ret);

    if (ret != WIFI_STATUS_SUCCESS) {
        disable_ret = wifi_ap_disable();
        printf("[wifi][linkd] wifi_ap_disable: %d\n", disable_ret);
    } else {
        printf("[wifi][linkd] provisioning callback handled AP handoff\n");
    }

    return ret == WIFI_STATUS_SUCCESS ? 0 : 1;
}

static int run_wifi(int argc, char **argv)
{
    const char *cmd = argc > 0 ? argv[0] : "state";
    enum wifi_status ret = wifi_init();

    if (ret != WIFI_STATUS_SUCCESS) {
        fprintf(stderr, "wifi_init failed: %d\n", ret);
        return 1;
    }

    if (strcmp(cmd, "scan") == 0) {
        struct wifi_scan_result results[32];
        uint32_t total = 0;

        ret = wifi_on(WIFI_MODE_STATION);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("[wifi] wifi_on failed: %d\n", ret);

        ret = wifi_get_scan_results(results, NULL, &total, ARRAY_SIZE(results));
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("[wifi] scan failed: %d\n", ret);
        } else {
            printf("[wifi] found %u networks\n", total);
            for (uint32_t i = 0; i < total && i < ARRAY_SIZE(results); ++i) {
                printf("  SSID=%s RSSI=%d FREQ=%u SEC=%d BSSID=",
                    results[i].ssid, results[i].rssi,
                    results[i].freq, results[i].key_mgmt);
                print_mac(results[i].bssid);
                printf("\n");
            }
        }
    } else if (strcmp(cmd, "state") == 0) {
        struct wifi_state state;

        ret = wifi_get_state(&state);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("[wifi] wifi_get_state failed: %d\n", ret);
        } else {
            printf("[wifi] support_mode=%u current_mode=%u init=%u enable=%u sta_state=%d ap_state=%d\n",
                state.support_mode, state.current_mode,
                state.current_mode_init_flag, state.current_mode_enable_flag,
                state.sta_state, state.ap_state);
        }
    } else if (strcmp(cmd, "info") == 0) {
        struct wifi_sta_info info;

        ret = wifi_sta_get_info(&info);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("[wifi] wifi_sta_get_info failed: %d\n", ret);
        } else {
            printf("[wifi] SSID=%s RSSI=%d FREQ=%d IP=%u.%u.%u.%u\n",
                info.ssid, info.rssi, info.freq,
                info.ip_addr[0], info.ip_addr[1], info.ip_addr[2], info.ip_addr[3]);
        }
    } else if (strcmp(cmd, "list") == 0) {
        struct wifi_sta_list list;

        memset(&list, 0, sizeof(list));
        ret = wifi_sta_list_networks(&list);
        if (ret != WIFI_STATUS_SUCCESS) {
            printf("[wifi] wifi_sta_list_networks failed: %d\n", ret);
        } else {
            printf("[wifi] saved networks: %d\n", list.list_num);
            for (int i = 0; i < list.list_num; ++i)
                printf("  [%d] %s\n", list.nodes[i].id, list.nodes[i].ssid);
        }
        free(list.nodes);
    } else if (strcmp(cmd, "on") == 0 || strcmp(cmd, "off") == 0) {
        enum wifi_mode mode = parse_wifi_mode(argc > 1 ? argv[1] : "station");

        ret = strcmp(cmd, "on") == 0 ? wifi_on(mode) : wifi_off(mode);
        printf("[wifi] wifi_%s: %d\n", cmd, ret);
    } else if (strcmp(cmd, "connect") == 0) {
        struct wifi_sta_connect_param param;

        if (argc < 2) {
            fprintf(stderr, "wifi connect requires ssid [password]\n");
            wifi_deinit();
            return 1;
        }
        memset(&param, 0, sizeof(param));
        param.ssid = argv[1];
        if (argc > 2)
            param.password = argv[2];
        ret = wifi_on(WIFI_MODE_STATION);
        if (ret != WIFI_STATUS_SUCCESS)
            printf("[wifi] wifi_on failed: %d\n", ret);
        ret = wifi_sta_connect(&param);
        printf("[wifi] wifi_sta_connect: %d\n", ret);
    } else if (strcmp(cmd, "disconnect") == 0) {
        ret = wifi_sta_disconnect();
        printf("[wifi] wifi_sta_disconnect: %d\n", ret);
    } else if (strcmp(cmd, "remove") == 0) {
        ret = wifi_sta_remove_networks(argc > 1 ? argv[1] : NULL);
        printf("[wifi] wifi_sta_remove_networks: %d\n", ret);
    } else if (strcmp(cmd, "mac") == 0) {
        uint8_t mac[6] = {0};

        ret = wifi_get_mac(argc > 1 ? argv[1] : NULL, mac);
        if (ret == WIFI_STATUS_SUCCESS) {
            printf("[wifi] MAC=");
            print_mac(mac);
            printf("\n");
        } else {
            printf("[wifi] wifi_get_mac failed: %d\n", ret);
        }
    } else if (strcmp(cmd, "linkd") == 0) {
        int rc = run_wifi_linkd(argc - 1, argv + 1);
        wifi_deinit();
        return rc;
    } else {
        fprintf(stderr, "unknown wifi command: %s\n", cmd);
        wifi_deinit();
        return 1;
    }

    wifi_deinit();
    return 0;
}

static void nfc_event_cb(struct nfc_dev *dev, const struct nfc_tag_info *info, void *ctx)
{
    (void)dev;
    (void)ctx;

    printf("[nfc] callback uid_len=%u type=%d rssi=%d\n",
        info ? info->uid_len : 0,
        info ? (int)info->type : -1,
        info ? info->rssi_dbm : 0);
}

static void print_nfc_uid(const struct nfc_tag_info *info)
{
    printf("[nfc] UID:");
    for (uint8_t i = 0; i < info->uid_len; ++i)
        printf(" %02X", info->uid[i]);
    printf("\n");
}

static int run_nfc(int argc, char **argv)
{
    int count = parse_int(argc > 0 ? argv[0] : NULL, 20);
    struct nfc_dev *dev;

    if (load_peripheral_config() != 0)
        return 1;

    printf("[nfc] dev=%s addr=0x%02X block=%u\n",
        g_peripheral_config.nfc.i2c_dev,
        g_peripheral_config.nfc.i2c_addr,
        g_peripheral_config.nfc.demo_block);
    dev = nfc_alloc_i2c(g_peripheral_config.nfc.name,
        g_peripheral_config.nfc.i2c_dev,
        g_peripheral_config.nfc.i2c_addr);
    if (!dev) {
        fprintf(stderr, "nfc_alloc_i2c failed\n");
        return 1;
    }

    nfc_set_callback(dev, nfc_event_cb, NULL);
    if (nfc_init(dev) < 0) {
        fprintf(stderr, "nfc_init failed\n");
        nfc_free(dev);
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        struct nfc_tag_info info;
        int ret = nfc_poll(dev, &info, 100);

        if (ret == 0) {
            uint8_t buf[16] = {0};
            print_nfc_uid(&info);
            ret = nfc_read_block(dev, g_peripheral_config.nfc.demo_block,
                buf, sizeof(buf));
            printf("[nfc] read block ret=%d first4=%02X %02X %02X %02X\n",
                ret, buf[0], buf[1], buf[2], buf[3]);
            break;
        }
        if (ret == 1) {
            printf("[nfc] no tag\n");
        } else {
            printf("[nfc] poll error=%d\n", ret);
        }
        usleep(200000);
    }

    nfc_free(dev);
    return 0;
}

static int run_one_rohs_motor(const struct cubpet_motor_config *fixed, float speed)
{
    struct rohs_motor_info info;
    struct motor_dev *motor;
    struct motor_cmd cmd = {
        .mode = MOTOR_MODE_POS,
        .pos_des = 0.0f,
        .vel_des = speed,
    };
    struct motor_state state;
    const float positions[] = {0.0f, 180.0f, 90.0f, 75.0f, 105.0f, 90.0f};

    if (!fixed)
        return 1;

    info = rohs_info_from_config(fixed);

    printf("[motor] %s index=%u step=%u dir=%u sleep=%u stop=%u range=%d speed=%.1f\n",
        fixed->name,
        info.motor_index, info.step_gpio, info.dir_gpio, info.enable_gpio,
        info.stop_gpio, info.constant_range, cmd.vel_des);

    motor = motor_alloc_pwm("pwm_RoHS", 0, &info);
    if (!motor) {
        fprintf(stderr, "motor_alloc_pwm(pwm_RoHS) failed\n");
        return 1;
    }

    if (motor_init_one(motor) < 0) {
        fprintf(stderr, "motor_init_one failed\n");
        motor_free(&motor, 1);
        return 1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(positions); ++i) {
        cmd.pos_des = positions[i];
        motor_set_cmd_one(motor, &cmd);
        motor_get_state_one(motor, &state);
        printf("[motor] target=%.1f pos=%.2f vel=%.2f trq=%.2f\n",
            cmd.pos_des, state.pos, state.vel, state.trq);
        usleep(500000);
    }

    cmd.mode = MOTOR_MODE_IDLE;
    motor_set_cmd_one(motor, &cmd);
    motor_free(&motor, 1);
    return 0;
}

static int run_motor(int argc, char **argv)
{
    const char *which = argc > 0 ? argv[0] : "all";
    float speed = (float)parse_int(argc > 1 ? argv[1] : NULL, 2);
    int ret = 0;
    bool found = false;

    if (load_peripheral_config() != 0)
        return 1;

    for (size_t i = 0; i < g_peripheral_config.motor_count; ++i) {
        const struct cubpet_motor_config *motor =
            &g_peripheral_config.motors[i];
        if (strcmp(which, "all") != 0 && strcmp(which, motor->name) != 0)
            continue;
        found = true;
        if (run_one_rohs_motor(motor, speed) != 0)
            ret = 1;
    }

    if (!found && strcmp(which, "all") != 0) {
        fprintf(stderr, "unknown motor: %s\n", which);
        return 1;
    }

    return ret;
}

static int run_fan(int argc, char **argv)
{
    int speed = parse_int(argc > 0 ? argv[0] : NULL, 50);
    int seconds = parse_int(argc > 1 ? argv[1] : NULL, 5);
    struct pwm_generic_info info;
    struct motor_cmd cmd = {
        .mode = MOTOR_MODE_VEL,
        .vel_des = (float)speed,
    };
    struct motor_dev *fan;

    if (speed < 0)
        speed = 0;
    if (speed > 100)
        speed = 100;
    if (seconds < 0)
        seconds = 0;

    if (load_peripheral_config() != 0)
        return 1;

    info.period = g_peripheral_config.fan.period;
    info.duty_cycle = g_peripheral_config.fan.duty_cycle;

    printf("[fan] gpio=%u speed=%d%% duration=%ds\n",
        g_peripheral_config.fan.gpio, speed, seconds);
    fan = motor_alloc_pwm("pwm_gpio", g_peripheral_config.fan.gpio, &info);
    if (!fan) {
        fprintf(stderr, "motor_alloc_pwm(pwm_gpio) failed\n");
        return 1;
    }

    if (motor_init_one(fan) < 0) {
        fprintf(stderr, "fan motor_init_one failed\n");
        motor_free(&fan, 1);
        return 1;
    }

    motor_set_cmd_one(fan, &cmd);
    sleep((unsigned int)seconds);

    cmd.mode = MOTOR_MODE_IDLE;
    cmd.vel_des = 0.0f;
    motor_set_cmd_one(fan, &cmd);
    motor_free(&fan, 1);
    return 0;
}

static void led_sleep_ms(uint32_t duration_ms)
{
    if (duration_ms == 0)
        return;
    usleep((useconds_t)duration_ms * 1000U);
}

static void run_led_ticks(struct led_dev *dev, uint32_t duration_ms, uint16_t tick_ms)
{
    uint32_t elapsed = 0;

    if (!dev || tick_ms == 0)
        return;

    while (elapsed < duration_ms) {
        uint16_t step = tick_ms;

        if (duration_ms - elapsed < step)
            step = (uint16_t)(duration_ms - elapsed);
        led_tick(dev, step);
        usleep((useconds_t)step * 1000U);
        elapsed += step;
    }
}

static void led_run_solid_stage(struct led_dev *dev, const char *label,
    const struct led_color *color, uint8_t brightness, uint32_t duration_ms)
{
    printf("[led] %s color=(%u,%u,%u) brightness=%u duration=%ums\n",
        label, color->r, color->g, color->b, brightness, duration_ms);
    led_set_color(dev, color);
    led_set_brightness(dev, brightness);
    led_sleep_ms(duration_ms);
}

static int run_led(int argc, char **argv)
{
    int loops = parse_int(argc > 0 ? argv[0] : NULL, 1);
    int tick_ms = parse_int(argc > 1 ? argv[1] : NULL, 50);
    struct led_ws2812_spi_args args;
    struct led_dev *dev;
    const struct cubpet_led_config *cfg;

    if (load_peripheral_config() != 0)
        return 1;

    cfg = &g_peripheral_config.led;
    if (loops <= 0)
        loops = 1;
    if (tick_ms <= 0)
        tick_ms = 50;
    if (tick_ms > UINT16_MAX)
        tick_ms = UINT16_MAX;

    if (strcmp(cfg->type, "spi-ws2812") != 0 || cfg->name[0] == '\0' ||
        cfg->dev_path[0] == '\0') {
        fprintf(stderr, "[led] unsupported or missing LED config: type=%s name=%s dev=%s\n",
            cfg->type, cfg->name, cfg->dev_path);
        return 1;
    }

    printf("[led] board=%s type=%s name=%s dev=%s leds=%u speed=%" PRIu32
        " reset=%" PRIu32 " loops=%d tick_ms=%d\n",
        g_peripheral_config.board_name, cfg->type, cfg->name, cfg->dev_path,
        cfg->num_leds, cfg->spi_speed_hz, cfg->reset_bytes, loops, tick_ms);

    args.dev_path = cfg->dev_path;
    args.num_leds = cfg->num_leds;
    args.spi_speed_hz = cfg->spi_speed_hz;
    args.reset_bytes = cfg->reset_bytes;

    dev = led_alloc_spi(cfg->name, &args);
    if (!dev) {
        fprintf(stderr, "[led] led_alloc_spi failed\n");
        return 1;
    }

    for (int i = 0; i < loops; ++i) {
        struct led_blink_param blink = {
            .period_ms = 800,
            .on_ms = 200,
            .count = 3,
        };
        struct led_color warm = {130, 200, 30};

        printf("[led] loop %d/%d\n", i + 1, loops);
        led_run_solid_stage(dev, "red", &LED_COLOR_RED, 128, 700);
        led_run_solid_stage(dev, "green", &LED_COLOR_GREEN, 96, 700);
        led_run_solid_stage(dev, "blue", &LED_COLOR_BLUE, 64, 700);

        printf("[led] blink color=(255,255,255) brightness=255 period=%ums on=%ums count=%u\n",
            blink.period_ms, blink.on_ms, blink.count);
        led_set_color(dev, &LED_COLOR_WHITE);
        led_set_brightness(dev, 255);
        led_blink(dev, &blink);
        run_led_ticks(dev, 2800, (uint16_t)tick_ms);

        printf("[led] breath color=(%u,%u,%u) period=2000ms\n",
            warm.r, warm.g, warm.b);
        led_set_color(dev, &warm);
        led_set_brightness(dev, 180);
        led_breath(dev, 2000);
        run_led_ticks(dev, 4000, (uint16_t)tick_ms);
    }

    printf("[led] off\n");
    led_set_state(dev, false);
    led_free(dev);
    return 0;
}

static const char *light_level_name(uint32_t lux)
{
    if (lux < 400)
        return "dark";
    if (lux < 1000)
        return "normal";
    if (lux < 1600)
        return "bright";
    return "strong";
}

static int run_light_sensor(int argc, char **argv)
{
    int count = parse_int(argc > 0 ? argv[0] : NULL, 20);
    struct light_sensor_dev *dev;

    if (load_peripheral_config() != 0)
        return 1;

    printf("[light_sensor] dev=%s addr=0x%02X\n",
        g_peripheral_config.light_sensor.i2c_dev,
        g_peripheral_config.light_sensor.i2c_addr);
    dev = light_sensor_alloc_i2c(g_peripheral_config.light_sensor.name,
        g_peripheral_config.light_sensor.i2c_dev,
        g_peripheral_config.light_sensor.i2c_addr);
    if (!dev) {
        fprintf(stderr, "light_sensor_alloc_i2c failed\n");
        return 1;
    }

    if (light_sensor_init(dev) < 0) {
        fprintf(stderr, "light_sensor_init failed\n");
        light_sensor_free(dev);
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        uint32_t light_value = 0;
        int ret = light_sensor_poll(dev, &light_value);

        if (ret == 0) {
            printf("[light_sensor] light=%u lux level=%s\n",
                light_value, light_level_name(light_value));
        } else if (ret == -EAGAIN) {
            printf("[light_sensor] no new FIFO data\n");
        } else {
            printf("[light_sensor] poll failed: %d\n", ret);
        }
        sleep(1);
    }

    light_sensor_free(dev);
    return 0;
}

static int run_g_sensor(int argc, char **argv)
{
    int count = parse_int(argc > 0 ? argv[0] : NULL, 50);
    int rate_hz = parse_int(argc > 1 ? argv[1] : NULL, 10);
    int shake_delta = parse_int(argc > 2 ? argv[2] : NULL, 120);
    struct imu_dev *imu;
    struct imu_config cfg = {0};
    struct imu_data data;
    bool have_prev = false;
    float prev[3] = {0};

    if (count <= 0)
        count = 50;
    if (rate_hz <= 0)
        rate_hz = 10;
    if (shake_delta < 0)
        shake_delta = 120;

    if (load_peripheral_config() != 0)
        return 1;

    cfg.sample_rate = (uint32_t)rate_hz;
    cfg.mounting_matrix[0] = 1.0f;
    cfg.mounting_matrix[4] = 1.0f;
    cfg.mounting_matrix[8] = 1.0f;

    printf("[g_sensor] name=mxc4005 selector=%s index=%u rate=%dHz shake_delta=%d\n",
        g_peripheral_config.g_sensor.i2c_dev,
        g_peripheral_config.g_sensor.i2c_addr,
        rate_hz,
        shake_delta);

    imu = imu_alloc_i2c("mxc4005",
        g_peripheral_config.g_sensor.i2c_dev,
        g_peripheral_config.g_sensor.i2c_addr,
        NULL);
    if (!imu) {
        fprintf(stderr, "imu_alloc_i2c(mxc4005) failed\n");
        return 1;
    }

    if (imu_init(imu, &cfg) != 0) {
        fprintf(stderr, "imu_init failed\n");
        imu_free(imu);
        return 1;
    }

    printf("%-14s %-12s %-12s %-12s %-12s\n",
        "timestamp_us", "acc_x_raw", "acc_y_raw", "acc_z_raw", "event");
    for (int i = 0; i < count; ++i) {
        if (imu_read(imu, &data) == 0) {
            const float dx = have_prev ? data.acc[0] - prev[0] : 0.0f;
            const float dy = have_prev ? data.acc[1] - prev[1] : 0.0f;
            const float dz = have_prev ? data.acc[2] - prev[2] : 0.0f;
            const float delta = fabsf(dx) + fabsf(dy) + fabsf(dz);
            const bool shake = have_prev && delta >= (float)shake_delta;

            printf("%-14" PRIu64 " %-12.0f %-12.0f %-12.0f %-12s\n",
                data.timestamp_us, data.acc[0], data.acc[1], data.acc[2],
                shake ? "shake" : "--");
            prev[0] = data.acc[0];
            prev[1] = data.acc[1];
            prev[2] = data.acc[2];
            have_prev = true;
        } else {
            printf("[g_sensor] read failed\n");
        }
        usleep((useconds_t)(1000000 / rate_hz));
    }

    imu_free(imu);
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd;

    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    cmd = argv[1];
    if (strcmp(cmd, "misc_io") == 0)
        return run_misc_io(argc - 2, argv + 2);
    if (strcmp(cmd, "pm") == 0)
        return run_pm(argc - 2, argv + 2);
    if (strcmp(cmd, "wifi") == 0)
        return run_wifi(argc - 2, argv + 2);
    if (strcmp(cmd, "nfc") == 0)
        return run_nfc(argc - 2, argv + 2);
    if (strcmp(cmd, "motor") == 0)
        return run_motor(argc - 2, argv + 2);
    if (strcmp(cmd, "fan") == 0)
        return run_fan(argc - 2, argv + 2);
    if (strcmp(cmd, "light_sensor") == 0)
        return run_light_sensor(argc - 2, argv + 2);
    if (strcmp(cmd, "g_sensor") == 0)
        return run_g_sensor(argc - 2, argv + 2);
    if (strcmp(cmd, "led") == 0)
        return run_led(argc - 2, argv + 2);

    fprintf(stderr, "unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
