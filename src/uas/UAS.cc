/*===================================================================
======================================================================*/

/**
 * @file
 *   @brief Represents one unmanned aerial vehicle
 *
 *   @author Lorenz Meier <mavteam@student.ethz.ch>
 *
 */

#include <QList>
#include <QMessageBox>
#include <QTimer>
#include <QSettings>
#include <iostream>
#include <QDebug>
#include <cmath>
#include <qmath.h>
#include "UAS.h"
#include "LinkInterface.h"
#include "UASManager.h"
#include "QGC.h"
#include "GAudioOutput.h"
#include "MAVLinkProtocol.h"
#include "QGCMAVLink.h"
#include "LinkManager.h"
#include "SerialLink.h"

UAS::UAS(MAVLinkProtocol* protocol, int id) : UASInterface(),
uasId(id),
startTime(QGC::groundTimeMilliseconds()),
commStatus(COMM_DISCONNECTED),
name(""),
autopilot(-1),
links(new QList<LinkInterface*>()),
unknownPackets(),
mavlink(protocol),
waypointManager(*this),
thrustSum(0),
thrustMax(10),
startVoltage(0),
warnVoltage(9.5f),
warnLevelPercent(20.0f),
currentVoltage(12.0f),
lpVoltage(12.0f),
batteryRemainingEstimateEnabled(false),
mode(-1),
status(-1),
navMode(-1),
onboardTimeOffset(0),
controlRollManual(true),
controlPitchManual(true),
controlYawManual(true),
controlThrustManual(true),
manualRollAngle(0),
manualPitchAngle(0),
manualYawAngle(0),
manualThrust(0),
receiveDropRate(0),
sendDropRate(0),
lowBattAlarm(false),
positionLock(false),
localX(0.0),
localY(0.0),
localZ(0.0),
latitude(0.0),
longitude(0.0),
altitude(0.0),
roll(0.0),
pitch(0.0),
yaw(0.0),
statusTimeout(new QTimer(this)),
paramsOnceRequested(false),
airframe(0),
attitudeKnown(false),
paramManager(NULL),
attitudeStamped(false),
lastAttitude(0),
    simulation(new QGCFlightGearLink(this))
{
    color = UASInterface::getNextColor();
    setBattery(LIPOLY, 3);
    connect(statusTimeout, SIGNAL(timeout()), this, SLOT(updateState()));
    connect(this, SIGNAL(systemSpecsChanged(int)), this, SLOT(writeSettings()));
    statusTimeout->start(500);
    readSettings();
}

UAS::~UAS()
{
    writeSettings();
    delete links;
    links=NULL;
}

void UAS::writeSettings()
{
    QSettings settings;
    settings.beginGroup(QString("MAV%1").arg(uasId));
    settings.setValue("NAME", this->name);
    settings.setValue("AIRFRAME", this->airframe);
    settings.setValue("AP_TYPE", this->autopilot);
    settings.setValue("BATTERY_SPECS", getBatterySpecs());
    settings.endGroup();
    settings.sync();
}

void UAS::readSettings()
{
    QSettings settings;
    settings.beginGroup(QString("MAV%1").arg(uasId));
    this->name = settings.value("NAME", this->name).toString();
    this->airframe = settings.value("AIRFRAME", this->airframe).toInt();
    this->autopilot = settings.value("AP_TYPE", this->autopilot).toInt();
    if (settings.contains("BATTERY_SPECS")) {
        setBatterySpecs(settings.value("BATTERY_SPECS").toString());
    }
    settings.endGroup();
}

int UAS::getUASID() const
{
    return uasId;
}

void UAS::updateState()
{
    // Check if heartbeat timed out
    quint64 heartbeatInterval = QGC::groundTimeUsecs() - lastHeartbeat;
    if (heartbeatInterval > timeoutIntervalHeartbeat)
    {
        emit heartbeatTimeout(heartbeatInterval);
        emit heartbeatTimeout();
    }

    // Position lock is set by the MAVLink message handler
    // if no position lock is available, indicate an error
    if (positionLock)
    {
        positionLock = false;
    }
    else
    {
        if ((mode == (int)MAV_MODE_AUTO || mode == (int)MAV_MODE_GUIDED) && positionLock)
        {
            GAudioOutput::instance()->notifyNegative();
        }
    }
}

void UAS::setSelected()
{
    if (UASManager::instance()->getActiveUAS() != this) {
        UASManager::instance()->setActiveUAS(this);
        emit systemSelected(true);
    }
}

bool UAS::getSelected() const
{
    return (UASManager::instance()->getActiveUAS() == this);
}

void UAS::receiveMessageNamedValue(const mavlink_message_t& message)
{
    if (message.msgid == MAVLINK_MSG_ID_NAMED_VALUE_FLOAT)
    {
        mavlink_named_value_float_t val;
        mavlink_msg_named_value_float_decode(&message, &val);
        QByteArray bytes(val.name, MAVLINK_MSG_NAMED_VALUE_FLOAT_FIELD_NAME_LEN);
        emit valueChanged(this->getUASID(), QString(bytes), tr("raw"), val.value, getUnixTime());
    }
    else if (message.msgid == MAVLINK_MSG_ID_NAMED_VALUE_INT)
    {
        mavlink_named_value_int_t val;
        mavlink_msg_named_value_int_decode(&message, &val);
        QByteArray bytes(val.name, MAVLINK_MSG_NAMED_VALUE_INT_FIELD_NAME_LEN);
        emit valueChanged(this->getUASID(), QString(bytes), tr("raw"), val.value, getUnixTime());
    }
}

