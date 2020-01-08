
#include "Rover6.h"


/*
 * Constructor
 */
Rover6::Rover6()
{
    #ifdef ENABLE_SERVOS
    servos = new Adafruit_PWMServoDriver(0x40, &Wire1);
    servo_pulse_mins = new int[NUM_SERVOS];
    servo_pulse_maxs = new int[NUM_SERVOS];
    servo_positions = new double[NUM_SERVOS];
    for (size_t i = 0; i < NUM_SERVOS; i++) {
        servo_pulse_mins[i] = 150;
        servo_pulse_maxs[i] = 600;
        servo_positions[i] = 0.0;
    }
    #endif

    #ifdef ENABLE_INA
    ina219 = new Adafruit_INA219();
    ina219_shuntvoltage = 0.0;
    ina219_busvoltage = 0.0;
    ina219_current_mA = 0.0;
    ina219_loadvoltage = 0.0;
    ina219_power_mW = 0.0;
    #endif


    #ifdef ENABLE_MOTORS
    motorA = new TB6612(MOTORA_PWM, MOTORA_DR2, MOTORA_DR1);
    motorB = new TB6612(MOTORB_PWM, MOTORB_DR1, MOTORB_DR2);

    motors_on_standby = true;
    #endif

    #ifdef ENABLE_ENCODERS
    //encoder objects initialized in setup
    encA_pos = 0;
    encB_pos = 0;
    #endif

    #ifdef ENABLE_TOF
    lox1 = new Adafruit_VL53L0X();
    lox2 = new Adafruit_VL53L0X();
    measure1 = new VL53L0X_RangingMeasurementData_t();
    measure2 = new VL53L0X_RangingMeasurementData_t();
    #endif

    #ifdef ENABLE_TFT
    tft = new Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
    tft_brightness = 0;

    // scrolling_buffer = new String[TFT_SCROLLING_BUFFER_SIZE];
    // for (int i = 0; i < TFT_SCROLLING_BUFFER_SIZE; i++) {
    //     scrolling_buffer[i] = "";
    // }
    // scrolling_index = 0;
    #endif


    #ifdef ENABLE_BNO
    bno = new Adafruit_BNO055(-1, BNO055_ADDRESS_A, &Wire1);
    orientationData = new sensors_event_t();
    angVelocityData = new sensors_event_t();
    linearAccelData = new sensors_event_t();
    bno_board_temp = 0;
    #endif

    #ifdef ENABLE_IR
    irrecv = new IRrecv(IR_RECEIVER_PIN);
    irresults = new decode_results();
    ir_result_available = false;
    #endif

    is_idle = true;
    data_buffer = "";

    current_time = 0;
    i2c_report_timer = 0;
    fast_sensor_report_timer = 0;
    lox_report_timer = 0;
}

/*
 * Setup devices
 */
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
    setup_display();
    setup_BNO055();
    setup_IR();

    // flush serial buffer

    // setup_timers();

    set_idle(true);

    current_time = millis();
    i2c_report_timer = millis();
    fast_sensor_report_timer = millis();
}

/*
 * Toggle idle
 */
void Rover6::set_idle(bool state)
{
    if (state == is_idle) {
        return;
    }

    // println_display("Setting idle to: %d", is_idle);
    print_info("Setting idle to: %d", is_idle);

    is_idle = state;

    set_motorA(0);
    set_motorB(0);

    if (is_idle) {
        set_motor_standby(true);
        set_servo_standby(true);
        // set_display_brightness(0);
        // setup_timers();
        // interrupts();
    }
    else {
        set_motor_standby(false);
        set_servo_standby(false);
        reset_encoders();
        // set_display_brightness(255);

        current_time = millis();
        i2c_report_timer = millis();
        fast_sensor_report_timer = millis();
        // noInterrupts();
        // end_timers();
    }
}

/*
 * Serial communication
 */
