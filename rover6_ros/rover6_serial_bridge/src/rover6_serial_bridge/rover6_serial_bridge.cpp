#include <rover6_serial_bridge/rover6_serial_bridge.h>


Rover6SerialBridge::Rover6SerialBridge(ros::NodeHandle* nodehandle):nh(*nodehandle)
{
    nh.param<string>("serial_port", _serialPort, "/dev/serial0");
    nh.param<int>("serial_baud", _serialBaud, 115200);
    nh.param<string>("imu_frame_id", _imuFrameID, "bno055_imu");
    nh.param<string>("enc_frame_id", _encFrameID, "encoders");
    nh.param<double>("wheel_radius_cm", _wheelRadiusCm, 32.5);
    nh.param<double>("ticks_per_rotation", _ticksPerRotation, 38400.0);
    nh.param<double>("max_rpm", _maxRPM, 915.0);
    _cmPerTick = 2.0 * M_PI * _wheelRadiusCm / _ticksPerRotation;
    _cpsToCmd = 255.0 / _maxRPM;

    imu_msg.header.frame_id = _imuFrameID;
    enc_msg.header.frame_id = _encFrameID;

    _serialBuffer = "";
    _serialBufferIndex = 0;
    _currentBufferSegment = "";
    _readPacketNum = 0;
    _writePacketNum = 0;

    _dateString = new char[16];

    readyState = new StructReadyState;
    readyState->rover_name = "";
    readyState->is_ready = false;
    readyState->time_ms = 0;
     
    deviceStartTime = ros::Time::now();
    offsetTimeMs = 0;

    imu_pub = nh.advertise<sensor_msgs::Imu>("/bno055", 100);
    enc_pub = nh.advertise<rover6_serial_bridge::Rover6Encoder>("/encoders", 100);

    ROS_INFO("Rover 6 serial bridge init done");
}


void Rover6SerialBridge::configure()
{
    ROS_INFO("Configuring serial device.");
    // attempt to open the serial port
    try
    {
        _serialRef.setPort(_serialPort);
        _serialRef.setBaudrate(_serialBaud);
        serial::Timeout timeout = serial::Timeout::simpleTimeout(1000);
        _serialRef.setTimeout(timeout);
        _serialRef.open();
        ROS_INFO("Serial device configured.");
    }
    catch (serial::IOException e)
    {
        ROS_ERROR_STREAM("Unable to open port: " << _serialPort);
        ROS_ERROR_STREAM("Serial exception: " << e.what());
        cerr << "Serial exception: " << e.what() << endl;
        throw;
    }
}

void Rover6SerialBridge::setStartTime(uint32_t time_ms) {
    deviceStartTime = ros::Time::now();
    offsetTimeMs = time_ms;
}

ros::Time Rover6SerialBridge::getDeviceTime(uint32_t time_ms) {
    return deviceStartTime + ros::Duration((double)(time_ms - offsetTimeMs) / 1000.0);
}

void Rover6SerialBridge::checkReady()
{
    ROS_INFO("Checking if the serial device is ready.");

    ros::Time begin_time = ros::Time::now();
    ros::Time write_time = ros::Time::now();
    ros::Duration general_timeout = ros::Duration(5.0);
    ros::Duration write_timeout = ros::Duration(1.0);
    
    writeSerial("?", "s", "rover6");

    while (!readyState->is_ready)
    {
        if (!ros::ok()) {
            break;
        }
        if ((ros::Time::now() - begin_time) > general_timeout) {
            throw ReadyTimeoutException;
        }
        if ((ros::Time::now() - write_time) > write_timeout) {
            writeSerial("?", "s", "rover6");
            write_time = ros::Time::now();
        }
        if (_serialRef.available() > 2) {
            readSerial();
        }
    }

    if (readyState->is_ready) {
        setStartTime(readyState->time_ms);
        ROS_INFO_STREAM("Serial device is ready. Rover name is " << readyState->rover_name);
    }
    else {
        ROS_ERROR("Failed to receive ready signal!");
    }
}