void UAS::receiveMessage(LinkInterface* link, mavlink_message_t message)
{
    if (!link) return;
    if (!links->contains(link))
    {
        addLink(link);
        //        qDebug() << __FILE__ << __LINE__ << "ADDED LINK!" << link->getName();
    }
    //    else
    //    {
    //        qDebug() << __FILE__ << __LINE__ << "DID NOT ADD LINK" << link->getName() << "ALREADY IN LIST";
    //    }

    //    qDebug() << "UAS RECEIVED from" << message.sysid << "component" << message.compid << "msg id" << message.msgid << "seq no" << message.seq;

    // Only accept messages from this system (condition 1)
    // and only then if a) attitudeStamped is disabled OR b) attitudeStamped is enabled
    // and we already got one attitude packet
    if (message.sysid == uasId && (!attitudeStamped || (attitudeStamped && (lastAttitude != 0)) || message.msgid == MAVLINK_MSG_ID_ATTITUDE))
    {
        QString uasState;
        QString stateDescription;

        switch (message.msgid)
        {
        case MAVLINK_MSG_ID_HEARTBEAT:
        {
            lastHeartbeat = QGC::groundTimeUsecs();
            emit heartbeat(this);
            mavlink_heartbeat_t state;
            mavlink_msg_heartbeat_decode(&message, &state);
            // Set new type if it has changed
            if (this->type != state.type)
            {
                this->type = state.type;
                if (airframe == 0)
                {
                    switch (type)
                    {
                    case MAV_TYPE_FIXED_WING:
                        setAirframe(UASInterface::QGC_AIRFRAME_EASYSTAR);
                        break;
                    case MAV_TYPE_QUADROTOR:
                        setAirframe(UASInterface::QGC_AIRFRAME_CHEETAH);
                        break;
                    default:
                        // Do nothing
                        break;
                    }
                }
                this->autopilot = state.autopilot;
                emit systemTypeSet(this, type);
            }

            QString audiostring = "System " + getUASName();
            QString stateAudio = "";
            QString modeAudio = "";
            QString navModeAudio = "";
            bool statechanged = false;
            bool modechanged = false;


            if (state.system_status != this->status)
            {
                statechanged = true;
                this->status = state.system_status;
                getStatusForCode((int)state.system_status, uasState, stateDescription);
                emit statusChanged(this, uasState, stateDescription);
                emit statusChanged(this->status);

                shortStateText = uasState;

                stateAudio = tr(" changed status to ") + uasState;
            }

            if (this->mode != static_cast<int>(state.system_mode))
            {
                modechanged = true;
                this->mode = static_cast<int>(state.system_mode);


                shortModeText = getShortModeTextFor(this->mode);

                emit modeChanged(this->getUASID(), shortModeText, "");

                modeAudio = " is now in " + shortModeText;
            }

            if (navMode != state.flight_mode)
            {
                emit navModeChanged(uasId, state.flight_mode, getNavModeText(state.flight_mode));
                navMode = state.flight_mode;
                navModeAudio = tr(" changed nav mode to ") + tr("FIXME");
            }

            // AUDIO
            if (modechanged && statechanged)
            {
                // Output both messages
                audiostring += modeAudio + " and " + stateAudio;
            }
            else if (modechanged || statechanged)
            {
                // Output the one message
                audiostring += modeAudio + stateAudio + navModeAudio;
            }

            if ((int)state.system_status == (int)MAV_STATE_CRITICAL || state.system_status == (int)MAV_STATE_EMERGENCY)
            {
                GAudioOutput::instance()->startEmergency();
            }
            else if (modechanged || statechanged)
            {
                GAudioOutput::instance()->stopEmergency();
                GAudioOutput::instance()->say(audiostring);
            }

            if (state.system_status == MAV_STATE_POWEROFF)
            {
                emit systemRemoved(this);
                emit systemRemoved();
            }
}

            break;
        case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT:
        case MAVLINK_MSG_ID_NAMED_VALUE_INT:
            // Receive named value message
            receiveMessageNamedValue(message);
            break;
        case MAVLINK_MSG_ID_SYS_STATUS:
        {
                mavlink_sys_status_t state;
                mavlink_msg_sys_status_decode(&message, &state);

                emit loadChanged(this,state.load/10.0f);
                emit valueChanged(uasId, "Load", "%", ((float)state.load)/10.0f, getUnixTime());

                currentVoltage = state.voltage_battery/1000.0f;
                lpVoltage = filterVoltage(currentVoltage);
                if (startVoltage == 0) startVoltage = currentVoltage;
                timeRemaining = calculateTimeRemaining();
                if (!batteryRemainingEstimateEnabled)
                {
                    chargeLevel = state.battery_percent/2.55f;
                }
                //qDebug() << "Voltage: " << currentVoltage << " Chargelevel: " << getChargeLevel() << " Time remaining " << timeRemaining;
                emit batteryChanged(this, lpVoltage, getChargeLevel(), timeRemaining);
                emit voltageChanged(message.sysid, state.battery_percent/2.55f);

                // LOW BATTERY ALARM
                if (lpVoltage < warnVoltage)
                {
                    startLowBattAlarm();
                }
                else
                {
                    stopLowBattAlarm();
                }

                // COMMUNICATIONS DROP RATE
                emit dropRateChanged(this->getUASID(), state.errors_uart/1000.0f);
            }
            break;

#ifdef MAVLINK_ENABLED_PIXHAWK
        case MAVLINK_MSG_ID_CONTROL_STATUS:
            {
                mavlink_control_status_t status;
                mavlink_msg_control_status_decode(&message, &status);
                // Emit control status vector
                emit attitudeControlEnabled(static_cast<bool>(status.control_att));
                emit positionXYControlEnabled(static_cast<bool>(status.control_pos_xy));
                emit positionZControlEnabled(static_cast<bool>(status.control_pos_z));
                emit positionYawControlEnabled(static_cast<bool>(status.control_pos_yaw));

                // Emit localization status vector
                emit localizationChanged(this, status.position_fix);
                emit visionLocalizationChanged(this, status.vision_fix);
                emit gpsLocalizationChanged(this, status.gps_fix);
            }
            break;
#endif // PIXHAWK
        case MAVLINK_MSG_ID_RAW_IMU:
            {
                mavlink_raw_imu_t raw;
                mavlink_msg_raw_imu_decode(&message, &raw);
                quint64 time = getUnixTime(raw.usec);

                emit valueChanged(uasId, "accel x", "raw", static_cast<double>(raw.xacc), time);
                emit valueChanged(uasId, "accel y", "raw", static_cast<double>(raw.yacc), time);
                emit valueChanged(uasId, "accel z", "raw", static_cast<double>(raw.zacc), time);
                emit valueChanged(uasId, "gyro roll", "raw", static_cast<double>(raw.xgyro), time);
                emit valueChanged(uasId, "gyro pitch", "raw", static_cast<double>(raw.ygyro), time);
                emit valueChanged(uasId, "gyro yaw", "raw", static_cast<double>(raw.zgyro), time);
                emit valueChanged(uasId, "mag x", "raw", static_cast<double>(raw.xmag), time);
                emit valueChanged(uasId, "mag y", "raw", static_cast<double>(raw.ymag), time);
                emit valueChanged(uasId, "mag z", "raw", static_cast<double>(raw.zmag), time);
            }
            break;
        case MAVLINK_MSG_ID_SCALED_IMU:
            {
                mavlink_scaled_imu_t scaled;
                mavlink_msg_scaled_imu_decode(&message, &scaled);
                quint64 time = getUnixTime(scaled.usec);

                emit valueChanged(uasId, "accel x", "g", scaled.xacc/1000.0f, time);
                emit valueChanged(uasId, "accel y", "g", scaled.yacc/1000.0f, time);
                emit valueChanged(uasId, "accel z", "g", scaled.zacc/1000.0f, time);
                emit valueChanged(uasId, "gyro roll", "rad/s", scaled.xgyro/1000.0f, time);
                emit valueChanged(uasId, "gyro pitch", "rad/s", scaled.ygyro/1000.0f, time);
                emit valueChanged(uasId, "gyro yaw", "rad/s", scaled.zgyro/1000.0f, time);
                emit valueChanged(uasId, "mag x", "uTesla", scaled.xmag/100.0f, time);
                emit valueChanged(uasId, "mag y", "uTesla", scaled.ymag/100.0f, time);
                emit valueChanged(uasId, "mag z", "uTesla", scaled.zmag/100.0f, time);
            }
            break;
        case MAVLINK_MSG_ID_ATTITUDE:
            //std::cerr << std::endl;
            //std::cerr << "Decoded attitude message:" << " roll: " << std::dec << mavlink_msg_attitude_get_roll(message.payload) << " pitch: " << mavlink_msg_attitude_get_pitch(message.payload) << " yaw: " << mavlink_msg_attitude_get_yaw(message.payload) << std::endl;
            {
                mavlink_attitude_t attitude;
                mavlink_msg_attitude_decode(&message, &attitude);
                quint64 time = getUnixReferenceTime(attitude.usec);
                lastAttitude = time;
                roll = QGC::limitAngleToPMPIf(attitude.roll);
                pitch = QGC::limitAngleToPMPIf(attitude.pitch);
                yaw = QGC::limitAngleToPMPIf(attitude.yaw);
                emit valueChanged(uasId, "roll", "rad", roll, time);
                emit valueChanged(uasId, "pitch", "rad", pitch, time);
                emit valueChanged(uasId, "yaw", "rad", yaw, time);
                emit valueChanged(uasId, "rollspeed", "rad/s", attitude.rollspeed, time);
                emit valueChanged(uasId, "pitchspeed", "rad/s", attitude.pitchspeed, time);
                emit valueChanged(uasId, "yawspeed", "rad/s", attitude.yawspeed, time);

                // Emit in angles

                // Convert yaw angle to compass value
                // in 0 - 360 deg range
                float compass = (yaw/M_PI)*180.0+360.0f;
                while (compass > 360.0f) {
                    compass -= 360.0f;
                }

                attitudeKnown = true;

                emit valueChanged(uasId, "roll deg", "deg", (roll/M_PI)*180.0, time);
                emit valueChanged(uasId, "pitch deg", "deg", (pitch/M_PI)*180.0, time);
                emit valueChanged(uasId, "heading deg", "deg", compass, time);
                emit valueChanged(uasId, "rollspeed d/s", "deg/s", (attitude.rollspeed/M_PI)*180.0, time);
                emit valueChanged(uasId, "pitchspeed d/s", "deg/s", (attitude.pitchspeed/M_PI)*180.0, time);
                emit valueChanged(uasId, "yawspeed d/s", "deg/s", (attitude.yawspeed/M_PI)*180.0, time);

                emit attitudeChanged(this, roll, pitch, yaw, time);
                emit attitudeSpeedChanged(uasId, attitude.rollspeed, attitude.pitchspeed, attitude.yawspeed, time);
            }
            break;
        case MAVLINK_MSG_ID_HIL_CONTROLS:
            {
                mavlink_hil_controls_t hil;
                mavlink_msg_hil_controls_decode(&message, &hil);
                emit hilControlsChanged(hil.time_us, hil.roll_ailerons, hil.pitch_elevator, hil.yaw_rudder, hil.throttle, hil.mode, hil.nav_mode);
            }
            break;
        case MAVLINK_MSG_ID_VFR_HUD:
            {
                mavlink_vfr_hud_t hud;
                mavlink_msg_vfr_hud_decode(&message, &hud);
                quint64 time = getUnixTime();
                // Display updated values
                emit valueChanged(uasId, "airspeed", "m/s", hud.airspeed, time);
                emit valueChanged(uasId, "groundspeed", "m/s", hud.groundspeed, time);
                emit valueChanged(uasId, "altitude", "m", hud.alt, time);
                emit valueChanged(uasId, "heading", "deg", hud.heading, time);
                emit valueChanged(uasId, "climbrate", "m/s", hud.climb, time);
                emit valueChanged(uasId, "throttle", "%", hud.throttle, time);
                emit thrustChanged(this, hud.throttle/100.0);

                if (!attitudeKnown)
                {
                    yaw = QGC::limitAngleToPMPId((((double)hud.heading-180.0)/360.0)*M_PI);
                    emit attitudeChanged(this, roll, pitch, yaw, time);
                }

                emit altitudeChanged(uasId, hud.alt);
                //yaw = (hud.heading-180.0f/360.0f)*M_PI;
                //emit attitudeChanged(this, roll, pitch, yaw, getUnixTime());
                emit speedChanged(this, hud.airspeed, 0.0f, hud.climb, getUnixTime());
            }
            break;
        case MAVLINK_MSG_ID_NAV_CONTROLLER_OUTPUT:
            {
                mavlink_nav_controller_output_t nav;
                mavlink_msg_nav_controller_output_decode(&message, &nav);
                quint64 time = getUnixTime();
                // Update UI
                emit valueChanged(uasId, "nav roll", "deg", nav.nav_roll, time);
                emit valueChanged(uasId, "nav pitch", "deg", nav.nav_pitch, time);
                emit valueChanged(uasId, "nav bearing", "deg", nav.nav_bearing, time);
                emit valueChanged(uasId, "target bearing", "deg", nav.target_bearing, time);
                emit valueChanged(uasId, "wp dist", "m", nav.wp_dist, time);
                emit valueChanged(uasId, "alt err", "m", nav.alt_error, time);
                emit valueChanged(uasId, "airspeed err", "m/s", nav.alt_error, time);
                emit valueChanged(uasId, "xtrack err", "m", nav.xtrack_error, time);
            }
            break;
        case MAVLINK_MSG_ID_LOCAL_POSITION:
            //std::cerr << std::endl;
            //std::cerr << "Decoded attitude message:" << " roll: " << std::dec << mavlink_msg_attitude_get_roll(message.payload) << " pitch: " << mavlink_msg_attitude_get_pitch(message.payload) << " yaw: " << mavlink_msg_attitude_get_yaw(message.payload) << std::endl;
            {
                mavlink_local_position_t pos;
                mavlink_msg_local_position_decode(&message, &pos);
                quint64 time = getUnixTime(pos.usec);
                localX = pos.x;
                localY = pos.y;
                localZ = pos.z;
                emit valueChanged(uasId, "x", "m", pos.x, time);
                emit valueChanged(uasId, "y", "m", pos.y, time);
                emit valueChanged(uasId, "z", "m", pos.z, time);
                emit valueChanged(uasId, "x speed", "m/s", pos.vx, time);
                emit valueChanged(uasId, "y speed", "m/s", pos.vy, time);
                emit valueChanged(uasId, "z speed", "m/s", pos.vz, time);
                emit localPositionChanged(this, pos.x, pos.y, pos.z, time);
                emit speedChanged(this, pos.vx, pos.vy, pos.vz, time);

                //                qDebug()<<"Local Position = "<<pos.x<<" - "<<pos.y<<" - "<<pos.z;
                //                qDebug()<<"Speed Local Position = "<<pos.vx<<" - "<<pos.vy<<" - "<<pos.vz;

                //emit attitudeChanged(this, pos.roll, pos.pitch, pos.yaw, time);
                // Set internal state
                if (!positionLock) {
                    // If position was not locked before, notify positive
                    GAudioOutput::instance()->notifyPositive();
                }
                positionLock = true;
            }
            break;
        case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
            //std::cerr << std::endl;
            //std::cerr << "Decoded attitude message:" << " roll: " << std::dec << mavlink_msg_attitude_get_roll(message.payload) << " pitch: " << mavlink_msg_attitude_get_pitch(message.payload) << " yaw: " << mavlink_msg_attitude_get_yaw(message.payload) << std::endl;
            {
                mavlink_global_position_int_t pos;
                mavlink_msg_global_position_int_decode(&message, &pos);
                quint64 time = getUnixTime();
                latitude = pos.lat/(double)1E7;
                longitude = pos.lon/(double)1E7;
                altitude = pos.alt/1000.0;
                speedX = pos.vx/100.0;
                speedY = pos.vy/100.0;
                speedZ = pos.vz/100.0;
                emit valueChanged(uasId, "latitude", "deg", latitude, time);
                emit valueChanged(uasId, "longitude", "deg", longitude, time);
                emit valueChanged(uasId, "altitude", "m", altitude, time);
                double totalSpeed = sqrt(speedX*speedX + speedY*speedY + speedZ*speedZ);
                emit valueChanged(uasId, "gps speed", "m/s", totalSpeed, time);
                emit globalPositionChanged(this, latitude, longitude, altitude, time);
                emit speedChanged(this, speedX, speedY, speedZ, time);
                // Set internal state
                if (!positionLock) {
                    // If position was not locked before, notify positive
                    GAudioOutput::instance()->notifyPositive();
                }
                positionLock = true;
                //TODO fix this hack for forwarding of global position for patch antenna tracking
                forwardMessage(message);
            }
            break;
        case MAVLINK_MSG_ID_GLOBAL_POSITION:
            {
                mavlink_global_position_t pos;
                mavlink_msg_global_position_decode(&message, &pos);
                quint64 time = getUnixTime();
                latitude = pos.lat;
                longitude = pos.lon;
                altitude = pos.alt;
                speedX = pos.vx;
                speedY = pos.vy;
                speedZ = pos.vz;
                emit valueChanged(uasId, "latitude", "deg", latitude, time);
                emit valueChanged(uasId, "longitude", "deg", longitude, time);
                emit valueChanged(uasId, "altitude", "m", altitude, time);
                double totalSpeed = sqrt(speedX*speedX + speedY*speedY + speedZ*speedZ);
                emit valueChanged(uasId, "gps speed", "m/s", totalSpeed, time);
                emit globalPositionChanged(this, latitude, longitude, altitude, time);
                emit speedChanged(this, speedX, speedY, speedZ, time);
                // Set internal state
                if (!positionLock) {
                    // If position was not locked before, notify positive
                    GAudioOutput::instance()->notifyPositive();
                }
                positionLock = true;
                //TODO fix this hack for forwarding of global position for patch antenna tracking
                forwardMessage(message);
            }
            break;
        case MAVLINK_MSG_ID_GPS_RAW:
            //std::cerr << std::endl;
            //std::cerr << "Decoded attitude message:" << " roll: " << std::dec << mavlink_msg_attitude_get_roll(message.payload) << " pitch: " << mavlink_msg_attitude_get_pitch(message.payload) << " yaw: " << mavlink_msg_attitude_get_yaw(message.payload) << std::endl;
            {
                mavlink_gps_raw_t pos;
                mavlink_msg_gps_raw_decode(&message, &pos);

                // SANITY CHECK
                // only accept values in a realistic range
                // quint64 time = getUnixTime(pos.usec);
                quint64 time = getUnixTime();

                emit valueChanged(uasId, "latitude", "deg", pos.lat, time);
                emit valueChanged(uasId, "longitude", "deg", pos.lon, time);

                if (pos.fix_type > 0) {
                    emit valueChanged(uasId, "gps speed", "m/s", pos.v, time);
                    latitude = pos.lat;
                    longitude = pos.lon;
                    altitude = pos.alt;
                    positionLock = true;

                    // Check for NaN
                    int alt = pos.alt;
                    if (alt != alt) {
                        alt = 0;
                        emit textMessageReceived(uasId, message.compid, 255, "GCS ERROR: RECEIVED NaN FOR ALTITUDE");
                    }
                    emit valueChanged(uasId, "altitude", "m", pos.alt, time);
                    // Smaller than threshold and not NaN
                    if (pos.v < 1000000 && pos.v == pos.v) {
                        emit valueChanged(uasId, "speed", "m/s", pos.v, time);
                        //qDebug() << "GOT GPS RAW";
                        // emit speedChanged(this, (double)pos.v, 0.0, 0.0, time);
                    } else {
                        emit textMessageReceived(uasId, message.compid, 255, QString("GCS ERROR: RECEIVED INVALID SPEED OF %1 m/s").arg(pos.v));
                    }
                }
            }
            break;
        case MAVLINK_MSG_ID_GPS_RAW_INT:
            {
                mavlink_gps_raw_int_t pos;
                mavlink_msg_gps_raw_int_decode(&message, &pos);

                // SANITY CHECK
                // only accept values in a realistic range
                // quint64 time = getUnixTime(pos.usec);
                quint64 time = getUnixTime();

                emit valueChanged(uasId, "latitude", "deg", pos.lat/(double)1E7, time);
                emit valueChanged(uasId, "longitude", "deg", pos.lon/(double)1E7, time);

                if (pos.fix_type > 0) {
                    emit globalPositionChanged(this, pos.lat/(double)1E7, pos.lon/(double)1E7, pos.alt/1000.0, time);
                    emit valueChanged(uasId, "gps speed", "m/s", pos.vel, time);
                    latitude = pos.lat/(double)1E7;
                    longitude = pos.lon/(double)1E7;
                    altitude = pos.alt/1000.0;
                    positionLock = true;

                    // Check for NaN
                    int alt = pos.alt;
                    if (alt != alt) {
                        alt = 0;
                        emit textMessageReceived(uasId, message.compid, 255, "GCS ERROR: RECEIVED NaN FOR ALTITUDE");
                    }
                    emit valueChanged(uasId, "altitude", "m", pos.alt/(double)1E3, time);
                    // Smaller than threshold and not NaN

                    float vel = pos.vel/100.0f;

                    if (vel < 1000000 && !isnan(vel) && !isinf(vel)) {
                        emit valueChanged(uasId, "speed", "m/s", vel, time);
                        //qDebug() << "GOT GPS RAW";
                        // emit speedChanged(this, (double)pos.v, 0.0, 0.0, time);
                    } else {
                        emit textMessageReceived(uasId, message.compid, 255, QString("GCS ERROR: RECEIVED INVALID SPEED OF %1 m/s").arg(vel));
                    }
                }
            }
            break;
        case MAVLINK_MSG_ID_GPS_STATUS:
            {
                mavlink_gps_status_t pos;
                mavlink_msg_gps_status_decode(&message, &pos);
                for(int i = 0; i < (int)pos.satellites_visible; i++)
                {
                    emit gpsSatelliteStatusChanged(uasId, (unsigned char)pos.satellite_prn[i], (unsigned char)pos.satellite_elevation[i], (unsigned char)pos.satellite_azimuth[i], (unsigned char)pos.satellite_snr[i], static_cast<bool>(pos.satellite_used[i]));
                }
            }
            break;
        case MAVLINK_MSG_ID_GPS_LOCAL_ORIGIN_SET:
            {
                mavlink_gps_local_origin_set_t pos;
                mavlink_msg_gps_local_origin_set_decode(&message, &pos);
                emit homePositionChanged(uasId, pos.latitude, pos.longitude, pos.altitude);
            }
            break;
        case MAVLINK_MSG_ID_RAW_PRESSURE:
            {
                mavlink_raw_pressure_t pressure;
                mavlink_msg_raw_pressure_decode(&message, &pressure);
                quint64 time = this->getUnixTime(pressure.usec);
                emit valueChanged(uasId, "abs pressure", "raw", pressure.press_abs, time);
                emit valueChanged(uasId, "diff pressure 1", "raw", pressure.press_diff1, time);
                emit valueChanged(uasId, "diff pressure 2", "raw", pressure.press_diff2, time);
                emit valueChanged(uasId, "temperature", "raw", pressure.temperature, time);
            }
            break;

        case MAVLINK_MSG_ID_SCALED_PRESSURE:
            {
                mavlink_scaled_pressure_t pressure;
                mavlink_msg_scaled_pressure_decode(&message, &pressure);
                quint64 time = this->getUnixTime(pressure.usec);
                emit valueChanged(uasId, "abs pressure", "hPa", pressure.press_abs, time);
                emit valueChanged(uasId, "diff pressure", "hPa", pressure.press_diff, time);
                emit valueChanged(uasId, "temperature", "C", pressure.temperature/100.0, time);
            }
            break;

        case MAVLINK_MSG_ID_RC_CHANNELS_RAW:
            {
                mavlink_rc_channels_raw_t channels;
                mavlink_msg_rc_channels_raw_decode(&message, &channels);
                emit remoteControlRSSIChanged(channels.rssi/255.0f);
                emit remoteControlChannelRawChanged(0, channels.chan1_raw);
                emit remoteControlChannelRawChanged(1, channels.chan2_raw);
                emit remoteControlChannelRawChanged(2, channels.chan3_raw);
                emit remoteControlChannelRawChanged(3, channels.chan4_raw);
                emit remoteControlChannelRawChanged(4, channels.chan5_raw);
                emit remoteControlChannelRawChanged(5, channels.chan6_raw);
                emit remoteControlChannelRawChanged(6, channels.chan7_raw);
                emit remoteControlChannelRawChanged(7, channels.chan8_raw);
                quint64 time = getUnixTime();
                emit valueChanged(uasId, "rc in #1", "us", channels.chan1_raw, time);
                emit valueChanged(uasId, "rc in #2", "us", channels.chan2_raw, time);
                emit valueChanged(uasId, "rc in #3", "us", channels.chan3_raw, time);
                emit valueChanged(uasId, "rc in #4", "us", channels.chan4_raw, time);
                emit valueChanged(uasId, "rc in #5", "us", channels.chan5_raw, time);
                emit valueChanged(uasId, "rc in #6", "us", channels.chan6_raw, time);
                emit valueChanged(uasId, "rc in #7", "us", channels.chan7_raw, time);
                emit valueChanged(uasId, "rc in #8", "us", channels.chan8_raw, time);
            }
            break;
        case MAVLINK_MSG_ID_RC_CHANNELS_SCALED:
            {
                mavlink_rc_channels_scaled_t channels;
                mavlink_msg_rc_channels_scaled_decode(&message, &channels);
                emit remoteControlRSSIChanged(channels.rssi/255.0f);
                emit remoteControlChannelScaledChanged(0, channels.chan1_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(1, channels.chan2_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(2, channels.chan3_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(3, channels.chan4_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(4, channels.chan5_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(5, channels.chan6_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(6, channels.chan7_scaled/10000.0f);
                emit remoteControlChannelScaledChanged(7, channels.chan8_scaled/10000.0f);
            }
            break;
        case MAVLINK_MSG_ID_PARAM_VALUE:
            {
                mavlink_param_value_t value;
                mavlink_msg_param_value_decode(&message, &value);
                QByteArray bytes((char*)value.param_id, MAVLINK_MSG_PARAM_VALUE_FIELD_PARAM_ID_LEN);
                QString parameterName = QString(bytes);
                int component = message.compid;
                float val = value.param_value;

                // Insert component if necessary
                if (!parameters.contains(component))
                {
                    parameters.insert(component, new QMap<QString, float>());
                }

                // Insert parameter into registry
                if (parameters.value(component)->contains(parameterName)) parameters.value(component)->remove(parameterName);
                parameters.value(component)->insert(parameterName, val);

                // Emit change
                emit parameterChanged(uasId, message.compid, parameterName, val);
                emit parameterChanged(uasId, message.compid, value.param_count, value.param_index, parameterName, val);
            }
            break;
        case MAVLINK_MSG_ID_COMMAND_ACK:
            mavlink_command_ack_t ack;
            mavlink_msg_command_ack_decode(&message, &ack);
            if (ack.result == 1)
            {
                emit textMessageReceived(uasId, message.compid, 0, tr("SUCCESS: Executed CMD: %1").arg(ack.command));
            }
            else
            {
                emit textMessageReceived(uasId, message.compid, 0, tr("FAILURE: Rejected CMD: %1").arg(ack.command));
            }
            break;
        case MAVLINK_MSG_ID_DEBUG:
            emit valueChanged(uasId, QString("debug ") + QString::number(mavlink_msg_debug_get_ind(&message)), "raw", mavlink_msg_debug_get_value(&message), MG::TIME::getGroundTimeNow());
            break;
        case MAVLINK_MSG_ID_ROLL_PITCH_YAW_THRUST_SETPOINT:
            {
                mavlink_roll_pitch_yaw_thrust_setpoint_t out;
                mavlink_msg_roll_pitch_yaw_thrust_setpoint_decode(&message, &out);
                quint64 time = getUnixTime(out.time_ms*1000);
                emit attitudeThrustSetPointChanged(this, out.roll, out.pitch, out.yaw, out.thrust, time);
                emit valueChanged(uasId, "att control roll", "rad", out.roll, time);
                emit valueChanged(uasId, "att control pitch", "rad", out.pitch, time);
                emit valueChanged(uasId, "att control yaw", "rad", out.yaw, time);
                emit valueChanged(uasId, "att control thrust", "0-1", out.thrust, time);
            }
            break;
        case MAVLINK_MSG_ID_WAYPOINT_COUNT:
            {
                mavlink_waypoint_count_t wpc;
                mavlink_msg_waypoint_count_decode(&message, &wpc);
                if (wpc.target_system == mavlink->getSystemId() && wpc.target_component == mavlink->getComponentId())
                {
                    waypointManager.handleWaypointCount(message.sysid, message.compid, wpc.count);
                }
                else
                {
                    qDebug() << "Got waypoint message, but was not for me";
                }
            }
            break;

        case MAVLINK_MSG_ID_WAYPOINT:
            {
                mavlink_waypoint_t wp;
                mavlink_msg_waypoint_decode(&message, &wp);
                //qDebug() << "got waypoint (" << wp.seq << ") from ID " << message.sysid << " x=" << wp.x << " y=" << wp.y << " z=" << wp.z;
                if(wp.target_system == mavlink->getSystemId() && wp.target_component == mavlink->getComponentId())
                {
                    waypointManager.handleWaypoint(message.sysid, message.compid, &wp);
                }
                else
                {
                    qDebug() << "Got waypoint message, but was not for me";
                }
            }
            break;

        case MAVLINK_MSG_ID_WAYPOINT_ACK:
            {
                mavlink_waypoint_ack_t wpa;
                mavlink_msg_waypoint_ack_decode(&message, &wpa);
                if(wpa.target_system == mavlink->getSystemId() && wpa.target_component == mavlink->getComponentId())
                {
                    waypointManager.handleWaypointAck(message.sysid, message.compid, &wpa);
                }
            }
            break;

        case MAVLINK_MSG_ID_WAYPOINT_REQUEST:
            {
                mavlink_waypoint_request_t wpr;
                mavlink_msg_waypoint_request_decode(&message, &wpr);
                if(wpr.target_system == mavlink->getSystemId() && wpr.target_component == mavlink->getComponentId())
                {
                    waypointManager.handleWaypointRequest(message.sysid, message.compid, &wpr);
                }
                else
                {
                    qDebug() << "Got waypoint message, but was not for me";
                }
            }
            break;

        case MAVLINK_MSG_ID_WAYPOINT_REACHED:
            {
                mavlink_waypoint_reached_t wpr;
                mavlink_msg_waypoint_reached_decode(&message, &wpr);
                waypointManager.handleWaypointReached(message.sysid, message.compid, &wpr);
                QString text = QString("System %1 reached waypoint %2").arg(getUASName()).arg(wpr.seq);
                GAudioOutput::instance()->say(text);
                emit textMessageReceived(message.sysid, message.compid, 0, text);
            }
            break;

        case MAVLINK_MSG_ID_WAYPOINT_CURRENT:
            {
                mavlink_waypoint_current_t wpc;
                mavlink_msg_waypoint_current_decode(&message, &wpc);
                waypointManager.handleWaypointCurrent(message.sysid, message.compid, &wpc);
            }
            break;

        case MAVLINK_MSG_ID_LOCAL_POSITION_SETPOINT:
            {
                mavlink_local_position_setpoint_t p;
                mavlink_msg_local_position_setpoint_decode(&message, &p);
                emit positionSetPointsChanged(uasId, p.x, p.y, p.z, p.yaw, QGC::groundTimeUsecs());
            }
            break;
        case MAVLINK_MSG_ID_SERVO_OUTPUT_RAW:
            {
                mavlink_servo_output_raw_t servos;
                mavlink_msg_servo_output_raw_decode(&message, &servos);
                quint64 time = getUnixTime();
                emit valueChanged(uasId, "servo #1", "us", servos.servo1_raw, time);
                emit valueChanged(uasId, "servo #2", "us", servos.servo2_raw, time);
                emit valueChanged(uasId, "servo #3", "us", servos.servo3_raw, time);
                emit valueChanged(uasId, "servo #4", "us", servos.servo4_raw, time);
                emit valueChanged(uasId, "servo #5", "us", servos.servo5_raw, time);
                emit valueChanged(uasId, "servo #6", "us", servos.servo6_raw, time);
                emit valueChanged(uasId, "servo #7", "us", servos.servo7_raw, time);
                emit valueChanged(uasId, "servo #8", "us", servos.servo8_raw, time);
            }
            break;
        case MAVLINK_MSG_ID_STATUSTEXT:
            {
                QByteArray b;
                b.resize(MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN);
                mavlink_msg_statustext_get_text(&message, b.data());
                //b.append('\0');
                QString text = QString(b);
                int severity = mavlink_msg_statustext_get_severity(&message);
                //qDebug() << "RECEIVED STATUS:" << text;false
                //emit statusTextReceived(severity, text);
                emit textMessageReceived(uasId, message.compid, severity, text);
            }
            break;
#ifdef MAVLINK_ENABLED_PIXHAWK
        case MAVLINK_MSG_ID_DATA_TRANSMISSION_HANDSHAKE:
            {
                qDebug() << "RECIEVED ACK TO GET IMAGE";
                mavlink_data_transmission_handshake_t p;
                mavlink_msg_data_transmission_handshake_decode(&message, &p);
                imageSize = p.size;
                imagePackets = p.packets;
                imagePayload = p.payload;
                imageQuality = p.jpg_quality;
                imageType = p.type;
                imageStart = QGC::groundTimeMilliseconds();
            }
            break;

        case MAVLINK_MSG_ID_ENCAPSULATED_DATA:
            {
                mavlink_encapsulated_data_t img;
                mavlink_msg_encapsulated_data_decode(&message, &img);
                int seq = img.seqnr;
                int pos = seq * imagePayload;

                // Check if we have a valid transaction
                if (imagePackets == 0)
                {
                    // NO VALID TRANSACTION - ABORT
                    // Restart statemachine
                    imagePacketsArrived = 0;
                }

                for (int i = 0; i < imagePayload; ++i)
                {
                    if (pos <= imageSize) {
                        imageRecBuffer[pos] = img.data[i];
                    }
                    ++pos;
                }

                ++imagePacketsArrived;

                // emit signal if all packets arrived
                if ((imagePacketsArrived >= imagePackets))
                {
                    // Restart statemachine
                    imagePacketsArrived = 0;
                    emit imageReady(this);
                    qDebug() << "imageReady emitted. all packets arrived";
                }
            }
            break;
#endif
        case MAVLINK_MSG_ID_DEBUG_VECT:
        {
            mavlink_debug_vect_t vect;
            mavlink_msg_debug_vect_decode(&message, &vect);
            QString str((const char*)vect.name);
            quint64 time = getUnixTime(vect.usec);
            emit valueChanged(uasId, str+".x", "raw", vect.x, time);
            emit valueChanged(uasId, str+".y", "raw", vect.y, time);
            emit valueChanged(uasId, str+".z", "raw", vect.z, time);
        }
        break;
        // WILL BE ENABLED ONCE MESSAGE IS IN COMMON MESSAGE SET
//        case MAVLINK_MSG_ID_MEMORY_VECT:
//        {
//            mavlink_memory_vect_t vect;
//            mavlink_msg_memory_vect_decode(&message, &vect);
//            QString str("mem_%1");
//            quint64 time = getUnixTime(0);
//            int16_t *mem0 = (int16_t *)&vect.value[0];
//            uint16_t *mem1 = (uint16_t *)&vect.value[0];
//            int32_t *mem2 = (int32_t *)&vect.value[0];
//            // uint32_t *mem3 = (uint32_t *)&vect.value[0]; causes overload problem
//            float *mem4 = (float *)&vect.value[0];
//            if ( vect.ver == 0) vect.type = 0, vect.ver = 1; else ;
//            if ( vect.ver == 1)
//            {
//                switch (vect.type) {
//                default:
//                case 0:
//                    for (int i = 0; i < 16; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*2)), "i16", mem0[i], time);
//                    break;
//                case 1:
//                    for (int i = 0; i < 16; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*2)), "ui16", mem1[i], time);
//                    break;
//                case 2:
//                    for (int i = 0; i < 16; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*2)), "Q15", (float)mem0[i]/32767.0, time);
//                    break;
//                case 3:
//                    for (int i = 0; i < 16; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*2)), "1Q14", (float)mem0[i]/16383.0, time);
//                    break;
//                case 4:
//                    for (int i = 0; i < 8; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*4)), "i32", mem2[i], time);
//                    break;
//                case 5:
//                    for (int i = 0; i < 8; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*4)), "i32", mem2[i], time);
//                    break;
//                case 6:
//                    for (int i = 0; i < 8; i++)
//                        emit valueChanged(uasId, str.arg(vect.address+(i*4)), "float", mem4[i], time);
//                    break;
//                }
//            }
//        }
//        break;
				//#ifdef MAVLINK_ENABLED_PIXHAWK
				//            case MAVLINK_MSG_ID_POINT_OF_INTEREST:
				//            {
				//                mavlink_point_of_interest_t poi;
				//                mavlink_msg_point_of_interest_decode(&message, &poi);
				//                emit poiFound(this, poi.type, poi.color, QString((QChar*)poi.name, MAVLINK_MSG_POINT_OF_INTEREST_FIELD_NAME_LEN), poi.x, poi.y, poi.z);
				//            }
				//            break;
				//            case MAVLINK_MSG_ID_POINT_OF_INTEREST_CONNECTION:
				//            {
				//                mavlink_point_of_interest_connection_t poi;
				//                mavlink_msg_point_of_interest_connection_decode(&message, &poi);
				//                emit poiConnectionFound(this, poi.type, poi.color, QString((QChar*)poi.name, MAVLINK_MSG_POINT_OF_INTEREST_CONNECTION_FIELD_NAME_LEN), poi.x1, poi.y1, poi.z1, poi.x2, poi.y2, poi.z2);
				//            }
				//            break;
				//#endif
#ifdef MAVLINK_ENABLED_UALBERTA
        case MAVLINK_MSG_ID_NAV_FILTER_BIAS:
            {
                mavlink_nav_filter_bias_t bias;
                mavlink_msg_nav_filter_bias_decode(&message, &bias);
                quint64 time = getUnixTime();
                emit valueChanged(uasId, "b_f[0]", "raw", bias.accel_0, time);
                emit valueChanged(uasId, "b_f[1]", "raw", bias.accel_1, time);
                emit valueChanged(uasId, "b_f[2]", "raw", bias.accel_2, time);
                emit valueChanged(uasId, "b_w[0]", "raw", bias.gyro_0, time);
                emit valueChanged(uasId, "b_w[1]", "raw", bias.gyro_1, time);
                emit valueChanged(uasId, "b_w[2]", "raw", bias.gyro_2, time);
            }
            break;
        case MAVLINK_MSG_ID_RADIO_CALIBRATION:
            {
                mavlink_radio_calibration_t radioMsg;
                mavlink_msg_radio_calibration_decode(&message, &radioMsg);
                QVector<uint16_t> aileron;
                QVector<uint16_t> elevator;
                QVector<uint16_t> rudder;
                QVector<uint16_t> gyro;
                QVector<uint16_t> pitch;
                QVector<uint16_t> throttle;

                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_AILERON_LEN; ++i)
                    aileron << radioMsg.aileron[i];
                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_ELEVATOR_LEN; ++i)
                    elevator << radioMsg.elevator[i];
                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_RUDDER_LEN; ++i)
                    rudder << radioMsg.rudder[i];
                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_GYRO_LEN; ++i)
                    gyro << radioMsg.gyro[i];
                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_PITCH_LEN; ++i)
                    pitch << radioMsg.pitch[i];
                for (int i=0; i<MAVLINK_MSG_RADIO_CALIBRATION_FIELD_THROTTLE_LEN; ++i)
                    throttle << radioMsg.throttle[i];

                QPointer<RadioCalibrationData> radioData = new RadioCalibrationData(aileron, elevator, rudder, gyro, pitch, throttle);
                emit radioCalibrationReceived(radioData);
                delete radioData;
            }
            break;

#endif
            // Messages to ignore
        case MAVLINK_MSG_ID_LOCAL_POSITION_SETPOINT_SET:
            break;
        default:
            {
                if (!unknownPackets.contains(message.msgid))
                {
                    unknownPackets.append(message.msgid);
                    QString errString = tr("UNABLE TO DECODE MESSAGE NUMBER %1").arg(message.msgid);
                    GAudioOutput::instance()->say(errString+tr(", please check console for details."));
                    emit textMessageReceived(uasId, message.compid, 255, errString);
                    std::cout << "Unable to decode message from system " << std::dec << static_cast<int>(message.sysid) << " with message id:" << static_cast<int>(message.msgid) << std::endl;
                    //qDebug() << std::cerr << "Unable to decode message from system " << std::dec << static_cast<int>(message.acid) << " with message id:" << static_cast<int>(message.msgid) << std::endl;
                }
            }
            break;
        }
    }
}

void UAS::setHomePosition(double lat, double lon, double alt)
{
    // Send new home position to UAS
    mavlink_gps_set_global_origin_t home;
    home.target_system = uasId;
    home.target_component = 0; // ALL components
    home.latitude = lat*1E7;
    home.longitude = lon*1E7;
    home.altitude = alt*1000;
    qDebug() << "lat:" << home.latitude << " lon:" << home.longitude;
    mavlink_message_t msg;
    mavlink_msg_gps_set_global_origin_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &home);
    sendMessage(msg);
}