void Rover6::write(String name, const char *formats, ...)
{
    va_list args;
    va_start(args, formats);
    String data = String(formats) + "\t";
    while (*formats != '\0') {
        if (*formats == 'd') {
            int i = va_arg(args, int);
            data += String(i);
        }
        else if (*formats == 'l') {
            int32_t s = va_arg(args, int32_t);
            data += String(s);
        }
        else if (*formats == 's') {
            char *s = va_arg(args, char*);
            data += s;
        }
        else if (*formats == 'f') {
            double f = va_arg(args, double);
            data += String(f);
        }
        data += "\t";
        ++formats;
    }
    va_end(args);
    data += PACKET_END;
    DATA_SERIAL.print(name + "\t" + data);
}

void Rover6::print_info(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    vsnprintf(SERIAL_MSG_BUFFER, SERIAL_MSG_BUFFER_SIZE, message, args);
    va_end(args);

    DATA_SERIAL.print("msg\tINFO\t");
    DATA_SERIAL.print(SERIAL_MSG_BUFFER);
    DATA_SERIAL.print('\n');
}

void Rover6::print_error(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    vsnprintf(SERIAL_MSG_BUFFER, SERIAL_MSG_BUFFER_SIZE, message, args);
    va_end(args);

    DATA_SERIAL.print("msg\tERROR\t");
    DATA_SERIAL.print(SERIAL_MSG_BUFFER);
    DATA_SERIAL.print('\n');
}

/*
 * External method. Check for data on serial
 */
void Rover6::check_serial()
{
    if (DATA_SERIAL.available()) {
        // String command = DATA_SERIAL.readStringUntil('\n');
        char command = DATA_SERIAL.read();

        // char first = command.charAt(0);
        if (command == '>') {
            set_idle(false);
        }
        else if (command == '<') {
            set_idle(true);
        }
        else if (command == '|') {
            _soft_restart();
        }
        else if (command == '?') {
            DATA_SERIAL.print("!\n");
        }
        else if (command == 'r') {
            report_status();
        }

        if (is_idle) {
            return;
        }

        switch (command) {
            case 'm':
                data_buffer = DATA_SERIAL.readStringUntil('\n');
                switch (data_buffer.charAt(0)) {
                    case 'a': set_motorA(data_buffer.substring(1).toInt()); break;
                    case 'b': set_motorB(data_buffer.substring(1).toInt()); break;
                    case 's': set_motor_standby((bool)(data_buffer.substring(1).toInt())); break;
                }
                break;
            case 's':
                data_buffer = DATA_SERIAL.readStringUntil('\n');
                if (data_buffer.charAt(0) == 't') {
                    // tell servo positions
                }
                else {
                    for (size_t i = 0; i < 1000; i++) {
                        set_servo_pwm(0, i);
                        set_servo_pwm(1, i);
                        delay(1);
                    }
                    // set_servo_pwm(
                    //     data_buffer.substring(0, 2).toInt(),
                    //     data_buffer.substring(2).toInt()
                    // );
                }
                break;

            // case 'd': display_image(); break;
            default: break;
        }
    }
}

/*
 * Poll sensors and send over serial
 */
void Rover6::report_data()
{
    if (is_idle) {
        read_IR();
        report_IR();

        delay(10);
        return;
    }

    current_time = millis();

    if (current_time - fast_sensor_report_timer > FAST_SAMPLERATE_DELAY_MS)
    {
        fast_sensor_report_timer = current_time;

        read_INA219();
        read_encoders();
        read_fsrs();
        read_IR();

        report_INA219();
        report_encoders();
        report_fsrs();
        report_IR();

    }

    if (current_time - i2c_report_timer > BNO055_SAMPLERATE_DELAY_MS)
    {
        display_sensors();
        i2c_report_timer = current_time;

        read_BNO055();
        // read_VL53L0X();

        report_BNO055();
        // report_VL53L0X();
    }

    if (current_time - lox_report_timer > LOX_SAMPLERATE_DELAY_MS)
    {
        lox_report_timer = current_time;

        read_VL53L0X();
        report_VL53L0X();
    }
}

/*
 * Return all device statuses when requested
 */
void Rover6::report_status()
{

}

/*
 * Serial
 */
