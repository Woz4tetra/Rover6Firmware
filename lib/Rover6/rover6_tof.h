#ifndef ROVER6_TOF
#define ROVER6_TOF

#include <Arduino.h>
#include <Adafruit_VL53L0X_Teensy.h>
#include "rover6_i2c.h"
#include "rover6_serial.h"
#include "rover6_general.h"
#include "rover6_motors.h"

/*
 * Adafruit TOF distance sensor
 * VL53L0X
 */

#define LOX1_ADDRESS 0x30
#define LOX2_ADDRESS 0x31

// shutdown pins
#define SHT_LOX1 7
#define SHT_LOX2 5

Adafruit_VL53L0X lox1;  // front
Adafruit_VL53L0X lox2;  // back
VL53L0X_RangingMeasurementData_t measure1;
VL53L0X_RangingMeasurementData_t measure2;

char* status_string = new char[0xff];

uint32_t lox_report_timer = 0;
#define LOX_SAMPLERATE_FAST_DELAY_MS 150
#define LOX_SAMPLERATE_SLOW_DELAY_MS 1000
unsigned int lox_samplerate_delay_ms = LOX_SAMPLERATE_FAST_DELAY_MS;

int LOX_FRONT_OBSTACLE_UPPER_THRESHOLD_MM = 0xffff;
int LOX_BACK_OBSTACLE_UPPER_THRESHOLD_MM = 0xffff;
int LOX_FRONT_OBSTACLE_LOWER_THRESHOLD_MM = 100;
int LOX_BACK_OBSTACLE_LOWER_THRESHOLD_MM = 100;

bool read_front_VL53L0X() {
    // lox1.rangingTest(&measure1, false); // pass in 'true' to get debug data printout!
    uint8_t pMeasurementDataReady = 0;
    VL53L0X_GetMeasurementDataReady(lox1.pMyDevice, &pMeasurementDataReady);
    return pMeasurementDataReady > 0;
}

bool read_back_VL53L0X() {
    // lox2.rangingTest(&measure2, false);
    uint8_t pMeasurementDataReady = 0;
    VL53L0X_GetMeasurementDataReady(lox2.pMyDevice, &pMeasurementDataReady);
    return pMeasurementDataReady > 0;
}


bool is_front_ok_VL53L0X() {
    if (lox1.Status != VL53L0X_ERROR_NONE) {
        VL53L0X_get_pal_error_string(lox1.Status, status_string);
        println_error("lox1 reported error %d: %s", lox1.Status, status_string);
        return false;
    }
    return true;
}


bool is_back_ok_VL53L0X() {
    if (lox2.Status != VL53L0X_ERROR_NONE) {
        VL53L0X_get_pal_error_string(lox1.Status, status_string);
        println_error("lox1 reported error %d: %s", lox2.Status, status_string);
        return false;
    }
    return true;
}

void setup_VL53L0X()
{
    pinMode(SHT_LOX1, OUTPUT);
    pinMode(SHT_LOX2, OUTPUT);

    println_info("Shutdown pins inited...");

    // all reset
    digitalWrite(SHT_LOX1, LOW);
    digitalWrite(SHT_LOX2, LOW);
    println_info("Both in reset mode...(pins are low)");
    delay(10);
    println_info("Starting...");

    // all unreset
    digitalWrite(SHT_LOX1, HIGH);
    digitalWrite(SHT_LOX2, HIGH);
    delay(10);

    // activating LOX1 and reseting LOX2
    digitalWrite(SHT_LOX1, HIGH);
    digitalWrite(SHT_LOX2, LOW);

    // initing LOX1
    if (!lox1.begin(LOX1_ADDRESS, false, &I2C_BUS_1)) {
        println_error("Failed to boot first VL53L0X");
    }
    delay(10);

    // activating LOX2
    digitalWrite(SHT_LOX2, HIGH);
    delay(10);

    //initing LOX2
    if (!lox2.begin(LOX2_ADDRESS, false, &I2C_BUS_1)) {
        println_error("Failed to boot second VL53L0X");
    }
    println_info("VL53L0X's initialized.");

    read_front_VL53L0X();
    read_back_VL53L0X();
    if (!is_front_ok_VL53L0X()) {
        println_error("lox1 failed first read!!");
    }
    if (!is_back_ok_VL53L0X()) {
        println_error("lox2 failed first read!!");
    }
}

void report_VL53L0X()
{
    if (!rover_state.is_reporting_enabled) {
        return;
    }
    print_data("lox", "ldddddd", CURRENT_TIME,
        measure1.RangeMilliMeter, measure2.RangeMilliMeter,
        measure1.RangeStatus, measure2.RangeStatus,
        lox1.Status, lox2.Status  // lookup table in vl53l0x_def.h line 133
    );
}

bool is_range_status_ok(uint8_t range_status) {
    return range_status < 4;
}

bool does_front_tof_see_obstacle() {
    if (!is_range_status_ok(measure1.RangeStatus)) {
        return true;
    }
    return (
        measure1.RangeMilliMeter < LOX_FRONT_OBSTACLE_LOWER_THRESHOLD_MM ||
        measure1.RangeMilliMeter > LOX_FRONT_OBSTACLE_UPPER_THRESHOLD_MM
    );
}

bool does_back_tof_see_obstacle() {
    if (!is_range_status_ok(measure2.RangeStatus)) {
        return true;
    }
    return (
        measure2.RangeMilliMeter < LOX_BACK_OBSTACLE_LOWER_THRESHOLD_MM ||
        measure2.RangeMilliMeter > LOX_BACK_OBSTACLE_UPPER_THRESHOLD_MM
    );
}

bool read_VL53L0X()
{
    // if (is_moving()) {
    //     lox_samplerate_delay_ms = LOX_SAMPLERATE_FAST_DELAY_MS;
    // }
    // else {
    //     lox_samplerate_delay_ms = LOX_SAMPLERATE_SLOW_DELAY_MS;
    // }
    // if (CURRENT_TIME - lox_report_timer < lox_samplerate_delay_ms) {
    //     return false;
    // }

    bool new_measurement = false;
    lox_report_timer = CURRENT_TIME;

    safety_struct.is_front_tof_ok = is_front_ok_VL53L0X();
    safety_struct.is_back_tof_ok = is_back_ok_VL53L0X();

    if (is_moving()) {
        if (is_moving_forward()) {
            if (read_front_VL53L0X()) new_measurement = true;
            safety_struct.is_front_tof_trig = does_front_tof_see_obstacle();
            safety_struct.is_back_tof_trig = false;
        }
        else {
            if (read_back_VL53L0X()) new_measurement = true;
            safety_struct.is_front_tof_trig = false;
            safety_struct.is_back_tof_trig = does_back_tof_see_obstacle();
        }
    }
    else {
        if (read_front_VL53L0X()) new_measurement = true;
        if (read_back_VL53L0X()) new_measurement = true;
        safety_struct.is_front_tof_trig = does_front_tof_see_obstacle();
        safety_struct.is_back_tof_trig = does_back_tof_see_obstacle();
    }

    if (safety_struct.is_back_tof_trig || safety_struct.is_front_tof_trig) {
        stop_motors();
    }
    return new_measurement;
}


#endif  // ROVER6_TOF