void Rover6SerialBridge::waitForPacketStart()
{
    stringstream msg_buffer;
    while (true) {
        if (_serialRef.available() < 2) {
            continue;
        }
        char c1 = _serialRef.read(1).at(0);
        if (c1 == PACKET_START_0) {
            char c2 = _serialRef.read(1).at(0);
            if (c2 == PACKET_START_1) {
                return;
            }
        }

        else if (c1 == PACKET_STOP) {
            ROS_INFO_STREAM("Device message: " << msg_buffer.str());
            msg_buffer.str(std::string());
        }
        else {
            msg_buffer << c1;
        }
    }
}

bool Rover6SerialBridge::readSerial()
{
    waitForPacketStart();
    _serialBuffer = _serialRef.readline();
    _serialBuffer = _serialBuffer.substr(0, _serialBuffer.length() - 1);  // remove newline character
    // at least 1 char for packet num
    // \t + at least 1 category char
    // 2 chars for checksum
    if (_serialBuffer.length() < 5) {
        ROS_ERROR_STREAM("Received packet has an invalid number of characters! " << _serialBuffer);
        _readPacketNum++;
        return false;
    }

    _serialBufferIndex = 0;
    uint8_t calc_checksum = 0;
    // compute checksum using all characters except the checksum itself
    for (size_t index = 0; index < _serialBuffer.length() - 2; index++) {
        calc_checksum += (uint8_t)_serialBuffer.at(index);
    }

    uint16_t recv_checksum = std::stoul(_serialBuffer.substr(_serialBuffer.length() - 2), nullptr, 16);

    if (calc_checksum != recv_checksum) {
        // checksum failed
        ROS_ERROR("Checksum failed! recv %d != calc %d", calc_checksum, recv_checksum);
        ROS_ERROR_STREAM("Buffer: " << _serialBuffer);
        _readPacketNum++;
        return false;
    }

    // get packet num segment
    if (!getNextSegment()) {
        ROS_ERROR("Failed to find packet number segment! %s", _serialBuffer);
        _readPacketNum++;
        return false;
    }
    uint32_t recv_packet_num = (uint32_t)stoi(_currentBufferSegment);
    if (recv_packet_num != _readPacketNum) {
        ROS_ERROR("Received packet num doesn't match local count. recv %d != local %d", recv_packet_num, _readPacketNum);
        ROS_ERROR_STREAM("Buffer: " << _serialBuffer);
        _readPacketNum = recv_packet_num;
    }

    // find category segment
    if (!getNextSegment()) {
        ROS_ERROR_STREAM("Failed to find category segment! Buffer: " << _serialBuffer);
        _readPacketNum++;
        return false;
    }

    string category = _currentBufferSegment;
    // ROS_INFO_STREAM("category: " << category);

    // remove checksum
    _serialBuffer = _serialBuffer.substr(0, _serialBuffer.length() - 2);

    processSerialPacket(category);

    _readPacketNum++;
    return true;
}

bool Rover6SerialBridge::getNextSegment()
{
    if (_serialBufferIndex >= _serialBuffer.length()) {
        return false;
    }
    size_t separator = _serialBuffer.find('\t', _serialBufferIndex);
    if (separator == std::string::npos) {
        _currentBufferSegment = _serialBuffer.substr(_serialBufferIndex);
        _serialBufferIndex = _serialBuffer.length();
        return true;
    }
    else {
        _currentBufferSegment = _serialBuffer.substr(_serialBufferIndex, separator - _serialBufferIndex);
        _serialBufferIndex = separator + 1;
        return true;
    }
}