void Rover6::setup_serial()
{
    // while (!Serial) {
    //     delay(1);
    // }
    // Serial.begin(115200);
    DATA_SERIAL.begin(500000);  // see https://www.pjrc.com/teensy/td_uart.html for UART info
    print_info("Rover #6");
    print_info("Serial buses initialized.");
}

/*
 * I2C
 */
void Rover6::setup_i2c()
{
    #if defined(ENABLE_BNO) || defined(ENABLE_TOF) || defined(ENABLE_SERVOS) || defined(ENABLE_INA)
    Wire.begin(I2C_MASTER, 0x00, I2C_PINS_18_19, I2C_PULLUP_EXT, 400000);
    Wire.setDefaultTimeout(200000); // 200ms
    Wire1.begin(I2C_MASTER, 0x00, I2C_PINS_37_38, I2C_PULLUP_EXT, 400000);
    Wire1.setDefaultTimeout(200000); // 200ms
    print_info("I2C initialized.");
    #endif  // enable i2c
}

/*
 * Adafruit PWM servo driver
 * PCA9685
 */
void Rover6::setup_servos()
{
    #ifdef ENABLE_SERVOS
    servos->begin();
    // servos->setPWMFreq(60);
    servos->setPWMFreq(50);
    delay(10);
    print_info("PCA9685 Servos initialized.");
    pinMode(SERVO_STBY, OUTPUT);
    #endif  // ENABLE_SERVOS
}

void Rover6::set_servo(uint8_t n, double angle)
{
    #ifdef ENABLE_SERVOS
    servo_positions[n] = angle;
    uint16_t pulse = (uint16_t)map(angle, 0, 180, servo_pulse_mins[n], servo_pulse_maxs[n]);
    set_servo_pwm(n, pulse);
    #endif  // ENABLE_SERVOS
}

void Rover6::set_servo_pwm(uint8_t n, uint16_t pulse)
{
    #ifdef ENABLE_SERVOS
    print_info("Setting servo %d pulse: %d", n, pulse);
    servos->setPWM(n, 0, pulse);
    #endif  // ENABLE_SERVOS
}

void Rover6::set_servo_standby(bool standby)
{
    #ifdef ENABLE_SERVOS
    if (standby) {  // set servos to low power
        digitalWrite(SERVO_STBY, HIGH);
    }
    else {  // bring servos out of standby mode
        digitalWrite(SERVO_STBY, LOW);
    }
    #endif  // ENABLE_SERVOS
}

/*
 * Adafruit High-side current and voltage meter
 * INA219
 */
void Rover6::setup_INA219()
{
    #ifdef ENABLE_INA
    ina219->begin(&Wire);
    print_info("INA219 initialized.");
    #endif  // ENABLE_INA
}

void Rover6::read_INA219()
{
    #ifdef ENABLE_INA
    ina219_shuntvoltage = ina219->getShuntVoltage_mV();
    ina219_busvoltage = ina219->getBusVoltage_V();
    ina219_current_mA = ina219->getCurrent_mA();
    ina219_power_mW = ina219->getPower_mW();
    ina219_loadvoltage = ina219_busvoltage + (ina219_shuntvoltage / 1000);
    #endif  // ENABLE_INA
}

void Rover6::report_INA219() {
    #ifdef ENABLE_INA
    write("ina", "lfff", millis(), ina219_current_mA, ina219_power_mW, ina219_loadvoltage);
    #endif  // ENABLE_INA
}

/*
 * Adafruit dual motor driver breakout + encoders
 * TB6612
 */
void Rover6::setup_motors()
{
    #ifdef ENABLE_MOTORS
    pinMode(MOTOR_STBY, OUTPUT);
    motorA->begin();
    motorB->begin();
    print_info("Motors initialized.");
    #endif  // ENABLE_MOTORS
}

void Rover6::set_motor_standby(bool standby)
{
    #ifdef ENABLE_MOTORS
    if (standby != motors_on_standby) {
        motors_on_standby = standby;
    }
    else {
        return;
    }
    if (standby) {  // set motors to low power
        digitalWrite(MOTOR_STBY, LOW);
    }
    else {  // bring motors out of standby mode
        digitalWrite(MOTOR_STBY, HIGH);
    }
    #endif  // ENABLE_MOTORS
}