void UAS::setLocalOriginAtCurrentGPSPosition()
{
    bool result = false;
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Setting new World Coordinate Frame Origin");
    msgBox.setInformativeText("Do you want to set a new origin? Waypoints defined in the local frame will be shifted in their physical location");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    int ret = msgBox.exec();

    // Close the message box shortly after the click to prevent accidental clicks
    QTimer::singleShot(5000, &msgBox, SLOT(reject()));


    if (ret == QMessageBox::Yes)
    {
        // FIXME MAVLINKV10PORTINGNEEDED
//        mavlink_message_t msg;
//        mavlink_msg_action_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), 0, MAV_ACTION_SET_ORIGIN);
//        // Send message twice to increase chance that it reaches its goal
//        sendMessage(msg);
//        // Wait 5 ms
//        MG::SLEEP::usleep(5000);
//        // Send again
//        sendMessage(msg);
        result = true;
    }
}

void UAS::setLocalPositionSetpoint(float x, float y, float z, float yaw)
{
#ifdef MAVLINK_ENABLED_PIXHAWK
    mavlink_message_t msg;
    mavlink_msg_position_control_setpoint_set_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, 0, x, y, z, yaw);
    sendMessage(msg);
#else
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(z);
    Q_UNUSED(yaw);
#endif
}

