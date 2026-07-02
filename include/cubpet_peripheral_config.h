#ifndef CUBPET_PERIPHERAL_CONFIG_H
#define CUBPET_PERIPHERAL_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CUBPET_PERIPHERAL_MAX_MOTORS 8
#define CUBPET_PERIPHERAL_MAX_GPIO_INPUTS 16
#define CUBPET_PERIPHERAL_NAME_LEN 64
#define CUBPET_PERIPHERAL_PATH_LEN 128

struct cubpet_motor_config {
    char name[CUBPET_PERIPHERAL_NAME_LEN];
    uint8_t motor_index;
    uint8_t step_gpio;
    uint8_t dir_gpio;
    uint8_t enable_gpio;
    uint8_t stop_gpio;
    int current_position;
    int constant_range;
    int gpio_max_steps;
    int enable_gpio_level;
    int dir_gpio_left_level;
    int stop_gpio_active_level;
    int range_steps;
};

struct cubpet_i2c_device_config {
    char name[CUBPET_PERIPHERAL_NAME_LEN];
    char i2c_dev[CUBPET_PERIPHERAL_PATH_LEN];
    unsigned int scl_gpio;
    unsigned int sda_gpio;
    uint8_t i2c_addr;
    uint8_t demo_block;
    unsigned int irq_gpio;
    unsigned int reset_gpio;
    unsigned int int1_gpio;
};

struct cubpet_gpio_input_config {
    char name[CUBPET_PERIPHERAL_NAME_LEN];
    char role[CUBPET_PERIPHERAL_NAME_LEN];
    char chip_name[CUBPET_PERIPHERAL_NAME_LEN];
    unsigned int line_offset;
    int active_high;
};

struct cubpet_pm_config {
    char charger_node[CUBPET_PERIPHERAL_NAME_LEN];
    char capacity_node[CUBPET_PERIPHERAL_NAME_LEN];
};

struct cubpet_fan_config {
    unsigned int gpio;
    uint32_t period;
    uint32_t duty_cycle;
};

struct cubpet_peripheral_config {
    char board_name[CUBPET_PERIPHERAL_NAME_LEN];
    struct cubpet_motor_config motors[CUBPET_PERIPHERAL_MAX_MOTORS];
    size_t motor_count;
    struct cubpet_i2c_device_config nfc;
    struct cubpet_i2c_device_config light_sensor;
    struct cubpet_i2c_device_config g_sensor;
    struct cubpet_gpio_input_config gpio_inputs[CUBPET_PERIPHERAL_MAX_GPIO_INPUTS];
    size_t gpio_input_count;
    struct cubpet_pm_config pm;
    struct cubpet_fan_config fan;
};

void cubpet_peripheral_config_init_defaults(struct cubpet_peripheral_config* config);
int cubpet_peripheral_config_load_for_model(const char* model,
                                            const char* board_config_dir,
                                            struct cubpet_peripheral_config* config);
int cubpet_peripheral_config_load_auto(struct cubpet_peripheral_config* config);
const struct cubpet_motor_config* cubpet_peripheral_find_motor(
    const struct cubpet_peripheral_config* config,
    const char* name);

#ifdef __cplusplus
}
#endif

#endif  // CUBPET_PERIPHERAL_CONFIG_H