void Rover6::set_motorA(int speed)
{
    #ifdef ENABLE_MOTORS
    motorA->setSpeed(speed);
    motorA_cmd = speed;
    #endif  // ENABLE_MOTORS
}
void Rover6::set_motorB(int speed)
{
    #ifdef ENABLE_MOTORS
    motorB->setSpeed(speed);
    motorB_cmd = speed;
    #endif  // ENABLE_MOTORS
}

void Rover6::drive_forward(int speed)
{
    #ifdef ENABLE_MOTORS
    set_motorA(-speed);
    set_motorB(-speed);
    delay(500);
    set_motorA(0);
    set_motorB(0);
    #endif  // ENABLE_MOTORS
}

void Rover6::rotate(int speed)
{
    #ifdef ENABLE_MOTORS
    set_motorA(speed);
    set_motorB(-speed);
    delay(250);
    set_motorA(0);
    set_motorB(0);
    #endif  // ENABLE_MOTORS
}

void Rover6::setup_encoders()
{
    #ifdef ENABLE_ENCODERS
    motorA_enc = new Encoder(MOTORA_ENCA, MOTORA_ENCB);
    motorB_enc = new Encoder(MOTORB_ENCA, MOTORB_ENCB);
    print_info("Encoders initialized.");
    #endif  // ENABLE_ENCODERS
}

void Rover6::read_encoders()
{
    #ifdef ENABLE_ENCODERS
    encA_pos = motorA_enc->read();
    encB_pos = motorB_enc->read();
    #endif  // ENABLE_ENCODERS
}

void Rover6::report_encoders() {
    #ifdef ENABLE_ENCODERS
    write("enc", "lll", millis(), encA_pos, encB_pos);
    #endif  // ENABLE_ENCODERS
}

void Rover6::reset_encoders()
{
    #ifdef ENABLE_ENCODERS
    encA_pos = 0;
    encB_pos = 0;
    motorA_enc->write(0);
    motorB_enc->write(0);
    #endif  // ENABLE_ENCODERS
}

/*
 * Adafruit TOF distance sensor
 * VL53L0X
 */
void Rover6::setup_VL53L0X()
{
    #ifdef ENABLE_TOF
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
        print_error("Failed to boot first VL53L0X");
        while(1);
    }
    delay(10);

    // activating LOX2
    digitalWrite(SHT_LOX2, HIGH);
    delay(10);

    //initing LOX2
    if(!lox2->begin(LOX2_ADDRESS, false, &Wire)) {
        print_error("Failed to boot second VL53L0X");
        while(1);
    }
    print_info("VL53L0X's initialized.");
    #endif  // ENABLE_TOF
}

void Rover6::read_VL53L0X()
{
    #ifdef ENABLE_TOF
    lox1->rangingTest(measure1, false); // pass in 'true' to get debug data printout!
    lox2->rangingTest(measure2, false); // pass in 'true' to get debug data printout!

    // lox1_out_of_range = measure1->RangeStatus == 4;  // if out of range
    // measure1.RangeMilliMeter
    #endif  // ENABLE_TOF
}

void Rover6::report_VL53L0X() {
    #ifdef ENABLE_TOF
    write("lox", "ldddd", millis(), measure1->RangeMilliMeter, measure2->RangeMilliMeter, measure1->RangeStatus, measure2->RangeStatus);
    #endif  // ENABLE_TOF
}

/*
 * Adafruit TFT 1.8" display
 * ST7735
 */

void Rover6::setup_display()
{
    #ifdef ENABLE_TFT
    pinMode(TFT_LITE, OUTPUT);
    tft->initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    delay(10);
    set_display_brightness(255);
    tft->fillScreen(ST77XX_BLACK);

    tft->setTextWrap(false);
    tft->setTextSize(1);
    tft->setRotation(1); // horizontal display
    tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);

    print_info("TFT display initialized.");

    #endif  // ENABLE_TFT
}