void UAS::setLocalPositionOffset(float x, float y, float z, float yaw)
{
#ifdef MAVLINK_ENABLED_PIXHAWK
    mavlink_message_t msg;
    mavlink_msg_position_control_offset_set_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, x, y, z, yaw);
    sendMessage(msg);
#else
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(z);
    Q_UNUSED(yaw);
#endif
}

void UAS::startRadioControlCalibration()
{
    mavlink_message_t msg;
    // Param 1: gyro cal, param 2: mag cal, param 3: pressure cal, Param 4: radio
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, MAV_COMP_ID_IMU, MAV_CMD_PREFLIGHT_CALIBRATION, 1, 0, 0, 0, 1);
    sendMessage(msg);
}

void UAS::startDataRecording()
{
    mavlink_message_t msg;
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, MAV_CMD_DO_CONTROL_VIDEO, 1, -1, -1, -1, 2);
    sendMessage(msg);
}

void UAS::stopDataRecording()
{
    mavlink_message_t msg;
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, MAV_CMD_DO_CONTROL_VIDEO, 1, -1, -1, -1, 0);
    sendMessage(msg);
}

void UAS::startMagnetometerCalibration()
{
    mavlink_message_t msg;
    // Param 1: gyro cal, param 2: mag cal, param 3: pressure cal, Param 4: radio
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, MAV_COMP_ID_IMU, MAV_CMD_PREFLIGHT_CALIBRATION, 1, 0, 1, 0, 0);
    sendMessage(msg);
}