void Rover6SerialBridge::processSerialPacket(string category)
{
    if (category.compare("txrx") == 0) {
        CHECK_SEGMENT(0);
        unsigned long long packet_num = (unsigned long long)stol(_currentBufferSegment);
        CHECK_SEGMENT(1);
        bool success = (bool)stoi(_currentBufferSegment);

        if (!success) {
            ROS_ERROR("Device failed to parse sent packet number '%llu'", packet_num);
        }
    }
    else if (category.compare("bno") == 0) {
        parseImu();
    }
    else if (category.compare("enc") == 0) {
        parseEncoder();
    }
    else if (category.compare("fsr") == 0) {

    }
    else if (category.compare("safe") == 0) {

    }
    else if (category.compare("ina") == 0) {

    }
    else if (category.compare("ir") == 0) {

    }
    else if (category.compare("servo") == 0) {

    }
    else if (category.compare("lox") == 0) {

    }
    else if (category.compare("ready") == 0) {
        if (!getNextSegment()) {  ROS_ERROR_STREAM("Failed to parse first segment in 'ready': " << _serialBuffer);  return;  }
        readyState->time_ms = (uint32_t)stoi(_currentBufferSegment);
        if (!getNextSegment()) {  ROS_ERROR_STREAM("Failed to parse second segment in 'ready': " << _serialBuffer);  return;  }
        readyState->rover_name = _currentBufferSegment;
        readyState->is_ready = true;
    }
}


void Rover6SerialBridge::writeSerial(string name, const char *formats, ...)
{
    va_list args;
    va_start(args, formats);
    string packet;
    stringstream sstream;
    sstream << PACKET_START_0 << PACKET_START_1 << _writePacketNum << "\t" << name;

    while (*formats != '\0') {
        sstream << "\t";
        if (*formats == 'd') {
            int i = va_arg(args, int32_t);
            sstream << i;
        }
        else if (*formats == 'u') {
            uint32_t u = va_arg(args, uint32_t);
            sstream << u;
        }
        else if (*formats == 's') {
            char *s = va_arg(args, char*);
            sstream << s;
        }
        else if (*formats == 'f') {
            double f = va_arg(args, double);
            sstream << (float)f;
        }
        else {
            ROS_ERROR("Invalid format %c", *formats);
        }
        ++formats;
    }
    va_end(args);

    packet = sstream.str();

    uint8_t calc_checksum = 0;
    for (size_t index = 2; index < packet.length(); index++) {
        calc_checksum += (uint8_t)packet.at(index);
    }
    ROS_INFO("calc_checksum: %d", calc_checksum);

    if (calc_checksum < 0x10) {
        sstream << "0";
    }
    // can't pass uint8_t to std::hex, has to be int
    sstream << std::hex << (int)calc_checksum;

    sstream << PACKET_STOP;

    packet = sstream.str();    
  
    // checksum might be inserting null characters. Force the buffer to extend
    // to include packet stop and checksum

    ROS_INFO_STREAM("Writing: " << packet);
    _serialRef.write(packet);
    _writePacketNum++;
}

void Rover6SerialBridge::setup()
{
    configure();

    // wait for startup messages from the microcontroller
    checkReady();

    // tell the microcontroller to start
    setActive(true);
    setReporting(true);
}


void Rover6SerialBridge::loop()
{
    // if the serial buffer has data, parse it
    if (_serialRef.available() > 2) {
        readSerial();
    }

    ROS_INFO_THROTTLE(15, "%llu packets received", _readPacketNum);
}

void Rover6SerialBridge::stop()
{
    setActive(false);
    setReporting(false);
    _serialRef.close();
}


int Rover6SerialBridge::run()
{
    setup();
    
    ros::Rate clock_rate(120);  // run loop at 120 Hz

    int exit_code = 0;
    while (ros::ok())
    {
        // let ROS process any events
        ros::spinOnce();
        clock_rate.sleep();

        try {
            loop();
        }
        catch (exception& e) {
            ROS_ERROR_STREAM("Exception in main loop: " << e.what());
            exit_code = 1;
            break;
        }
    }
    stop();

    return exit_code;
}

void Rover6SerialBridge::setActive(bool state)
{
    if (state) {
        writeSerial("<>", "d", 1);
    }
    else {
        writeSerial("<>", "d", 0);
    }
}

void Rover6SerialBridge::softRestart() {
    writeSerial("<>", "d", 2);
}

void Rover6SerialBridge::setReporting(bool state)
{
    if (state) {
        writeSerial("[]", "d", 1);
    }
    else {
        writeSerial("[]", "d", 0);
    }
}

void Rover6SerialBridge::resetSensors() {
    writeSerial("[]", "d", 2);
}