void Rover6::set_display_brightness(int brightness)
{
    #ifdef ENABLE_TFT
    analogWrite(TFT_LITE, brightness);
    tft_brightness = brightness;
    #endif  // ENABLE_TFT
}

void Rover6::print_display(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    vsnprintf(TFT_MSG_BUFFER, TFT_BUFFER_SIZE, message, args);
    va_end(args);

    tft->print(TFT_MSG_BUFFER);
}

void Rover6::display_sensors()
{
    //tft->fillScreen(ST77XX_BLACK);
    tft->setCursor(0, 0);
    tft->print(String(ina219_current_mA));
    tft->print("mA, ");
    tft->print(String(ina219_loadvoltage));
    tft->println("V         ");
    print_display("M: %d, %d, %d         \n\
E: %d, %d         \n\
D: %d, %d, %d, %d         \n\
F: %d, %d         \n",
        motorA_cmd, motorB_cmd, motors_on_standby,
        encA_pos, encB_pos,
        measure1->RangeMilliMeter, measure2->RangeMilliMeter, measure1->RangeStatus, measure2->RangeStatus,
        fsr_1_val, fsr_2_val
    );
}


/*
void Rover6::display_image()
{
    #ifdef ENABLE_TFT
    // Display is 128x64 16-bit color
    // String must be at least length 16384 (0x4000)

    // if (encoded_image.length() < 0x4000) {
    //     print_error("Invalid string length");
    //     print_error(encoded_image.length());
    //     return;
    // }
    print_info("Displaying image");

    bmpDraw();

    if (DATA_SERIAL.read() != '\n') {
        print_info("Image does not end with a newline!!");
    }
    #endif  // ENABLE_TFT
}


uint16_t Rover6::serial_read16()
{
    #ifdef ENABLE_TFT
    uint16_t result;
    ((uint8_t *)&result)[0] = DATA_SERIAL.read(); // LSB
    ((uint8_t *)&result)[1] = DATA_SERIAL.read(); // MSB
    return result;
    #else
    return 0;
    #endif  // ENABLE_TFT
}

uint32_t Rover6::serial_read32()
{
    #ifdef ENABLE_TFT
    uint32_t result;
    ((uint8_t *)&result)[0] = DATA_SERIAL.read(); // LSB
    ((uint8_t *)&result)[1] = DATA_SERIAL.read();
    ((uint8_t *)&result)[2] = DATA_SERIAL.read();
    ((uint8_t *)&result)[3] = DATA_SERIAL.read(); // MSB
    return result;
    #else
    return 0;
    #endif  // ENABLE_TFT
}

#define EXPECTED_WIDTH  128
#define EXPECTED_HEIGHT  160

void Rover6::bmpDraw()
{
    #ifdef ENABLE_TFT
    // uint8_t  x = 128;
    // uint8_t  y = 64;
    int      bmpWidth, bmpHeight;   // W+H in pixels
    uint32_t rowSize;               // Not always = bmpWidth; may have padding
    uint8_t  bmpDepth;              // Bit depth (currently must be 24)
    uint32_t bmpImageoffset;        // Start of image data in file
    uint8_t  sdbuffer[3 * EXPECTED_WIDTH * EXPECTED_HEIGHT + 0xff]; // pixel buffer (R+G+B per pixel) + extra
    uint32_t buffidx = 0;//sizeof(sdbuffer); // Current position in sdbuffer
    uint32_t image_size;
    boolean  flip    = true;        // BMP is stored bottom-to-top
    // boolean  goodBmp = false;       // Set to true on valid header parse
    int      row, col;
    uint8_t  r, g, b;
    uint32_t pos = 0;
    uint32_t startTime = micros();

    tft->fillScreen(ST77XX_BLACK);

    // Parse BMP header
    if (serial_read16() == 0x4D42)
    {
        Serial.print("File size: "); Serial.println(serial_read32());
        (void)serial_read32(); // Read & ignore creator bytes
        bmpImageoffset = serial_read32(); // Start of image data
        Serial.print("Image Offset: "); Serial.println(bmpImageoffset, DEC);

        // Read DIB header
        Serial.print("Header size: "); Serial.println(serial_read32());

        bmpWidth  = serial_read32();
        bmpHeight = serial_read32();
        if (serial_read16() == 1)  // if goodBmp
        {
            bmpDepth = serial_read16(); // bits per pixel
            Serial.print("Bit Depth: "); Serial.println(bmpDepth);

            if ((bmpDepth == 24) && (serial_read32() == 0)) { // 0 = uncompressed
                // goodBmp = true; // Supported BMP format -- proceed!
                Serial.print("Image size: ");
                Serial.print(bmpWidth);
                Serial.print('x');
                Serial.println(bmpHeight);

                // If bmpHeight is negative, image is in top-down order.
                // This is not canon but has been observed in the wild.
                if (bmpHeight < 0) {
                    bmpHeight = -bmpHeight;
                    flip      = false;
                }

                rowSize = (bmpWidth * 3 + 3) & ~3;
                // rowSize = bmpWidth * 3;

                // Set TFT address window to clipped image bounds
                tft->startWrite();
                tft->setAddrWindow(0, 0, bmpWidth, bmpHeight);

                if (!DATA_SERIAL.available()) {
                    print_error("ERROR: Ran out of data on serial before image finished drawing!");
                    return;
                }

                image_size = bmpHeight * bmpWidth * 3;
                if (sizeof(sdbuffer) < image_size) {
                    print_error("ERROR: Buffer is insufficiently sized for image!");
                    return;
                }
                // DATA_SERIAL.readBytes(sdbuffer, bmpImageoffset);
                DATA_SERIAL.readBytes(sdbuffer, image_size);

                bmpImageoffset = 0;
                for (row = 0; row < bmpHeight; row++) { // For each scanline...
                    if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
                        pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
                    else     // Bitmap is stored top-to-bottom
                        pos = bmpImageoffset + row * rowSize;

                    if (pos != buffidx) {
                        buffidx = pos;
                    }
                    for (col = 0; col < bmpWidth; col++) { // For each pixel...
                        // Convert pixel from BMP to TFT format, push to display
                        b = sdbuffer[buffidx++];
                        g = sdbuffer[buffidx++];
                        r = sdbuffer[buffidx++];
                        tft->pushColor(tft->color565(r, g, b));
                        // tft->drawPixel(col, row, tft->color565(r,g,b));

                    } // end pixel
                } // end scanline
                tft->endWrite();
                Serial.print("Loaded in ");
                Serial.print(micros() - startTime);
                Serial.println(" us");
            } // end goodBmp
        }
    }
    #endif  // ENABLE_TFT
}
*/

