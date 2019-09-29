
#include "Rover6.h"

Rover6::Rover6()
{
    servos = new Adafruit_PWMServoDriver(0x40, &Wire1);
    servo_pulse_mins = new int[NUM_SERVOS];
    servo_pulse_maxs = new int[NUM_SERVOS];
    for (size_t i = 0; i < NUM_SERVOS; i++) {
        servo_pulse_mins[i] = 150;
    }
    for (size_t i = 0; i < NUM_SERVOS; i++) {
        servo_pulse_maxs[i] = 600;
    }

    ina219 = new Adafruit_INA219();
    ina219_shuntvoltage = 0.0;
    ina219_busvoltage = 0.0;
    ina219_current_mA = 0.0;
    ina219_loadvoltage = 0.0;
    ina219_power_mW = 0.0;

    motorA = new TB6612(MOTORA_PWM, MOTORA_DR1, MOTORA_DR2);
    motorB = new TB6612(MOTORB_PWM, MOTORB_DR1, MOTORB_DR2);
    //encoder objects initialized in setup
    encA_pos = 0;
    encB_pos = 0;

    lox1 = new Adafruit_VL53L0X();
    lox2 = new Adafruit_VL53L0X();
    measure1 = new VL53L0X_RangingMeasurementData_t();
    measure2 = new VL53L0X_RangingMeasurementData_t();
    lox_timer = new IntervalTimer();
    lox1_dist_mm = 0;
    lox2_dist_mm = 0;

    tft = new Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

    bno = new Adafruit_BNO055(-1, BNO055_ADDRESS_A, &Wire1);
    bno_board_temp = 0;
    bno055_data = new float[BNO055_DATA_BUF_LEN];
    for (size_t i = 0; i < BNO055_DATA_BUF_LEN; i++) {
        bno055_data[i] = 0.0;
    }

    is_idle = true;
}

void Rover6::begin()
{
    setup_serial();
    setup_i2c();
    setup_servos();
    setup_INA219();
    setup_motors();
    setup_encoders();
    setup_VL53L0X();
    setup_fsrs();
    initialize_display();
    setup_BNO055();
    // setup_timers();

    set_idle(true);
}

void Rover6::setup_timers()
{
    lox_timer->begin(Rover6::read_VL53L0X, VL53L0X_SAMPLERATE_DELAY_US);
}

void Rover6::end_timers()
{
    lox_timer->end();
}

void Rover6::set_idle(bool state)
{
    if (state == is_idle) {
        return;
    }
    is_idle = state;
    if (is_idle) {
        set_motor_standby(true);
        set_display_brightness(0);
        // setup_timers();
        // interrupts();
    }
    else {
        set_motor_standby(false);
        set_display_brightness(255);
        // noInterrupts();
        // end_timers();
    }
}

void Rover6::check_serial()
{
    if (DATA_SERIAL.available()) {
        String command = DATA_SERIAL.readStringUntil('\n');

        char first = command.charAt(0);
        if (first == '>') {
            set_idle(false);
        }
        else if (first == '<') {
            set_idle(true);
        }
        else if (first == '?') {
            DATA_SERIAL.print("!\n");
        }
        else if (first == 'r') {
            DATA_SERIAL.println("Reporting");
            report_status();
        }

        if (is_idle) {
            return;
        }

        switch (first) {
            case 'm':
                switch (command.charAt(1)) {
                    case 'a': set_motorA(command.substring(2).toInt()); break;
                    case 'b': set_motorB(command.substring(2).toInt()); break;
                    case 's': set_motor_standby((bool)(command.substring(2).toInt())); break;
                }
                break;
            case 's':
                set_servo(
                    command.substring(2, 3).toInt(),
                    command.substring(3).toInt()
                );
                break;
        }
    }
}