void UAS::startGyroscopeCalibration()
{
    mavlink_message_t msg;
    // Param 1: gyro cal, param 2: mag cal, param 3: pressure cal, Param 4: radio
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, MAV_COMP_ID_IMU, MAV_CMD_PREFLIGHT_CALIBRATION, 1, 1, 0, 0, 0);
    sendMessage(msg);
}

void UAS::startPressureCalibration()
{
    mavlink_message_t msg;
    // Param 1: gyro cal, param 2: mag cal, param 3: pressure cal, Param 4: radio
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, MAV_COMP_ID_IMU, MAV_CMD_PREFLIGHT_CALIBRATION, 1, 0, 0, 1, 0);
    sendMessage(msg);
}

quint64 UAS::getUnixReferenceTime(quint64 time)
{
    // Same as getUnixTime, but does not react to attitudeStamped mode
    if (time == 0)
    {
        //        qDebug() << "XNEW time:" <<QGC::groundTimeMilliseconds();
        return QGC::groundTimeMilliseconds();
    }
    // Check if time is smaller than 40 years,
    // assuming no system without Unix timestamp
    // runs longer than 40 years continuously without
    // reboot. In worst case this will add/subtract the
    // communication delay between GCS and MAV,
    // it will never alter the timestamp in a safety
    // critical way.
    //
    // Calculation:
    // 40 years
    // 365 days
    // 24 hours
    // 60 minutes
    // 60 seconds
    // 1000 milliseconds
    // 1000 microseconds
#ifndef _MSC_VER
    else if (time < 1261440000000000LLU)
#else
        else if (time < 1261440000000000)
#endif
        {
        //        qDebug() << "GEN time:" << time/1000 + onboardTimeOffset;
        if (onboardTimeOffset == 0)
        {
            onboardTimeOffset = QGC::groundTimeMilliseconds() - time/1000;
        }
        return time/1000 + onboardTimeOffset;
    }
    else
    {
        // Time is not zero and larger than 40 years -> has to be
        // a Unix epoch timestamp. Do nothing.
        return time/1000;
    }
}