/*
 * Adafruit FSR
 * Interlink 402
 */
void Rover6::setup_fsrs()
{
    #ifdef ENABLE_FSR
    pinMode(FSR_PIN_1, INPUT);
    pinMode(FSR_PIN_2, INPUT);
    print_info("FSRs initialized.");
    #endif  // ENABLE_FSR
}

void Rover6::read_fsrs()
{
    #ifdef ENABLE_FSR
    fsr_1_val = analogRead(FSR_PIN_1);
    fsr_2_val = analogRead(FSR_PIN_2);
    #endif  // ENABLE_FSR
}

void Rover6::report_fsrs() {
    #ifdef ENABLE_FSR
    write("fsr", "ldd", millis(), fsr_1_val, fsr_2_val);
    #endif  // ENABLE_FSR
}

/*
 * Adafruit 9-DOF Absolute Orientation IMU
 * BNO055
 */
void Rover6::setup_BNO055()
{
    #ifdef ENABLE_BNO
    if (!bno->begin())
    {
        /* There was a problem detecting the BNO055 ... check your connections */
        print_error("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
        while (1);
    }
    print_info("BNO055 initialized.");
    delay(500);
    #endif  // ENABLE_BNO
}

void Rover6::read_BNO055()
{
    #ifdef ENABLE_BNO
    //could add VECTOR_ACCELEROMETER, VECTOR_MAGNETOMETER,VECTOR_GRAVITY...
    bno->getEvent(orientationData, Adafruit_BNO055::VECTOR_EULER);
    bno->getEvent(angVelocityData, Adafruit_BNO055::VECTOR_GYROSCOPE);
    bno->getEvent(linearAccelData, Adafruit_BNO055::VECTOR_LINEARACCEL);
    #endif  // ENABLE_BNO
}

void Rover6::report_BNO055()
{
    #ifdef ENABLE_BNO
    write(
        "bno", "lfffffffff",
        millis(),
        orientationData->orientation.x,
        orientationData->orientation.y,
        orientationData->orientation.z,
        angVelocityData->gyro.x,
        angVelocityData->gyro.y,
        angVelocityData->gyro.z,
        linearAccelData->acceleration.x,
        linearAccelData->acceleration.y,
        linearAccelData->acceleration.z
    );
    #endif  // ENABLE_BNO
}

/*
 * IR remote receiver
 */
void Rover6::setup_IR()
{
    #ifdef ENABLE_IR
    irrecv->enableIRIn();
    irrecv->blink13(false);
    print_info("IR initialized.");
    #endif  // ENABLE_IR
}

void Rover6::read_IR()
{
    #ifdef ENABLE_IR
    if (irrecv->decode(irresults)) {
        ir_result_available = true;
        ir_type = irresults->decode_type;
        ir_value = irresults->value;
        irrecv->resume(); // Receive the next value
    }
    #endif  // ENABLE_IR
}


void Rover6::report_IR()
{
    #ifdef ENABLE_IR
    if (!ir_result_available) {
        return;
    }

    if (ir_type == NEC && ir_value != 0xffff) {  // 0xffff means repeat last command
        write("irr", "ldd", millis(), ir_type, ir_value);
    }

    switch (ir_value) {
        case 0x00ff: print_info("IR: VOL-"); break;  // VOL-
        case 0x807f: print_info("IR: Play/Pause"); set_idle(!is_idle); break;  // Play/Pause
        case 0x40bf: print_info("IR: VOL+"); break;  // VOL+
        case 0x20df: print_info("IR: SETUP"); break;  // SETUP
        case 0xa05f: print_info("IR: ^"); drive_forward(255); break;  // ^
        case 0x609f: print_info("IR: MODE"); break;  // MODE
        case 0x10ef: print_info("IR: <");  rotate(255); break;  // <
        case 0x906f: print_info("IR: ENTER"); set_motorA(0); set_motorB(0); break;  // ENTER
        case 0x50af: print_info("IR: >");  rotate(-255); break;  // >
        case 0x30cf: print_info("IR: 0 10+"); break;  // 0 10+
        case 0xb04f: print_info("IR: v");  drive_forward(-255); break;  // v
        case 0x708f: print_info("IR: Del"); break;  // Del
        case 0x08f7: print_info("IR: 1"); set_servo(0, (servo_positions[0] == 0.0 ? 90.0 : 0.0)); break;  // 1
        case 0x8877: print_info("IR: 2"); set_servo(1, (servo_positions[1] == 0.0 ? 180.0 : 0.0)); break;  // 2
        case 0x48B7: print_info("IR: 3"); break;  // 3
        case 0x28D7: print_info("IR: 4"); break;  // 4
        case 0xA857: print_info("IR: 5"); break;  // 5
        case 0x6897: print_info("IR: 6"); break;  // 6
        case 0x18E7: print_info("IR: 7"); break;  // 7
        case 0x9867: print_info("IR: 8"); break;  // 8
        case 0x58A7: print_info("IR: 9"); break;  // 9

    }
    // String decode_type;
    // if (irresults->decode_type == NEC) {
    //     decode_type = "NEC";
    // } else if (irresults->decode_type == SONY) {
    //     decode_type = "SONY";
    // } else if (irresults->decode_type == RC5) {
    //     decode_type = "RC5";
    // } else if (irresults->decode_type == RC6) {
    //     decode_type = "RC6";
    // } else if (irresults->decode_type == UNKNOWN) {
    //     decode_type = "???";
    // }

    ir_result_available = false;
    ir_type = 0;
    ir_value = 0;
    #endif  // ENABLE_IR
}
