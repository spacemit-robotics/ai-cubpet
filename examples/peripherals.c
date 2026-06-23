#define _DEFAULT_SOURCE

/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <light_sensor.h>
#include <misc_io.h>
#include <motor.h>
#include <nfc.h>
#include <pm.h>
#include <wifi.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

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

struct fixed_gpio_input {
    const char *name;
    const char *chip_name;
    unsigned int line_offset;
};

struct fixed_rohs_motor {
    const char *name;
    struct rohs_motor_info info;
};

static const char *k_default_i2c_dev = "/dev/i2c-5";
static const uint8_t k_nfc_i2c_addr = 0x28;
static const uint8_t k_nfc_demo_block = 4;
static const uint8_t k_light_sensor_i2c_addr = 0x48;

static const char *k_pm_charger_node = "ip2317-charger";
static const char *k_pm_capacity_node = "cw-bat";

static const struct fixed_gpio_input k_misc_inputs[] = {
    {"GPIO79", "gpiochip0", 79},
    {"GPIO77", "gpiochip0", 77},
    {"GPIO113", "gpiochip0", 113},
    {"GPIO126", "gpiochip0", 126},
    {"GPIO114", "gpiochip0", 114},
    {"GPIO73", "gpiochip0", 73},
};

static const struct fixed_rohs_motor k_rohs_motors[] = {
    {
        "head_lr",
        {
            .motor_index = 1,
            .step_gpio = 46,
            .dir_gpio = 43,
            .enable_gpio = 42,
            .stop_gpio = 83,
            .current_position = -1,
            .constant_range = 60,
            .gpio_max_steps = -1,
            .enable_gpio_level = false,
            .dir_gpio_left_level = false,
            .stop_gpio_active_level = false,
            .range_steps = 0,
        },
    },
    {
        "tail_lr",
        {
            .motor_index = 2,
            .step_gpio = 37,
            .dir_gpio = 38,
            .enable_gpio = 39,
            .stop_gpio = 61,
            .current_position = -1,
            .constant_range = 60,
            .gpio_max_steps = -1,
            .enable_gpio_level = false,
            .dir_gpio_left_level = false,
            .stop_gpio_active_level = false,
            .range_steps = 0,
        },
    },
    {
        "head_ud",
        {
            .motor_index = 3,
            .step_gpio = 34,
            .dir_gpio = 35,
            .enable_gpio = 36,
            .stop_gpio = 82,
            .current_position = -1,
            .constant_range = 60,
            .gpio_max_steps = -1,
            .enable_gpio_level = false,
            .dir_gpio_left_level = false,
            .stop_gpio_active_level = false,
            .range_steps = 0,
        },
    },
};

static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s misc_io [get|watch] [count]\n", prog);
    printf("  %s pm [count]\n", prog);
    printf("  %s wifi <scan|state|info|list|on|off|connect|disconnect|remove|mac> [args...]\n", prog);
    printf("  %s nfc [count]\n", prog);
    printf("  %s motor [all|head_lr|tail_lr|head_ud] [speed]\n", prog);
    printf("  %s fan [speed_percent] [seconds]\n", prog);
    printf("  %s light_sensor [count]\n", prog);
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

    if (strcmp(mode, "watch") == 0 || strcmp(mode, "trigger") == 0) {
        struct misc_dev *devs[ARRAY_SIZE(k_misc_inputs)] = {0};
        int ret = 0;

        for (size_t i = 0; i < ARRAY_SIZE(k_misc_inputs); ++i) {
            struct misc_gpiod_ctx ctx = {
                .chip_name = k_misc_inputs[i].chip_name,
                .line_offset = k_misc_inputs[i].line_offset,
                .consumer = "ai-cubpet",
            };

            devs[i] = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
            if (!devs[i]) {
                fprintf(stderr, "[misc_io] alloc failed: %s\n", k_misc_inputs[i].name);
                ret = 1;
                continue;
            }
            misc_io_config(devs[i], MISC_ACTIVE_HIGH, 0);
            misc_io_trigger(devs[i], misc_event_cb, (void *)k_misc_inputs[i].name);
        }
        for (int i = 0; i < count; ++i)
            sleep(1);
        for (size_t i = 0; i < ARRAY_SIZE(devs); ++i)
            misc_io_free(devs[i]);
        return ret;
    }

    if (strcmp(mode, "get") != 0) {
        fprintf(stderr, "unknown misc_io mode: %s\n", mode);
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        for (size_t j = 0; j < ARRAY_SIZE(k_misc_inputs); ++j) {
            struct misc_gpiod_ctx ctx = {
                .chip_name = k_misc_inputs[j].chip_name,
                .line_offset = k_misc_inputs[j].line_offset,
                .consumer = "ai-cubpet",
            };
            struct misc_dev *dev = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
            if (!dev) {
                fprintf(stderr, "[misc_io] alloc failed: %s\n", k_misc_inputs[j].name);
                continue;
            }
            printf("[misc_io] %s=%d\n", k_misc_inputs[j].name, misc_io_get(dev));
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
    struct pm_dev *batt = pm_alloc_generic("main_batt",
        k_pm_charger_node, k_pm_capacity_node, NULL);
    int ret;

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

    printf("[nfc] dev=%s addr=0x%02X block=%u\n",
        k_default_i2c_dev, k_nfc_i2c_addr, k_nfc_demo_block);
    dev = nfc_alloc_i2c("SI512:nfc0", k_default_i2c_dev, k_nfc_i2c_addr);
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
            ret = nfc_read_block(dev, k_nfc_demo_block, buf, sizeof(buf));
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

static int run_one_rohs_motor(const struct fixed_rohs_motor *fixed, float speed)
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

    info = fixed->info;

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

    for (size_t i = 0; i < ARRAY_SIZE(k_rohs_motors); ++i) {
        if (strcmp(which, "all") != 0 && strcmp(which, k_rohs_motors[i].name) != 0)
            continue;
        if (run_one_rohs_motor(&k_rohs_motors[i], speed) != 0)
            ret = 1;
    }

    if (ret == 0 && strcmp(which, "all") != 0) {
        bool found = false;
        for (size_t i = 0; i < ARRAY_SIZE(k_rohs_motors); ++i) {
            if (strcmp(which, k_rohs_motors[i].name) == 0)
                found = true;
        }
        if (!found) {
            fprintf(stderr, "unknown motor: %s\n", which);
            return 1;
        }
    }

    return ret;
}

static int run_fan(int argc, char **argv)
{
    int speed = parse_int(argc > 0 ? argv[0] : NULL, 50);
    int seconds = parse_int(argc > 1 ? argv[1] : NULL, 5);
    struct pwm_generic_info info = {
        .period = 100000,
        .duty_cycle = 0,
    };
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

    printf("[fan] gpio=72 speed=%d%% duration=%ds\n", speed, seconds);
    fan = motor_alloc_pwm("pwm_gpio", 72, &info);
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

static int run_light_sensor(int argc, char **argv)
{
    int count = parse_int(argc > 0 ? argv[0] : NULL, 20);
    struct light_sensor_dev *dev;

    printf("[light_sensor] dev=%s addr=0x%02X\n",
        k_default_i2c_dev, k_light_sensor_i2c_addr);
    dev = light_sensor_alloc_i2c("W1160:als0",
        k_default_i2c_dev, k_light_sensor_i2c_addr);
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
            printf("[light_sensor] light=%u lux\n", light_value);
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

    fprintf(stderr, "unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