/**
 * @warning If attitudeStamped is enabled, this function will not actually return the precise time stamp
 *          of this measurement augmented to UNIX time, but will MOVE the timestamp IN TIME to match
 *          the last measured attitude. There is no reason why one would want this, except for
 *          system setups where the onboard clock is not present or broken and datasets should
 *          be collected that are still roughly synchronized. PLEASE NOTE THAT ENABLING ATTITUDE STAMPED
 *          RUINS THE SCIENTIFIC NATURE OF THE CORRECT LOGGING FUNCTIONS OF QGROUNDCONTROL!
 */
quint64 UAS::getUnixTime(quint64 time)
{
    if (attitudeStamped)
    {
        return lastAttitude;
    }
    if (time == 0)
    {
        //        qDebug() << "XNEW time:" <<QGC::groundTimeMilliseconds();
        return QGC::groundTimeMilliseconds();
    }
    // Check if time is smaller than 40 years,
    // assuming no system without Unix timestamp
    // runs longer than 40 years continuously without
    // reboot. In worst case this will add/subtract the
    // communication delay between GCS and MAV,
    // it will never alter the timestamp in a safety
    // critical way.
    //
    // Calculation:
    // 40 years
    // 365 days
    // 24 hours
    // 60 minutes
    // 60 seconds
    // 1000 milliseconds
    // 1000 microseconds
#ifndef _MSC_VER
    else if (time < 1261440000000000LLU)
#else
        else if (time < 1261440000000000)
#endif
        {
        //        qDebug() << "GEN time:" << time/1000 + onboardTimeOffset;
        if (onboardTimeOffset == 0)
        {
            onboardTimeOffset = QGC::groundTimeMilliseconds() - time/1000;
        }
        return time/1000 + onboardTimeOffset;
    }
    else
    {
        // Time is not zero and larger than 40 years -> has to be
        // a Unix epoch timestamp. Do nothing.
        return time/1000;
    }
}

QList<QString> UAS::getParameterNames(int component)
{
    if (parameters.contains(component))
    {
        return parameters.value(component)->keys();
    }
    else
    {
        return QList<QString>();
    }
}

QList<int> UAS::getComponentIds()
{
    return parameters.keys();
}

void UAS::setMode(int mode)
{
    if (mode >= (int)MAV_MODE_PREFLIGHT && mode < (int)MAV_MODE_ENUM_END)
    {
        //this->mode = mode; //no call assignament, update receive message from UAS
        mavlink_message_t msg;
        mavlink_msg_set_mode_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, (uint8_t)uasId, (uint8_t)mode);
        sendMessage(msg);
        qDebug() << "SENDING REQUEST TO SET MODE TO SYSTEM" << uasId << ", REQUEST TO SET MODE " << (uint8_t)mode;
    }
    else
    {
        qDebug() << "uas Mode not assign: " << mode;
    }
}

void UAS::sendMessage(mavlink_message_t message)
{
    // Emit message on all links that are currently connected
    foreach (LinkInterface* link, *links)
    {
        if (link)
        {
            sendMessage(link, message);
        }
        else
        {
            // Remove from list
            links->removeAt(links->indexOf(link));
        }
    }
}

void UAS::forwardMessage(mavlink_message_t message)
{
    // Emit message on all links that are currently connected
    QList<LinkInterface*>link_list = LinkManager::instance()->getLinksForProtocol(mavlink);

    foreach(LinkInterface* link, link_list)
    {
        if (link)
        {
            SerialLink* serial = dynamic_cast<SerialLink*>(link);
            if(serial != 0)
            {
                for(int i=0; i<links->size(); i++)
                {
                    if(serial != links->at(i))
                    {
                        qDebug()<<"Antenna tracking: Forwarding Over link: "<<serial->getName()<<" "<<serial;
                        sendMessage(serial, message);
                    }
                }
            }
        }
    }
}

void UAS::sendMessage(LinkInterface* link, mavlink_message_t message)
{
    if(!link) return;
    // Create buffer
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    // Write message into buffer, prepending start sign
    int len = mavlink_msg_to_send_buffer(buffer, &message);
    mavlink_finalize_message_chan(&message, mavlink->getSystemId(), mavlink->getComponentId(), link->getId(), message.len);
    // If link is connected
    if (link->isConnected())
    {
        // Send the portion of the buffer now occupied by the message
        link->writeBytes((const char*)buffer, len);
    }
}

/**
 * @param value battery voltage
 */
float UAS::filterVoltage(float value) const
{
    return lpVoltage * 0.7f + value * 0.3f;
}

QString UAS::getNavModeText(int mode)
{
    switch (mode)
    {
    case MAV_FLIGHT_MODE_PREFLIGHT:
        return QString("PREFLIGHT");
        break;
    case MAV_FLIGHT_MODE_MANUAL:
        return QString("MANUAL");
        break;
    case MAV_FLIGHT_MODE_AUTO_TAKEOFF:
        return QString("TAKEOFF");
        break;
    case MAV_FLIGHT_MODE_AUTO_HOLD:
        return QString("HOLDING");
        break;
    case MAV_FLIGHT_MODE_AUTO_MISSION:
        return QString("MISSION");
        break;
    case MAV_FLIGHT_MODE_AUTO_VECTOR:
        return QString("VECTOR");
        break;
    case MAV_FLIGHT_MODE_AUTO_RETURNING:
        return QString("RETURNING");
        break;
    case MAV_FLIGHT_MODE_AUTO_LANDING:
        return QString("LANDING");
        break;
    case MAV_FLIGHT_MODE_AUTO_LOST:
        return QString("LOST");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_RATES_ACRO:
        return QString("S: RATE/ACRO");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_LEVELING:
        return QString("S: LEVELING");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_ROLL_PITCH_ABSOLUTE:
        return QString("S: R/P ABS");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_ROLL_YAW_ALTITUDE:
        return QString("S: R/Y ALT");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_ROLL_PITCH_YAW_ALTITUDE:
        return QString("S: R/P/Y ALT");
        break;
    case MAV_FLIGHT_MODE_STABILIZE_CURSOR_CONTROL:
        return QString("S: CURSOR");
        break;
    default:
        return QString("UNKNOWN");
    }
}

void UAS::getStatusForCode(int statusCode, QString& uasState, QString& stateDescription)
{
    switch (statusCode)
    {
    case MAV_STATE_UNINIT:
        uasState = tr("UNINIT");
        stateDescription = tr("Unitialized, booting up.");
        break;
    case MAV_STATE_BOOT:
        uasState = tr("BOOT");
        stateDescription = tr("Booting system, please wait.");
        break;
    case MAV_STATE_CALIBRATING:
        uasState = tr("CALIBRATING");
        stateDescription = tr("Calibrating sensors, please wait.");
        break;
    case MAV_STATE_ACTIVE:
        uasState = tr("ACTIVE");
        stateDescription = tr("Active, normal operation.");
        break;
    case MAV_STATE_STANDBY:
        uasState = tr("STANDBY");
        stateDescription = tr("Standby mode, ready for liftoff.");
        break;
    case MAV_STATE_CRITICAL:
        uasState = tr("CRITICAL");
        stateDescription = tr("FAILURE: Continuing operation.");
        break;
    case MAV_STATE_EMERGENCY:
        uasState = tr("EMERGENCY");
        stateDescription = tr("EMERGENCY: Land Immediately!");
        break;
        //case MAV_STATE_HILSIM:
        //uasState = tr("HIL SIM");
        //stateDescription = tr("HIL Simulation, Sensors read from SIM");
        //break;

    case MAV_STATE_POWEROFF:
        uasState = tr("SHUTDOWN");
        stateDescription = tr("Powering off system.");
        break;

    default:
        uasState = tr("UNKNOWN");
        stateDescription = tr("Unknown system state");
        break;
    }
}

QImage UAS::getImage()
{
#ifdef MAVLINK_ENABLED_PIXHAWK

    qDebug() << "IMAGE TYPE:" << imageType;

    // RAW greyscale
    if (imageType == MAVLINK_DATA_STREAM_IMG_RAW8U)
    {
        // TODO FIXME Fabian
        // RAW hardcoded to 22x22
        int imgWidth = 22;
        int imgHeight = 22;
        int imgColors = 255;
        //const int headerSize = 15;

        // Construct PGM header
        QString header("P5\n%1 %2\n%3\n");
        header = header.arg(imgWidth).arg(imgHeight).arg(imgColors);

        QByteArray tmpImage(header.toStdString().c_str(), header.toStdString().size());
        tmpImage.append(imageRecBuffer);

        //qDebug() << "IMAGE SIZE:" << tmpImage.size() << "HEADER SIZE: (15):" << header.size() << "HEADER: " << header;

        if (imageRecBuffer.isNull())
        {
            qDebug()<< "could not convertToPGM()";
            return QImage();
        }

        if (!image.loadFromData(tmpImage, "PGM"))
        {
            qDebug()<< "could not create extracted image";
            return QImage();
        }

    }
    // BMP with header
    else if (imageType == MAVLINK_DATA_STREAM_IMG_BMP ||
             imageType == MAVLINK_DATA_STREAM_IMG_JPEG ||
             imageType == MAVLINK_DATA_STREAM_IMG_PGM ||
             imageType == MAVLINK_DATA_STREAM_IMG_PNG)
    {
       if (!image.loadFromData(imageRecBuffer))
        {
           qDebug() << "Loading data from image buffer failed!";
        }
    }
    // Restart statemachine
    imagePacketsArrived = 0;
    //imageRecBuffer.clear();
    return image;
#else
	return QImage();
#endif

}

void UAS::requestImage()
{
#ifdef MAVLINK_ENABLED_PIXHAWK
    qDebug() << "trying to get an image from the uas...";

    // check if there is already an image transmission going on
    if (imagePacketsArrived == 0)
    {
        mavlink_message_t msg;
        mavlink_msg_data_transmission_handshake_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, DATA_TYPE_JPEG_IMAGE, 0, 0, 0, 50);
        sendMessage(msg);
    }
#endif
}


/* MANAGEMENT */

/*
 *
 * @return The uptime in milliseconds
 *
 **/
quint64 UAS::getUptime() const
{
    if(startTime == 0)
    {
        return 0;
    }
    else
    {
        return MG::TIME::getGroundTimeNow() - startTime;
    }
}

int UAS::getCommunicationStatus() const
{
    return commStatus;
}

void UAS::requestParameters()
{
    mavlink_message_t msg;
    mavlink_msg_param_request_list_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), 25);
    sendMessage(msg);
}