void Rover6::report_data()
{
    if (is_idle) {
        return;
    }

    // read_BNO055();
    read_INA219();
    read_encoders();
    read_fsrs();


    bno055_data[0] = orientationData->orientation.x;
    bno055_data[1] = orientationData->orientation.y;
    bno055_data[2] = orientationData->orientation.z;

    bno055_data[3] = angVelocityData->gyro.x;
    bno055_data[4] = angVelocityData->gyro.y;
    bno055_data[5] = angVelocityData->gyro.z;

    bno055_data[6] = linearAccelData->acceleration.x;
    bno055_data[7] = linearAccelData->acceleration.y;
    bno055_data[8] = linearAccelData->acceleration.z;

    DATA_SERIAL.print("bno\t");
    for (size_t i = 0; i < BNO055_DATA_BUF_LEN; i++) {
        DATA_SERIAL.print(i);
        DATA_SERIAL.print(':');
        DATA_SERIAL.print(bno055_data[i]);
        DATA_SERIAL.print('\t');
    }

    lox1_dist_mm = measure1->RangeMilliMeter;
    lox2_dist_mm = measure2->RangeMilliMeter;

    DATA_SERIAL.print("\nlox\t0:");
    DATA_SERIAL.print(lox1_dist_mm);
    DATA_SERIAL.print("\t1:");
    DATA_SERIAL.print(lox2_dist_mm);

    DATA_SERIAL.print("\nina\t0:");
    DATA_SERIAL.print(ina219_current_mA);
    DATA_SERIAL.print("\t1:");
    DATA_SERIAL.print(ina219_power_mW);
    DATA_SERIAL.print("\t2:");
    DATA_SERIAL.print(ina219_loadvoltage);

    DATA_SERIAL.print("\nenc\t0:");
    DATA_SERIAL.print(encA_pos);
    DATA_SERIAL.print("\t1:");
    DATA_SERIAL.print(encB_pos);

    DATA_SERIAL.print("\n>fsr\t0:");
    DATA_SERIAL.print(fsr_1_val);
    DATA_SERIAL.print("\t1:");
    DATA_SERIAL.print(fsr_2_val);
    DATA_SERIAL.print('\n');
}

void Rover6::report_status()
{
    print_info("VL53L0X report");

    uint8_t system, gyro, accel, mag = 0;
    bno->getCalibration(&system, &gyro, &accel, &mag);

    print_info("BNO055 report:");
    MSG_SERIAL.print("TEMP: ");
    MSG_SERIAL.print(this->bno->getTemp());

    MSG_SERIAL.print("\tCALIBRATION: Sys=");
    MSG_SERIAL.print(system, DEC);
    MSG_SERIAL.print(" Gyro=");
    MSG_SERIAL.print(gyro, DEC);
    MSG_SERIAL.print(" Accel=");
    MSG_SERIAL.print(accel, DEC);
    MSG_SERIAL.print(" Mag=");
    MSG_SERIAL.println(mag, DEC);

    // print last distance measurement with all fields
    // print bno status
    // print current actuator commands
}


void Rover6::setup_serial()
{
    MSG_SERIAL.begin(115200);
    while (!MSG_SERIAL) {
        delay(1);
    }

    DATA_SERIAL.begin(115200);  // see https://www.pjrc.com/teensy/td_uart.html for UART info
    // DATA_SERIAL.begin(500000);  // see https://www.pjrc.com/teensy/td_uart.html for UART info
    print_info("Rover #6");
    print_info("Serial buses initialized.");
}

void Rover6::setup_i2c()
{
    Wire.begin(I2C_MASTER, 0x00, I2C_PINS_18_19, I2C_PULLUP_EXT, 400000);
    Wire.setDefaultTimeout(200000); // 200ms
    Wire1.begin(I2C_MASTER, 0x00, I2C_PINS_37_38, I2C_PULLUP_EXT, 400000);
    Wire1.setDefaultTimeout(200000); // 200ms
    print_info("I2C initialized.");
}

void Rover6::setup_servos()
{
    servos->begin();
    servos->setPWMFreq(60);
    delay(10);
    print_info("PCA9685 Servos initialized.");
}

void Rover6::set_servo(uint8_t n, double angle) {
    uint16_t pulse = (uint16_t)map(angle, 0, 180, servo_pulse_mins[n], servo_pulse_maxs[n]);
    servos->setPWM(n, 0, pulse);
}

void Rover6::setup_INA219()
{
    ina219->begin(&Wire);
    print_info("INA219 initialized.");
}