void Rover6SerialBridge::writeTimeStr()
{
    time_t curr_time;
	tm* curr_tm;
    time(&curr_time);
	curr_tm = localtime(&curr_time);
    strftime(_dateString, 50, "%I:%M:%S%p", curr_tm);
    writeSerial("t", "s", _dateString);

}

void Rover6SerialBridge::writeSpeed(float speedA, float speedB) {
    writeSerial("m", "ff", speedA, speedB);
}

void Rover6SerialBridge::writeK(float kp_A, float ki_A, float kd_A, float kp_B, float ki_B, float kd_B) {
    writeSerial("ks", "fffff", kp_A, ki_A, kd_A, kp_B, ki_B, kd_B);
}

void Rover6SerialBridge::writeServo(int n){
    writeSerial("s", "d", n);
}

void Rover6SerialBridge::writeServo(int n, int command){
    writeSerial("s", "dd", n, command);
}

void Rover6SerialBridge::writeObstacleThresholds(int back_lower, int back_upper, int front_lower, int front_upper) {
    writeSerial("safe", "dddd", back_lower, back_upper, front_lower, front_upper);
}

void Rover6SerialBridge::setSafetyThresholds(double obstacle_threshold_x_mm, double ledge_threshold_y_mm, double buffer_x_mm)
{

}


void Rover6SerialBridge::parseImu()
{
    double roll, pitch, yaw;
    CHECK_SEGMENT(0); imu_msg.header.stamp = getDeviceTime((uint32_t)stoi(_currentBufferSegment));
    CHECK_SEGMENT(1); yaw = stof(_currentBufferSegment);
    CHECK_SEGMENT(2); pitch = stof(_currentBufferSegment);
    CHECK_SEGMENT(3); roll = stof(_currentBufferSegment);
    CHECK_SEGMENT(4); imu_msg.angular_velocity.x = stof(_currentBufferSegment);
    CHECK_SEGMENT(5); imu_msg.angular_velocity.y = stof(_currentBufferSegment);
    CHECK_SEGMENT(6); imu_msg.angular_velocity.z = stof(_currentBufferSegment);
    CHECK_SEGMENT(7); imu_msg.linear_acceleration.x = stof(_currentBufferSegment);
    CHECK_SEGMENT(8); imu_msg.linear_acceleration.y = stof(_currentBufferSegment);
    CHECK_SEGMENT(9); imu_msg.linear_acceleration.z = stof(_currentBufferSegment);
    eulerToQuat(roll, pitch, yaw);

    imu_pub.publish(imu_msg);
}

void Rover6SerialBridge::eulerToQuat(double roll, double pitch, double yaw)
{
    double cy = cos(yaw * 0.5);
	double sy = sin(yaw * 0.5);
	double cr = cos(roll * 0.5);
	double sr = sin(roll * 0.5);
	double cp = cos(pitch * 0.5);
	double sp = sin(pitch * 0.5);

	imu_msg.orientation.w = cy * cr * cp + sy * sr * sp;
	imu_msg.orientation.x = cy * sr * cp - sy * cr * sp;
	imu_msg.orientation.y = cy * cr * sp + sy * sr * cp;
	imu_msg.orientation.z = sy * cr * cp - cy * sr * sp;
}


double Rover6SerialBridge::convertTicksToCm(long ticks) {
    return (double)ticks * _cmPerTick;
}


void Rover6SerialBridge::parseEncoder()
{
    CHECK_SEGMENT(0); enc_msg.header.stamp = getDeviceTime((uint32_t)stoi(_currentBufferSegment));
    CHECK_SEGMENT(1); enc_msg.right_cm = convertTicksToCm(stol(_currentBufferSegment));
    CHECK_SEGMENT(2); enc_msg.left_cm = convertTicksToCm(stol(_currentBufferSegment));
    CHECK_SEGMENT(3); enc_msg.right_speed_cps = convertTicksToCm(stof(_currentBufferSegment));
    CHECK_SEGMENT(4); enc_msg.left_speed_cps = convertTicksToCm(stof(_currentBufferSegment));

    enc_pub.publish(enc_msg);
}