void UAS::writeParametersToStorage()
{
    mavlink_message_t msg;
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, MAV_CMD_PREFLIGHT_STORAGE, 1, 1, -1, -1, -1);
    sendMessage(msg);
}

void UAS::readParametersFromStorage()
{
    mavlink_message_t msg;
    mavlink_msg_command_short_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, uasId, 0, MAV_CMD_PREFLIGHT_STORAGE, 1, 0, -1, -1, -1);
    sendMessage(msg);
}

void UAS::enableAllDataTransmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    // 0 is a magic ID and will enable/disable the standard message set except for heartbeat
    stream.req_stream_id = MAV_DATA_STREAM_ALL;
    // Select the update rate in Hz the message should be send
    // All messages will be send with their default rate
    // TODO: use 0 to turn off and get rid of enable/disable? will require
    //  a different magic flag for turning on defaults, possibly something really high like 1111 ?
    stream.req_message_rate = 0;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableRawSensorDataTransmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_RAW_SENSORS;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableExtendedSystemStatusTransmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_EXTENDED_STATUS;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableRCChannelDataTransmission(int rate)
{
#if defined(MAVLINK_ENABLED_UALBERTA_MESSAGES)
    mavlink_message_t msg;
    mavlink_msg_request_rc_channels_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, enabled);
    sendMessage(msg);
#else
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_RC_CHANNELS;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
#endif
}

void UAS::enableRawControllerDataTransmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_RAW_CONTROLLER;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

//void UAS::enableRawSensorFusionTransmission(int rate)
//{
//    // Buffers to write data to
//    mavlink_message_t msg;
//    mavlink_request_data_stream_t stream;
//    // Select the message to request from now on
//    stream.req_stream_id = MAV_DATA_STREAM_RAW_SENSOR_FUSION;
//    // Select the update rate in Hz the message should be send
//    stream.req_message_rate = rate;
//    // Start / stop the message
//    stream.start_stop = (rate) ? 1 : 0;
//    // The system which should take this command
//    stream.target_system = uasId;
//    // The component / subsystem which should take this command
//    stream.target_component = 0;
//    // Encode and send the message
//    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
//}

void UAS::enablePositionTransmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_POSITION;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableExtra1Transmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_EXTRA1;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableExtra2Transmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_EXTRA2;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

void UAS::enableExtra3Transmission(int rate)
{
    // Buffers to write data to
    mavlink_message_t msg;
    mavlink_request_data_stream_t stream;
    // Select the message to request from now on
    stream.req_stream_id = MAV_DATA_STREAM_EXTRA3;
    // Select the update rate in Hz the message should be send
    stream.req_message_rate = rate;
    // Start / stop the message
    stream.start_stop = (rate) ? 1 : 0;
    // The system which should take this command
    stream.target_system = uasId;
    // The component / subsystem which should take this command
    stream.target_component = 0;
    // Encode and send the message
    mavlink_msg_request_data_stream_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &stream);
    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

/**
 * Set a parameter value onboard
 *
 * @param component The component to set the parameter
 * @param id Name of the parameter
 * @param value Parameter value
 */
void UAS::setParameter(const int component, const QString& id, const float value)
{
    if (!id.isNull())
    {
        mavlink_message_t msg;
        mavlink_param_set_t p;
        p.param_value = value;
        p.target_system = (uint8_t)uasId;
        p.target_component = (uint8_t)component;

        // Copy string into buffer, ensuring not to exceed the buffer size
        for (unsigned int i = 0; i < sizeof(p.param_id); i++)
        {
            // String characters
            if ((int)i < id.length() && i < (sizeof(p.param_id) - 1))
            {
                p.param_id[i] = id.toAscii()[i];
            }
            //        // Null termination at end of string or end of buffer
            //        else if ((int)i == id.length() || i == (sizeof(p.param_id) - 1))
            //        {
            //            p.param_id[i] = '\0';
            //        }
            // Zero fill
            else
            {
                p.param_id[i] = 0;
            }
        }
        mavlink_msg_param_set_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &p);
        sendMessage(msg);
    }
}

void UAS::requestParameter(int component, int parameter)
{
    mavlink_message_t msg;
    mavlink_param_request_read_t read;
    read.param_index = parameter;
    read.target_system = uasId;
    read.target_component = component;
    mavlink_msg_param_request_read_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &read);
    sendMessage(msg);
    qDebug() << __FILE__ << __LINE__ << "REQUESTING PARAM RETRANSMISSION FROM COMPONENT" << component << "FOR PARAM ID" << parameter;
}

void UAS::setSystemType(int systemType)
{
    type = systemType;
    // If the airframe is still generic, change it to a close default type
    if (airframe == 0)
    {
        switch (systemType)
        {
        case MAV_TYPE_FIXED_WING:
            airframe = QGC_AIRFRAME_EASYSTAR;
            break;
        case MAV_TYPE_QUADROTOR:
            airframe = QGC_AIRFRAME_MIKROKOPTER;
            break;
        }
    }
    emit systemSpecsChanged(uasId);
}

void UAS::setUASName(const QString& name)
{
    this->name = name;
    writeSettings();
    emit nameChanged(name);
    emit systemSpecsChanged(uasId);
}

void UAS::executeCommand(MAV_CMD command)
{
    mavlink_message_t msg;
    mavlink_command_short_t cmd;
    cmd.command = (uint8_t)command;
    cmd.confirmation = 0;
    cmd.param1 = 0.0f;
    cmd.param2 = 0.0f;
    cmd.param3 = 0.0f;
    cmd.param4 = 0.0f;
    cmd.target_system = uasId;
    cmd.target_component = 0;
    mavlink_msg_command_short_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &cmd);
    sendMessage(msg);
}

void UAS::executeCommand(MAV_CMD command, int confirmation, float param1, float param2, float param3, float param4, int component)
{
    mavlink_message_t msg;
    mavlink_command_short_t cmd;
    cmd.command = (uint8_t)command;
    cmd.confirmation = confirmation;
    cmd.param1 = param1;
    cmd.param2 = param2;
    cmd.param3 = param3;
    cmd.param4 = param4;
    cmd.target_system = uasId;
    cmd.target_component = component;
    mavlink_msg_command_short_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &cmd);
    sendMessage(msg);
}

void UAS::executeCommand(MAV_CMD command, int confirmation, float param1, float param2, float param3, float param4, float param5, float param6, float param7, int component)
{
    mavlink_message_t msg;
    mavlink_command_long_t cmd;
    cmd.command = (uint8_t)command;
    cmd.confirmation = confirmation;
    cmd.param1 = param1;
    cmd.param2 = param2;
    cmd.param3 = param3;
    cmd.param4 = param4;
    cmd.param5 = param5;
    cmd.param6 = param6;
    cmd.param7 = param7;
    cmd.target_system = uasId;
    cmd.target_component = component;
    mavlink_msg_command_long_encode(mavlink->getSystemId(), mavlink->getComponentId(), &msg, &cmd);
    sendMessage(msg);
}

/**
 * Launches the system
 *
 **/
void UAS::launch()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    mavlink_message_t msg;
//    // TODO Replace MG System ID with static function call and allow to change ID in GUI
//    mavlink_msg_action_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), MAV_COMP_ID_IMU, (uint8_t)MAV_ACTION_TAKEOFF);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
}

/**
 * Depending on the UAS, this might make the rotors of a helicopter spinning
 *
 **/
void UAS::armSystem()
{
    mavlink_message_t msg;
    mavlink_msg_set_safety_mode_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), MAV_SAFETY_ARMED);
    sendMessage(msg);
}

/**
 * @warning Depending on the UAS, this might completely stop all motors.
 *
 **/
void UAS::disarmSystem()
{
    mavlink_message_t msg;
    mavlink_msg_set_safety_mode_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), MAV_SAFETY_DISARMED);
    sendMessage(msg);
}

void UAS::setManualControlCommands(double roll, double pitch, double yaw, double thrust)
{
    // Scale values
    double rollPitchScaling = 0.2f;
    double yawScaling = 0.5f;
    double thrustScaling = 1.0f;

    manualRollAngle = roll * rollPitchScaling;
    manualPitchAngle = pitch * rollPitchScaling;
    manualYawAngle = yaw * yawScaling;
    manualThrust = thrust * thrustScaling;

    if(mode == (int)MAV_MODE_MANUAL)
    {
        mavlink_message_t message;
        mavlink_msg_manual_control_pack(mavlink->getSystemId(), mavlink->getComponentId(), &message, this->uasId, (float)manualRollAngle, (float)manualPitchAngle, (float)manualYawAngle, (float)manualThrust, controlRollManual, controlPitchManual, controlYawManual, controlThrustManual);
        sendMessage(message);
        qDebug() << __FILE__ << __LINE__ << ": SENT MANUAL CONTROL MESSAGE: roll" << manualRollAngle << " pitch: " << manualPitchAngle << " yaw: " << manualYawAngle << " thrust: " << manualThrust;

        emit attitudeThrustSetPointChanged(this, roll, pitch, yaw, thrust, MG::TIME::getGroundTimeNow());
    }
    else
    {
        qDebug() << "JOYSTICK/MANUAL CONTROL: IGNORING COMMANDS: Set mode to MANUAL to send joystick commands first";
    }
}

int UAS::getSystemType()
{
    return this->type;
}

void UAS::receiveButton(int buttonIndex)
{
    switch (buttonIndex)
    {
    case 0:

        break;
    case 1:

        break;
    default:

        break;
    }
    //    qDebug() << __FILE__ << __LINE__ << ": Received button clicked signal (button # is: " << buttonIndex << "), UNIMPLEMENTED IN MAVLINK!";

}



void UAS::halt()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    mavlink_message_t msg;
//    // TODO Replace MG System ID with static function call and allow to change ID in GUI
//    mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU, (int)MAV_ACTION_HALT);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
}

void UAS::go()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    mavlink_message_t msg;
//    // TODO Replace MG System ID with static function call and allow to change ID in GUI
//    mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU,  (int)MAV_ACTION_CONTINUE);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
}

/** Order the robot to return home / to land on the runway **/
void UAS::home()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    mavlink_message_t msg;
//    // TODO Replace MG System ID with static function call and allow to change ID in GUI
//    mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU,  (int)MAV_ACTION_RETURN);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
}

/**
 * The MAV starts the emergency landing procedure. The behaviour depends on the onboard implementation
 * and might differ between systems.
 */