void Rover6::read_INA219()
{
    ina219_shuntvoltage = ina219->getShuntVoltage_mV();
    ina219_busvoltage = ina219->getBusVoltage_V();
    ina219_current_mA = ina219->getCurrent_mA();
    ina219_power_mW = ina219->getPower_mW();
    ina219_loadvoltage = ina219_busvoltage + (ina219_shuntvoltage / 1000);
}

void Rover6::setup_motors()
{
    pinMode(MOTOR_STBY, OUTPUT);
    motorA->begin();
    motorB->begin();
    print_info("Motors initialized.");
}

void Rover6::set_motor_standby(bool standby)
{
    if (standby) {  // set motors to low power
        digitalWrite(MOTOR_STBY, LOW);
    }
    else {  // bring motors out of standby mode
        digitalWrite(MOTOR_STBY, HIGH);
    }
}

void Rover6::setup_encoders()
{
    motorA_enc = new Encoder(MOTORA_ENCA, MOTORA_ENCB);
    motorB_enc = new Encoder(MOTORB_ENCA, MOTORB_ENCB);
    print_info("Encoders initialized.");
}

void Rover6::read_encoders()
{
    encA_pos = motorA_enc->read();
    encB_pos = motorB_enc->read();
}

void Rover6::setup_VL53L0X()
{
    pinMode(SHT_LOX1, OUTPUT);
    pinMode(SHT_LOX2, OUTPUT);

    print_info("Shutdown pins inited...");

    // all reset
    digitalWrite(SHT_LOX1, LOW);
    digitalWrite(SHT_LOX2, LOW);
    print_info("Both in reset mode...(pins are low)");
    delay(10);
    print_info("Starting...");

    // all unreset
    digitalWrite(SHT_LOX1, HIGH);
    digitalWrite(SHT_LOX2, HIGH);
    delay(10);

    // activating LOX1 and reseting LOX2
    digitalWrite(SHT_LOX1, HIGH);
    digitalWrite(SHT_LOX2, LOW);

    // initing LOX1
    if(!lox1->begin(LOX1_ADDRESS, false, &Wire)) {
        print_error(F("Failed to boot first VL53L0X"));
        while(1);
    }
    delay(10);

    // activating LOX2
    digitalWrite(SHT_LOX2, HIGH);
    delay(10);

    //initing LOX2
    if(!lox2->begin(LOX2_ADDRESS, false, &Wire)) {
        print_error(F("Failed to boot second VL53L0X"));
        while(1);
    }
    print_info("VL53L0X's initialized.");
}
void Rover6::read_VL53L0X()
{
    Rover6::self->lox1->rangingTest(Rover6::self->measure1, false); // pass in 'true' to get debug data printout!
    Rover6::self->lox2->rangingTest(Rover6::self->measure2, false); // pass in 'true' to get debug data printout!

    // lox1_out_of_range = measure1->RangeStatus == 4;  // if out of range
    // measure1.RangeMilliMeter
}

void Rover6::setup_fsrs()
{
    pinMode(FSR_PIN_1, INPUT);
    pinMode(FSR_PIN_2, INPUT);
    print_info("FSRs initialized.");
}

void Rover6::read_fsrs()
{
    fsr_1_val = analogRead(FSR_PIN_1);
    fsr_2_val = analogRead(FSR_PIN_2);
}

void Rover6::initialize_display()
{
    pinMode(TFT_LITE, OUTPUT);
    tft->initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    delay(10);
    set_display_brightness(255);
    tft->fillScreen(ST77XX_BLACK);
    print_info("TFT display initialized.");
}

void Rover6::set_display_brightness(int brightness)
{
    analogWrite(TFT_LITE, brightness);
}

void Rover6::setup_BNO055()
{
    if (!bno->begin())
    {
        /* There was a problem detecting the BNO055 ... check your connections */
        print_error("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
        while (1);
    }
    print_info("BNO055 initialized.");
    delay(500);
}

void Rover6::read_BNO055()
{
    //could add VECTOR_ACCELEROMETER, VECTOR_MAGNETOMETER,VECTOR_GRAVITY...
    bno->getEvent(orientationData, Adafruit_BNO055::VECTOR_EULER);
    bno->getEvent(angVelocityData, Adafruit_BNO055::VECTOR_GYROSCOPE);
    bno->getEvent(linearAccelData, Adafruit_BNO055::VECTOR_LINEARACCEL);
}
