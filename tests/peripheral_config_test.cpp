#include "cubpet_peripheral_config.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void ExpectStringEq(const char* actual, const char* expected)
{
    assert(actual != nullptr);
    assert(std::strcmp(actual, expected) == 0);
}

void TestLoadMusePiPro()
{
    cubpet_peripheral_config config;
    cubpet_peripheral_config_init_defaults(&config);

    const int rc = cubpet_peripheral_config_load_for_model(
        "spacemit k1-x MUSE-Pi-Pro board",
        "tests/fixtures/boards",
        &config);

    assert(rc == 0);
    ExpectStringEq(config.board_name, "MUSE-Pi-Pro");

    assert(config.motor_count == 2);
    ExpectStringEq(config.motors[0].name, "head_lr");
    assert(config.motors[0].motor_index == 1);
    assert(config.motors[0].step_gpio == 51);
    assert(config.motors[0].dir_gpio == 52);
    assert(config.motors[0].enable_gpio == 50);
    assert(config.motors[0].stop_gpio == 0);
    assert(config.motors[0].constant_range == 60);
    assert(config.motors[0].gpio_max_steps == -1);

    ExpectStringEq(config.motors[1].name, "head_ud");
    assert(config.motors[1].motor_index == 2);
    assert(config.motors[1].step_gpio == 47);
    assert(config.motors[1].dir_gpio == 48);
    assert(config.motors[1].enable_gpio == 46);
    assert(config.motors[1].stop_gpio == 0);

    ExpectStringEq(config.nfc.name, "SI512:nfc0");
    ExpectStringEq(config.nfc.i2c_dev, "/dev/i2c-4");
    assert(config.nfc.scl_gpio == 41);
    assert(config.nfc.sda_gpio == 40);
    assert(config.nfc.i2c_addr == 0x28);
    assert(config.nfc.demo_block == 4);
    assert(config.nfc.irq_gpio == 72);
    assert(config.nfc.reset_gpio == 73);

    ExpectStringEq(config.light_sensor.name, "W1160:als0");
    ExpectStringEq(config.light_sensor.i2c_dev, "/dev/i2c-3");
    assert(config.light_sensor.scl_gpio == 38);
    assert(config.light_sensor.sda_gpio == 39);
    assert(config.light_sensor.i2c_addr == 0x48);

    ExpectStringEq(config.g_sensor.name, "g-sensor");
    ExpectStringEq(config.g_sensor.i2c_dev, "/dev/i2c-2");
    assert(config.g_sensor.scl_gpio == 70);
    assert(config.g_sensor.sda_gpio == 71);
    assert(config.g_sensor.i2c_addr == 0x19);
    assert(config.g_sensor.int1_gpio == 49);

    assert(config.gpio_input_count == 4);
    ExpectStringEq(config.gpio_inputs[0].name, "touch1");
    ExpectStringEq(config.gpio_inputs[0].role, "head");
    ExpectStringEq(config.gpio_inputs[0].chip_name, "gpiochip0");
    assert(config.gpio_inputs[0].line_offset == 33);
    assert(config.gpio_inputs[0].active_high == 1);
    ExpectStringEq(config.gpio_inputs[1].name, "touch2");
    ExpectStringEq(config.gpio_inputs[1].role, "nose");
    assert(config.gpio_inputs[1].line_offset == 34);
    ExpectStringEq(config.gpio_inputs[2].name, "touch3");
    ExpectStringEq(config.gpio_inputs[2].role, "foot");
    assert(config.gpio_inputs[2].line_offset == 35);
    ExpectStringEq(config.gpio_inputs[3].name, "wake");
    ExpectStringEq(config.gpio_inputs[3].role, "wake");
    assert(config.gpio_inputs[3].line_offset == 92);
    assert(config.gpio_inputs[3].active_high == 0);

    ExpectStringEq(config.pm.charger_node, "ip2317-charger");
    ExpectStringEq(config.pm.capacity_node, "cw-bat");

    assert(config.fan.gpio == 91);
    assert(config.fan.period == 100000);

    ExpectStringEq(config.led.type, "spi-ws2812");
    ExpectStringEq(config.led.name, "spi-ws2812:cubpet_rgb");
    ExpectStringEq(config.led.dev_path, "/dev/spidev2.0");
    assert(config.led.num_leds == 23);
    assert(config.led.spi_speed_hz == 6400000);
    assert(config.led.reset_bytes == 80);
}

void TestNoMatch()
{
    cubpet_peripheral_config config;
    cubpet_peripheral_config_init_defaults(&config);

    const int rc = cubpet_peripheral_config_load_for_model(
        "unknown board",
        "tests/fixtures/boards",
        &config);

    assert(rc != 0);
}

void TestExplicitConfigOverride()
{
    cubpet_peripheral_config config;
    cubpet_peripheral_config_init_defaults(&config);

    setenv("AI_CUBPET_PERIPHERAL_CONFIG",
        "tests/fixtures/boards/MUSE-Pi-Pro.json",
        1);
    const int rc = cubpet_peripheral_config_load_for_model(
        "unknown board",
        nullptr,
        &config);
    unsetenv("AI_CUBPET_PERIPHERAL_CONFIG");

    assert(rc == 0);
    ExpectStringEq(config.board_name, "MUSE-Pi-Pro");
}

}  // namespace

int main()
{
    TestLoadMusePiPro();
    TestNoMatch();
    TestExplicitConfigOverride();
    std::cout << "peripheral config tests passed\n";
    return 0;
}