void UAS::emergencySTOP()
{
//    mavlink_message_t msg;
//    // TODO Replace MG System ID with static function call and allow to change ID in GUI
//    // FIXME MAVLINKV10PORTINGNEEDED
//    //mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU, (int)MAV_ACTION_EMCY_LAND);
//    // Send message twice to increase chance of reception
//    sendMessage(msg);
//    sendMessage(msg);
}

/**
 * Shut down this mav - All onboard systems are immediately shut down (e.g. the main power line is cut).
 * @warning This might lead to a crash
 *
 * The command will not be executed until emergencyKILLConfirm is issues immediately afterwards
 */
bool UAS::emergencyKILL()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    bool result = false;
//    QMessageBox msgBox;
//    msgBox.setIcon(QMessageBox::Critical);
//    msgBox.setText("EMERGENCY: KILL ALL MOTORS ON UAS");
//    msgBox.setInformativeText("Do you want to cut power on all systems?");
//    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
//    msgBox.setDefaultButton(QMessageBox::Cancel);
//    int ret = msgBox.exec();

//    // Close the message box shortly after the click to prevent accidental clicks
//    QTimer::singleShot(5000, &msgBox, SLOT(reject()));


//    if (ret == QMessageBox::Yes)
//    {
//        mavlink_message_t msg;
//        // TODO Replace MG System ID with static function call and allow to change ID in GUI
//        mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU, (int)MAV_ACTION_EMCY_KILL);
//        // Send message twice to increase chance of reception
//        sendMessage(msg);
//        sendMessage(msg);
//        result = true;
//    }
//    return result;
    return false;
}

void UAS::enableHil(bool enable)
{
    // Connect Flight Gear Link
    if (enable)
    {
        startHil();
    }
    else
    {
        stopHil();
    }
}

/**
* @param time_us Timestamp (microseconds since UNIX epoch or microseconds since system boot)
* @param roll Roll angle (rad)
* @param pitch Pitch angle (rad)
* @param yaw Yaw angle (rad)
* @param rollspeed Roll angular speed (rad/s)
* @param pitchspeed Pitch angular speed (rad/s)
* @param yawspeed Yaw angular speed (rad/s)
* @param lat Latitude, expressed as * 1E7
* @param lon Longitude, expressed as * 1E7
* @param alt Altitude in meters, expressed as * 1000 (millimeters)
* @param vx Ground X Speed (Latitude), expressed as m/s * 100
* @param vy Ground Y Speed (Longitude), expressed as m/s * 100
* @param vz Ground Z Speed (Altitude), expressed as m/s * 100
* @param xacc X acceleration (mg)
* @param yacc Y acceleration (mg)
* @param zacc Z acceleration (mg)
*/
void UAS::sendHilState(uint64_t time_us, float roll, float pitch, float yaw, float rollspeed,
                    float pitchspeed, float yawspeed, int32_t lat, int32_t lon, int32_t alt,
                    int16_t vx, int16_t vy, int16_t vz, int16_t xacc, int16_t yacc, int16_t zacc)
{
    mavlink_message_t msg;
    mavlink_msg_hil_state_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, time_us, roll, pitch, yaw, rollspeed, pitchspeed, yawspeed, lat, lon, alt, vx, vy, vz, xacc, yacc, zacc);
    sendMessage(msg);
}


void UAS::startHil()
{
    // Connect Flight Gear Link
    simulation->connectSimulation();
    mavlink_message_t msg;
    mavlink_msg_set_safety_mode_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), MAV_SAFETY_HIL);
    sendMessage(msg);
}

void UAS::stopHil()
{
    simulation->disconnectSimulation();
    mavlink_message_t msg;
    mavlink_msg_set_safety_mode_pack(mavlink->getSystemId(), mavlink->getComponentId(), &msg, this->getUASID(), MAV_SAFETY_DISARMED);
    sendMessage(msg);
}


void UAS::shutdown()
{
    // FIXME MAVLINKV10PORTINGNEEDED
//    bool result = false;
//    QMessageBox msgBox;
//    msgBox.setIcon(QMessageBox::Critical);
//    msgBox.setText("Shutting down the UAS");
//    msgBox.setInformativeText("Do you want to shut down the onboard computer?");

//    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
//    msgBox.setDefaultButton(QMessageBox::Cancel);
//    int ret = msgBox.exec();

//    // Close the message box shortly after the click to prevent accidental clicks
//    QTimer::singleShot(5000, &msgBox, SLOT(reject()));

//    if (ret == QMessageBox::Yes)
//    {
//        // If the active UAS is set, execute command
//        mavlink_message_t msg;
//        // TODO Replace MG System ID with static function call and allow to change ID in GUI
//        mavlink_msg_action_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, this->getUASID(), MAV_COMP_ID_IMU,(int)MAV_ACTION_SHUTDOWN);
//        // Send message twice to increase chance of reception
//        sendMessage(msg);
//        sendMessage(msg);

//        result = true;
//    }
}

void UAS::setTargetPosition(float x, float y, float z, float yaw)
{
    mavlink_message_t msg;
    mavlink_msg_position_target_pack(MG::SYSTEM::ID, MG::SYSTEM::COMPID, &msg, x, y, z, yaw);

    // Send message twice to increase chance of reception
    sendMessage(msg);
    sendMessage(msg);
}

/**
 * @return The name of this system as string in human-readable form
 */
QString UAS::getUASName(void) const
{
    QString result;
    if (name == "")
    {
        result = tr("MAV ") + result.sprintf("%03d", getUASID());
    }
    else
    {
        result = name;
    }
    return result;
}

const QString& UAS::getShortState() const
{
    return shortStateText;
}

QString UAS::getShortModeTextFor(int id)
{
    QString mode;

    switch (id) {
    case (uint8_t)MAV_MODE_PREFLIGHT:
        mode = "PREFLIGHT";
        break;
    case (uint8_t)MAV_MODE_MANUAL:
        mode = "MANUAL";
        break;
    case (uint8_t)MAV_MODE_AUTO:
        mode = "AUTO";
        break;
    case (uint8_t)MAV_MODE_GUIDED:
        mode = "GUIDED";
        break;
    case (uint8_t)MAV_MODE_STABILIZE:
        mode = "STABILIZED";
        break;
    case (uint8_t)MAV_MODE_TEST:
        mode = "STABILIZED";
        break;
    default:
        mode = "UNKNOWN";
        break;
    }

    return mode;
}

const QString& UAS::getShortMode() const
{
    return shortModeText;
}

void UAS::addLink(LinkInterface* link)
{
    if (!links->contains(link))
    {
        links->append(link);
        connect(link, SIGNAL(destroyed(QObject*)), this, SLOT(removeLink(QObject*)));
    }
}

void UAS::removeLink(QObject* object)
{
    LinkInterface* link = dynamic_cast<LinkInterface*>(object);
    if (link)
    {
        links->removeAt(links->indexOf(link));
    }
}

/**
 * @brief Get the links associated with this robot
 *
 **/
QList<LinkInterface*>* UAS::getLinks()
{
    return links;
}



void UAS::setBattery(BatteryType type, int cells)
{
    this->batteryType = type;
    this->cells = cells;
    switch (batteryType)
    {
    case NICD:
        break;
    case NIMH:
        break;
    case LIION:
        break;
    case LIPOLY:
        fullVoltage = this->cells * UAS::lipoFull;
        emptyVoltage = this->cells * UAS::lipoEmpty;
        break;
    case LIFE:
        break;
    case AGZN:
        break;
    }
}

void UAS::setBatterySpecs(const QString& specs)
{
    if (specs.length() == 0 || specs.contains("%"))
    {
        batteryRemainingEstimateEnabled = false;
        bool ok;
        QString percent = specs;
        percent = percent.remove("%");
        float temp = percent.toFloat(&ok);
        if (ok)
        {
            warnLevelPercent = temp;
        }
        else
        {
            emit textMessageReceived(0, 0, 0, "Could not set battery options, format is wrong");
        }
    }
    else
    {
        batteryRemainingEstimateEnabled = true;
        QString stringList = specs;
        stringList = stringList.remove("V");
        stringList = stringList.remove("v");
        QStringList parts = stringList.split(",");
        if (parts.length() == 3)
        {
            float temp;
            bool ok;
            // Get the empty voltage
            temp = parts.at(0).toFloat(&ok);
            if (ok) emptyVoltage = temp;
            // Get the warning voltage
            temp = parts.at(1).toFloat(&ok);
            if (ok) warnVoltage = temp;
            // Get the full voltage
            temp = parts.at(2).toFloat(&ok);
            if (ok) fullVoltage = temp;
        }
        else
        {
            emit textMessageReceived(0, 0, 0, "Could not set battery options, format is wrong");
        }
    }
}

QString UAS::getBatterySpecs()
{
    if (batteryRemainingEstimateEnabled)
    {
        return QString("%1V,%2V,%3V").arg(emptyVoltage).arg(warnVoltage).arg(fullVoltage);
    }
    else
    {
        return QString("%1%").arg(warnLevelPercent);
    }
}

int UAS::calculateTimeRemaining()
{
    quint64 dt = MG::TIME::getGroundTimeNow() - startTime;
    double seconds = dt / 1000.0f;
    double voltDifference = startVoltage - currentVoltage;
    if (voltDifference <= 0) voltDifference = 0.00000000001f;
    double dischargePerSecond = voltDifference / seconds;
    int remaining = static_cast<int>((currentVoltage - emptyVoltage) / dischargePerSecond);
    // Can never be below 0
    if (remaining < 0) remaining = 0;
    return remaining;
}

/**
 * @return charge level in percent - 0 - 100
 */
float UAS::getChargeLevel()
{
    if (batteryRemainingEstimateEnabled)
    {
        if (lpVoltage < emptyVoltage)
        {
            chargeLevel = 0.0f;
        }
        else if (lpVoltage > fullVoltage)
        {
            chargeLevel = 100.0f;
        }
        else
        {
            chargeLevel = 100.0f * ((lpVoltage - emptyVoltage)/(fullVoltage - emptyVoltage));
        }
    }
    return chargeLevel;
}

void UAS::startLowBattAlarm()
{
    if (!lowBattAlarm)
    {
        GAudioOutput::instance()->alert(tr("SYSTEM %1 HAS LOW BATTERY").arg(getUASName()));
        QTimer::singleShot(2500, GAudioOutput::instance(), SLOT(startEmergency()));
        lowBattAlarm = true;
    }
}

void UAS::stopLowBattAlarm()
{
    if (lowBattAlarm)
    {
        GAudioOutput::instance()->stopEmergency();
        lowBattAlarm = false;
    }
}
