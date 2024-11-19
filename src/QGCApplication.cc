/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


/**
 * @file
 *   @brief Implementation of class QGCApplication
 *
 *   @author Lorenz Meier <mavteam@student.ethz.ch>
 *
 */

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtGui/QFontDatabase>
#include <QtGui/QIcon>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickImageProvider>
#include <QtQuickControls2/QQuickStyle>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlApplicationEngine>
#include <QTimer>
#include <QtMqtt/QtMqtt>
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttMessage>
#include <QtMqtt/QMqttSubscription>
#include <QJsonObject>
#include <QUuid>
#include <QProcess>
#include <QCoreApplication>
//#include <GStreamer.h>
#include <gst/gst.h>
#include <thread>
#include <gst/app/gstappsink.h>


#include "Audio/AudioOutput.h"
#include "QGCConfig.h"
#include "QGCApplication.h"
#include "CmdLineOptParser.h"
#include "UDPLink.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "QGCPalette.h"
#include "QGCMapPalette.h"
#include "QGCLoggingCategory.h"
#include "ParameterEditorController.h"
#include "ESP8266ComponentController.h"
#include "ScreenToolsController.h"
#include "QGCFileDialogController.h"
#include "RCChannelMonitorController.h"
#include "SyslinkComponentController.h"
#include "AutoPilotPlugin.h"
#include "VehicleComponent.h"
#include "FirmwarePluginManager.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "JoystickConfigController.h"
#include "JoystickManager.h"
#include "QmlObjectListModel.h"
#include "QGCGeoBoundingCube.h"
#include "MissionManager.h"
#include "QGroundControlQmlGlobal.h"
#include "FlightPathSegment.h"
#include "PlanMasterController.h"
#include "VideoManager.h"
#include "LogDownloadController.h"
#if !defined(QGC_DISABLE_MAVLINK_INSPECTOR)
#include "MAVLinkInspectorController.h"
#endif
#include "HorizontalFactValueGrid.h"
#include "InstrumentValueData.h"
#include "AppMessages.h"
#include "MissionCommandTree.h"
#include "QGCMapPolygon.h"
#include "QGCMapCircle.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
#include "QGCCorePlugin.h"
#include "QGCCameraManager.h"
#include "CameraCalc.h"
#include "VisualMissionItem.h"
#include "EditPositionDialogController.h"
#include "FactGroup.h"
#include "FactPanelController.h"
#include "FactValueSliderListModel.h"
#include "ShapeFileHelper.h"
#include "QGCFileDownload.h"
#include "MAVLinkConsoleController.h"
#include "MAVLinkChartController.h"
#include "GeoTagController.h"
#include "LogReplayLink.h"
#include "VehicleObjectAvoidance.h"
#include "TrajectoryPoints.h"
#include "RCToParamDialogController.h"
#include "QGCImageProvider.h"
#include "TerrainProfile.h"
#include "ToolStripAction.h"
#include "ToolStripActionList.h"
#include "VehicleLinkManager.h"
#include "VehicleCameraControl.h"
#include "Autotune.h"
#include "RemoteIDManager.h"
#include "CustomAction.h"
#include "CustomActionManager.h"
#include "AudioOutput.h"
#include "FollowMe.h"
#include "JsonHelper.h"
#include "VehicleBatteryFactGroup.h"
// #ifdef QGC_VIEWER3D
#include "Viewer3DManager.h"
// #endif
#include "GimbalController.h"
#ifndef NO_SERIAL_LINK
#include "FirmwareUpgradeController.h"
#include "SerialLink.h"
#endif

#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
#include <unistd.h>
#include <sys/types.h>
#endif
#endif

QGC_LOGGING_CATEGORY(QGCApplicationLog, "qgc.qgcapplication")

#ifndef HAVE_ENUM_MAV_CMD
#define HAVE_ENUM_MAV_CMD
typedef enum MAV_CMD
{
   MAV_CMD_NAV_WAYPOINT=16, /* Navigate to waypoint. This is intended for use in missions (for guided commands outside of missions use MAV_CMD_DO_REPOSITION). |Hold time. (ignored by fixed wing, time to stay at waypoint for rotary wing)| Acceptance radius (if the sphere with this radius is hit, the waypoint counts as reached)| 0 to pass through the WP, if > 0 radius to pass by WP. Positive value for clockwise orbit, negative value for counter-clockwise orbit. Allows trajectory control.| Desired yaw angle at waypoint (rotary wing). NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_LOITER_UNLIM=17, /* Loiter around this waypoint an unlimited amount of time |Empty| Empty| Loiter radius around waypoint for forward-only moving vehicles (not multicopters). If positive loiter clockwise, else counter-clockwise| Desired yaw angle. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_LOITER_TURNS=18, /* Loiter around this waypoint for X turns |Number of turns.| Leave loiter circle only once heading towards the next waypoint (0 = False)| Loiter radius around waypoint for forward-only moving vehicles (not multicopters). If positive loiter clockwise, else counter-clockwise| Loiter circle exit location and/or path to next waypoint ("xtrack") for forward-only moving vehicles (not multicopters). 0 for the vehicle to converge towards the center xtrack when it leaves the loiter (the line between the centers of the current and next waypoint), 1 to converge to the direct line between the location that the vehicle exits the loiter radius and the next waypoint. Otherwise the angle (in degrees) between the tangent of the loiter circle and the center xtrack at which the vehicle must leave the loiter (and converge to the center xtrack). NaN to use the current system default xtrack behaviour.| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_LOITER_TIME=19, /* Loiter at the specified latitude, longitude and altitude for a certain amount of time. Multicopter vehicles stop at the point (within a vehicle-specific acceptance radius). Forward-only moving vehicles (e.g. fixed-wing) circle the point with the specified radius/direction. If the Heading Required parameter (2) is non-zero forward moving aircraft will only leave the loiter circle once heading towards the next waypoint. |Loiter time (only starts once Lat, Lon and Alt is reached).| Leave loiter circle only once heading towards the next waypoint (0 = False)| Loiter radius around waypoint for forward-only moving vehicles (not multicopters). If positive loiter clockwise, else counter-clockwise.| Loiter circle exit location and/or path to next waypoint ("xtrack") for forward-only moving vehicles (not multicopters). 0 for the vehicle to converge towards the center xtrack when it leaves the loiter (the line between the centers of the current and next waypoint), 1 to converge to the direct line between the location that the vehicle exits the loiter radius and the next waypoint. Otherwise the angle (in degrees) between the tangent of the loiter circle and the center xtrack at which the vehicle must leave the loiter (and converge to the center xtrack). NaN to use the current system default xtrack behaviour.| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_RETURN_TO_LAUNCH=20, /* Return to launch location |Empty| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_NAV_LAND=21, /* Land at location. |Minimum target altitude if landing is aborted (0 = undefined/use system default).| Precision land mode.| Empty.| Desired yaw angle. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude.| Longitude.| Landing altitude (ground level in current frame).|  */
   MAV_CMD_NAV_TAKEOFF=22, /* Takeoff from ground / hand. Vehicles that support multiple takeoff modes (e.g. VTOL quadplane) should take off using the currently configured mode. |Minimum pitch (if airspeed sensor present), desired pitch without sensor| Empty| Empty| Yaw angle (if magnetometer present), ignored without magnetometer. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_LAND_LOCAL=23, /* Land at local position (local frame only) |Landing target number (if available)| Maximum accepted offset from desired landing position - computed magnitude from spherical coordinates: d = sqrt(x^2 + y^2 + z^2), which gives the maximum accepted distance between the desired landing position and the position where the vehicle is about to land| Landing descend rate| Desired yaw angle| Y-axis position| X-axis position| Z-axis / ground level position|  */
   MAV_CMD_NAV_TAKEOFF_LOCAL=24, /* Takeoff from local position (local frame only) |Minimum pitch (if airspeed sensor present), desired pitch without sensor| Empty| Takeoff ascend rate| Yaw angle (if magnetometer or another yaw estimation source present), ignored without one of these| Y-axis position| X-axis position| Z-axis position|  */
   MAV_CMD_NAV_FOLLOW=25, /* Vehicle following, i.e. this waypoint represents the position of a moving vehicle |Following logic to use (e.g. loitering or sinusoidal following) - depends on specific autopilot implementation| Ground speed of vehicle to be followed| Radius around waypoint. If positive loiter clockwise, else counter-clockwise| Desired yaw angle.| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_CONTINUE_AND_CHANGE_ALT=30, /* Continue on the current course and climb/descend to specified altitude.  When the altitude is reached continue to the next command (i.e., don't proceed to the next command until the desired altitude is reached. |Climb or Descend (0 = Neutral, command completes when within 5m of this command's altitude, 1 = Climbing, command completes when at or above this command's altitude, 2 = Descending, command completes when at or below this command's altitude.| Empty| Empty| Empty| Empty| Empty| Desired altitude|  */
   MAV_CMD_NAV_LOITER_TO_ALT=31, /* Begin loiter at the specified Latitude and Longitude.  If Lat=Lon=0, then loiter at the current position.  Don't consider the navigation command complete (don't leave loiter) until the altitude has been reached. Additionally, if the Heading Required parameter is non-zero the aircraft will not leave the loiter until heading toward the next waypoint. |Leave loiter circle only once heading towards the next waypoint (0 = False)| Loiter radius around waypoint for forward-only moving vehicles (not multicopters). If positive loiter clockwise, negative counter-clockwise, 0 means no change to standard loiter.| Empty| Loiter circle exit location and/or path to next waypoint ("xtrack") for forward-only moving vehicles (not multicopters). 0 for the vehicle to converge towards the center xtrack when it leaves the loiter (the line between the centers of the current and next waypoint), 1 to converge to the direct line between the location that the vehicle exits the loiter radius and the next waypoint. Otherwise the angle (in degrees) between the tangent of the loiter circle and the center xtrack at which the vehicle must leave the loiter (and converge to the center xtrack). NaN to use the current system default xtrack behaviour.| Latitude| Longitude| Altitude|  */
   MAV_CMD_DO_FOLLOW=32, /* Begin following a target |System ID (of the FOLLOW_TARGET beacon). Send 0 to disable follow-me and return to the default position hold mode.| Reserved| Reserved| Altitude mode: 0: Keep current altitude, 1: keep altitude difference to target, 2: go to a fixed altitude above home.| Altitude above home. (used if mode=2)| Reserved| Time to land in which the MAV should go to the default position hold mode after a message RX timeout.|  */
   MAV_CMD_DO_FOLLOW_REPOSITION=33, /* Reposition the MAV after a follow target command has been sent |Camera q1 (where 0 is on the ray from the camera to the tracking device)| Camera q2| Camera q3| Camera q4| altitude offset from target| X offset from target| Y offset from target|  */
   MAV_CMD_DO_ORBIT=34, /* Start orbiting on the circumference of a circle defined by the parameters. Setting values to NaN/INT32_MAX (as appropriate) results in using defaults. |Radius of the circle. Positive: orbit clockwise. Negative: orbit counter-clockwise. NaN: Use vehicle default radius, or current radius if already orbiting.| Tangential Velocity. NaN: Use vehicle default velocity, or current velocity if already orbiting.| Yaw behavior of the vehicle.| Orbit around the centre point for this many radians (i.e. for a three-quarter orbit set 270*Pi/180). 0: Orbit forever. NaN: Use vehicle default, or current value if already orbiting.| Center point latitude (if no MAV_FRAME specified) / X coordinate according to MAV_FRAME. INT32_MAX (or NaN if sent in COMMAND_LONG): Use current vehicle position, or current center if already orbiting.| Center point longitude (if no MAV_FRAME specified) / Y coordinate according to MAV_FRAME. INT32_MAX (or NaN if sent in COMMAND_LONG): Use current vehicle position, or current center if already orbiting.| Center point altitude (MSL) (if no MAV_FRAME specified) / Z coordinate according to MAV_FRAME. NaN: Use current vehicle altitude.|  */
   MAV_CMD_NAV_ROI=80, /* Sets the region of interest (ROI) for a sensor set or the vehicle itself. This can then be used by the vehicle's control system to control the vehicle attitude and the attitude of various sensors such as cameras. |Region of interest mode.| Waypoint index/ target ID. (see MAV_ROI enum)| ROI index (allows a vehicle to manage multiple ROI's)| Empty| x the location of the fixed ROI (see MAV_FRAME)| y| z|  */
   MAV_CMD_NAV_PATHPLANNING=81, /* Control autonomous path planning on the MAV. |0: Disable local obstacle avoidance / local path planning (without resetting map), 1: Enable local path planning, 2: Enable and reset local path planning| 0: Disable full path planning (without resetting map), 1: Enable, 2: Enable and reset map/occupancy grid, 3: Enable and reset planned route, but not occupancy grid| Empty| Yaw angle at goal| Latitude/X of goal| Longitude/Y of goal| Altitude/Z of goal|  */
   MAV_CMD_NAV_SPLINE_WAYPOINT=82, /* Navigate to waypoint using a spline path. |Hold time. (ignored by fixed wing, time to stay at waypoint for rotary wing)| Empty| Empty| Empty| Latitude/X of goal| Longitude/Y of goal| Altitude/Z of goal|  */
   MAV_CMD_NAV_ALTITUDE_WAIT=83, /* Mission command to wait for an altitude or downwards vertical speed. This is meant for high altitude balloon launches, allowing the aircraft to be idle until either an altitude is reached or a negative vertical speed is reached (indicating early balloon burst). The wiggle time is how often to wiggle the control surfaces to prevent them seizing up. |Altitude.| Descent speed.| How long to wiggle the control surfaces to prevent them seizing up.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_NAV_VTOL_TAKEOFF=84, /* Takeoff from ground using VTOL mode, and transition to forward flight with specified heading. The command should be ignored by vehicles that dont support both VTOL and fixed-wing flight (multicopters, boats,etc.). |Empty| Front transition heading.| Empty| Yaw angle. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_VTOL_LAND=85, /* Land using VTOL mode |Landing behaviour.| Empty| Approach altitude (with the same reference as the Altitude field). NaN if unspecified.| Yaw angle. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.).| Latitude| Longitude| Altitude (ground level) relative to the current coordinate frame. NaN to use system default landing altitude (ignore value).|  */
   MAV_CMD_NAV_GUIDED_ENABLE=92, /* hand control over to an external controller |On / Off (> 0.5f on)| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_NAV_DELAY=93, /* Delay the next navigation command a number of seconds or until a specified time |Delay (-1 to enable time-of-day fields)| hour (24h format, UTC, -1 to ignore)| minute (24h format, UTC, -1 to ignore)| second (24h format, UTC, -1 to ignore)| Empty| Empty| Empty|  */
   MAV_CMD_NAV_PAYLOAD_PLACE=94, /* Descend and place payload. Vehicle moves to specified location, descends until it detects a hanging payload has reached the ground, and then releases the payload. If ground is not detected before the reaching the maximum descent value (param1), the command will complete without releasing the payload. |Maximum distance to descend.| Empty| Empty| Empty| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_LAST=95, /* NOP - This command is only used to mark the upper limit of the NAV/ACTION commands in the enumeration |Empty| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_CONDITION_DELAY=112, /* Delay mission state machine. |Delay| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_CONDITION_CHANGE_ALT=113, /* Ascend/descend to target altitude at specified rate. Delay mission state machine until desired altitude reached. |Descent / Ascend rate.| Empty| Empty| Empty| Empty| Empty| Target Altitude|  */
   MAV_CMD_CONDITION_DISTANCE=114, /* Delay mission state machine until within desired distance of next NAV point. |Distance.| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_CONDITION_YAW=115, /* Reach a certain target angle. |target angle [0-360]. Absolute angles: 0 is north. Relative angle: 0 is initial yaw. Direction set by param3.| angular speed| direction: -1: counter clockwise, 0: shortest direction, 1: clockwise| 0: absolute angle, 1: relative offset| Empty| Empty| Empty|  */
   MAV_CMD_CONDITION_LAST=159, /* NOP - This command is only used to mark the upper limit of the CONDITION commands in the enumeration |Empty| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_MODE=176, /* Set system mode. |Mode| Custom mode - this is system specific, please refer to the individual autopilot specifications for details.| Custom sub mode - this is system specific, please refer to the individual autopilot specifications for details.| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_JUMP=177, /* Jump to the desired command in the mission list.  Repeat this action only the specified number of times |Sequence number| Repeat count| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_CHANGE_SPEED=178, /* Change speed and/or throttle set points. The value persists until it is overridden or there is a mode change |Speed type of value set in param2 (such as airspeed, ground speed, and so on)| Speed (-1 indicates no change, -2 indicates return to default vehicle speed)| Throttle (-1 indicates no change, -2 indicates return to default vehicle throttle value)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_SET_HOME=179, /* 
          Sets the home position to either to the current position or a specified position.
          The home position is the default position that the system will return to and land on.
          The position is set automatically by the system during the takeoff (and may also be set using this command).
          Note: the current home position may be emitted in a HOME_POSITION message on request (using MAV_CMD_REQUEST_MESSAGE with param1=242).
         |Use current (1=use current location, 0=use specified location)| Roll angle (of surface). Range: -180..180 degrees. NAN or 0 means value not set. 0.01 indicates zero roll.| Pitch angle (of surface). Range: -90..90 degrees. NAN or 0 means value not set. 0.01 means zero pitch.| Yaw angle. NaN to use default heading. Range: -180..180 degrees.| Latitude| Longitude| Altitude|  */
   MAV_CMD_DO_SET_PARAMETER=180, /* Set a system parameter.  Caution!  Use of this command requires knowledge of the numeric enumeration value of the parameter. |Parameter number| Parameter value| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_RELAY=181, /* Set a relay to a condition. |Relay instance number.| Setting. (1=on, 0=off, others possible depending on system hardware)| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_REPEAT_RELAY=182, /* Cycle a relay on and off for a desired number of cycles with a desired period. |Relay instance number.| Cycle count.| Cycle time.| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_SERVO=183, /* Set a servo to a desired PWM value. |Servo instance number.| Pulse Width Modulation.| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_REPEAT_SERVO=184, /* Cycle a between its nominal setting and a desired PWM for a desired number of cycles with a desired period. |Servo instance number.| Pulse Width Modulation.| Cycle count.| Cycle time.| Empty| Empty| Empty|  */
   MAV_CMD_DO_FLIGHTTERMINATION=185, /* Terminate flight immediately.
          Flight termination immediately and irreversibly terminates the current flight, returning the vehicle to ground.
          The vehicle will ignore RC or other input until it has been power-cycled.
          Termination may trigger safety measures, including: disabling motors and deployment of parachute on multicopters, and setting flight surfaces to initiate a landing pattern on fixed-wing).
          On multicopters without a parachute it may trigger a crash landing.
          Support for this command can be tested using the protocol bit: MAV_PROTOCOL_CAPABILITY_FLIGHT_TERMINATION.
          Support for this command can also be tested by sending the command with param1=0 (< 0.5); the ACK should be either MAV_RESULT_FAILED or MAV_RESULT_UNSUPPORTED.
         |Flight termination activated if > 0.5. Otherwise not activated and ACK with MAV_RESULT_FAILED.| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_CHANGE_ALTITUDE=186, /* Change altitude set point. |Altitude.| Frame of new altitude.| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_ACTUATOR=187, /* Sets actuators (e.g. servos) to a desired value. The actuator numbers are mapped to specific outputs (e.g. on any MAIN or AUX PWM or UAVCAN) using a flight-stack specific mechanism (i.e. a parameter). |Actuator 1 value, scaled from [-1 to 1]. NaN to ignore.| Actuator 2 value, scaled from [-1 to 1]. NaN to ignore.| Actuator 3 value, scaled from [-1 to 1]. NaN to ignore.| Actuator 4 value, scaled from [-1 to 1]. NaN to ignore.| Actuator 5 value, scaled from [-1 to 1]. NaN to ignore.| Actuator 6 value, scaled from [-1 to 1]. NaN to ignore.| Index of actuator set (i.e if set to 1, Actuator 1 becomes Actuator 7)|  */
   MAV_CMD_DO_RETURN_PATH_START=188, /* Mission item to specify the start of a failsafe/landing return-path segment (the end of the segment is the next MAV_CMD_DO_LAND_START item).
          A vehicle that is using missions for landing (e.g. in a return mode) will join the mission on the closest path of the return-path segment (instead of MAV_CMD_DO_LAND_START or the nearest waypoint).
          The main use case is to minimize the failsafe flight path in corridor missions, where the inbound/outbound paths are constrained (by geofences) to the same particular path.
          The MAV_CMD_NAV_RETURN_PATH_START would be placed at the start of the return path.
          If a failsafe occurs on the outbound path the vehicle will move to the nearest point on the return path (which is parallel for this kind of mission), effectively turning round and following the shortest path to landing.
          If a failsafe occurs on the inbound path the vehicle is already on the return segment and will continue to landing.
          The Latitude/Longitude/Altitude are optional, and may be set to 0 if not needed.
          If specified, the item defines the waypoint at which the return segment starts.
          If sent using as a command, the vehicle will perform a mission landing (using the land segment if defined) or reject the command if mission landings are not supported, or no mission landing is defined. When used as a command any position information in the command is ignored.
         |Empty| Empty| Empty| Empty| Latitudee. 0: not used.| Longitudee. 0: not used.| Altitudee. 0: not used.|  */
   MAV_CMD_DO_LAND_START=189, /* Mission command to perform a landing. This is used as a marker in a mission to tell the autopilot where a sequence of mission items that represents a landing starts.
	  It may also be sent via a COMMAND_LONG to trigger a landing, in which case the nearest (geographically) landing sequence in the mission will be used.
	  The Latitude/Longitude/Altitude is optional, and may be set to 0 if not needed. If specified then it will be used to help find the closest landing sequence.
	 |Empty| Empty| Empty| Empty| Latitude| Longitude| Altitude|  */
   MAV_CMD_DO_RALLY_LAND=190, /* Mission command to perform a landing from a rally point. |Break altitude| Landing speed| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_GO_AROUND=191, /* Mission command to safely abort an autonomous landing. |Altitude| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_REPOSITION=192, /* Reposition the vehicle to a specific WGS84 global position. This command is intended for guided commands (for missions use MAV_CMD_NAV_WAYPOINT instead). |Ground speed, less than 0 (-1) for default| Bitmask of option flags.| Loiter radius for planes. Positive values only, direction is controlled by Yaw value. A value of zero or NaN is ignored. | Yaw heading. NaN to use the current system yaw heading mode (e.g. yaw towards next waypoint, yaw to home, etc.). For planes indicates loiter direction (0: clockwise, 1: counter clockwise)| Latitude| Longitude| Altitude|  */
   MAV_CMD_DO_PAUSE_CONTINUE=193, /* If in a GPS controlled position mode, hold the current position or continue. |0: Pause current mission or reposition command, hold current position. 1: Continue mission. A VTOL capable vehicle should enter hover mode (multicopter and VTOL planes). A plane should loiter with the default loiter radius.| Reserved| Reserved| Reserved| Reserved| Reserved| Reserved|  */
   MAV_CMD_DO_SET_REVERSE=194, /* Set moving direction to forward or reverse. |Direction (0=Forward, 1=Reverse)| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_ROI_LOCATION=195, /* Sets the region of interest (ROI) to a location. This can then be used by the vehicle's control system to control the vehicle attitude and the attitude of various sensors such as cameras. This command can be sent to a gimbal manager but not to a gimbal device. A gimbal is not to react to this message. |Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).| Empty| Empty| Empty| Latitude of ROI location| Longitude of ROI location| Altitude of ROI location|  */
   MAV_CMD_DO_SET_ROI_WPNEXT_OFFSET=196, /* Sets the region of interest (ROI) to be toward next waypoint, with optional pitch/roll/yaw offset. This can then be used by the vehicle's control system to control the vehicle attitude and the attitude of various sensors such as cameras. This command can be sent to a gimbal manager but not to a gimbal device. A gimbal device is not to react to this message. |Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).| Empty| Empty| Empty| Pitch offset from next waypoint, positive pitching up| Roll offset from next waypoint, positive rolling to the right| Yaw offset from next waypoint, positive yawing to the right|  */
   MAV_CMD_DO_SET_ROI_NONE=197, /* Cancels any previous ROI command returning the vehicle/sensors to default flight characteristics. This can then be used by the vehicle's control system to control the vehicle attitude and the attitude of various sensors such as cameras. This command can be sent to a gimbal manager but not to a gimbal device. A gimbal device is not to react to this message. After this command the gimbal manager should go back to manual input if available, and otherwise assume a neutral position. |Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_ROI_SYSID=198, /* Mount tracks system with specified system ID. Determination of target vehicle position may be done with GLOBAL_POSITION_INT or any other means. This command can be sent to a gimbal manager but not to a gimbal device. A gimbal device is not to react to this message. |System ID| Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_CONTROL_VIDEO=200, /* Control onboard camera system. |Camera ID (-1 for all)| Transmission: 0: disabled, 1: enabled compressed, 2: enabled raw| Transmission mode: 0: video stream, >0: single images every n seconds| Recording: 0: disabled, 1: enabled compressed, 2: enabled raw| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_ROI=201, /* Sets the region of interest (ROI) for a sensor set or the vehicle itself. This can then be used by the vehicle's control system to control the vehicle attitude and the attitude of various sensors such as cameras. |Region of interest mode.| Waypoint index/ target ID (depends on param 1).| Region of interest index. (allows a vehicle to manage multiple ROI's)| Empty| MAV_ROI_WPNEXT: pitch offset from next waypoint, MAV_ROI_LOCATION: latitude| MAV_ROI_WPNEXT: roll offset from next waypoint, MAV_ROI_LOCATION: longitude| MAV_ROI_WPNEXT: yaw offset from next waypoint, MAV_ROI_LOCATION: altitude|  */
   MAV_CMD_DO_DIGICAM_CONFIGURE=202, /* Configure digital camera. This is a fallback message for systems that have not yet implemented PARAM_EXT_XXX messages and camera definition files (see https://mavlink.io/en/services/camera_def.html ). |Modes: P, TV, AV, M, Etc.| Shutter speed: Divisor number for one second.| Aperture: F stop number.| ISO number e.g. 80, 100, 200, Etc.| Exposure type enumerator.| Command Identity.| Main engine cut-off time before camera trigger. (0 means no cut-off)|  */
   MAV_CMD_DO_DIGICAM_CONTROL=203, /* Control digital camera. This is a fallback message for systems that have not yet implemented PARAM_EXT_XXX messages and camera definition files (see https://mavlink.io/en/services/camera_def.html ). |Session control e.g. show/hide lens| Zoom's absolute position| Zooming step value to offset zoom from the current position| Focus Locking, Unlocking or Re-locking| Shooting Command| Command Identity| Test shot identifier. If set to 1, image will only be captured, but not counted towards internal frame count.|  */
   MAV_CMD_DO_MOUNT_CONFIGURE=204, /* Mission command to configure a camera or antenna mount |Mount operation mode| stabilize roll? (1 = yes, 0 = no)| stabilize pitch? (1 = yes, 0 = no)| stabilize yaw? (1 = yes, 0 = no)| roll input (0 = angle body frame, 1 = angular rate, 2 = angle absolute frame)| pitch input (0 = angle body frame, 1 = angular rate, 2 = angle absolute frame)| yaw input (0 = angle body frame, 1 = angular rate, 2 = angle absolute frame)|  */
   MAV_CMD_DO_MOUNT_CONTROL=205, /* Mission command to control a camera or antenna mount |pitch depending on mount mode (degrees or degrees/second depending on pitch input).| roll depending on mount mode (degrees or degrees/second depending on roll input).| yaw depending on mount mode (degrees or degrees/second depending on yaw input).| altitude depending on mount mode.| latitude, set if appropriate mount mode.| longitude, set if appropriate mount mode.| Mount mode.|  */
   MAV_CMD_DO_SET_CAM_TRIGG_DIST=206, /* Mission command to set camera trigger distance for this flight. The camera is triggered each time this distance is exceeded. This command can also be used to set the shutter integration time for the camera. |Camera trigger distance. 0 to stop triggering.| Camera shutter integration time. -1 or 0 to ignore| Trigger camera once immediately. (0 = no trigger, 1 = trigger)| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_FENCE_ENABLE=207, /* 
          Enable the geofence.
          This can be used in a mission or via the command protocol.
          The persistence/lifetime of the setting is undefined.
          Depending on flight stack implementation it may persist until superseded, or it may revert to a system default at the end of a mission.
          Flight stacks typically reset the setting to system defaults on reboot.
	 |enable? (0=disable, 1=enable, 2=disable_floor_only)| Fence types to enable or disable as a bitmask. A value of 0 indicates that all fences should be enabled or disabled. This parameter is ignored if param 1 has the value 2| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_PARACHUTE=208, /* Mission item/command to release a parachute or enable/disable auto release. |Action| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_MOTOR_TEST=209, /* Command to perform motor test. |Motor instance number (from 1 to max number of motors on the vehicle).| Throttle type (whether the Throttle Value in param3 is a percentage, PWM value, etc.)| Throttle value.| Timeout between tests that are run in sequence.| Motor count. Number of motors to test in sequence: 0/1=one motor, 2= two motors, etc. The Timeout (param4) is used between tests.| Motor test order.| Empty|  */
   MAV_CMD_DO_INVERTED_FLIGHT=210, /* Change to/from inverted flight. |Inverted flight. (0=normal, 1=inverted)| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_GRIPPER=211, /* Mission command to operate a gripper. |Gripper instance number.| Gripper action to perform.| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_AUTOTUNE_ENABLE=212, /* Enable/disable autotune. |Enable (1: enable, 0:disable).| Specify which axis are autotuned. 0 indicates autopilot default settings.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_NAV_SET_YAW_SPEED=213, /* Sets a desired vehicle turn angle and speed change. |Yaw angle to adjust steering by.| Speed.| Final angle. (0=absolute, 1=relative)| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_CAM_TRIGG_INTERVAL=214, /* Mission command to set camera trigger interval for this flight. If triggering is enabled, the camera is triggered each time this interval expires. This command can also be used to set the shutter integration time for the camera. |Camera trigger cycle time. -1 or 0 to ignore.| Camera shutter integration time. Should be less than trigger cycle time. -1 or 0 to ignore.| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_RESUME_REPEAT_DIST=215, /* Set the distance to be repeated on mission resume |Distance.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_SPRAYER=216, /* Control attached liquid sprayer |0: disable sprayer. 1: enable sprayer.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_SEND_SCRIPT_MESSAGE=217, /* Pass instructions onto scripting, a script should be checking for a new command |uint16 ID value to be passed to scripting| float value to be passed to scripting| float value to be passed to scripting| float value to be passed to scripting| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_AUX_FUNCTION=218, /* Execute auxiliary function |Auxiliary Function.| Switch Level.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_MOUNT_CONTROL_QUAT=220, /* Mission command to control a camera or antenna mount, using a quaternion as reference. |quaternion param q1, w (1 in null-rotation)| quaternion param q2, x (0 in null-rotation)| quaternion param q3, y (0 in null-rotation)| quaternion param q4, z (0 in null-rotation)| Empty| Empty| Empty|  */
   MAV_CMD_DO_GUIDED_MASTER=221, /* set id of master controller |System ID| Component ID| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_GUIDED_LIMITS=222, /* Set limits for external control |Timeout - maximum time that external controller will be allowed to control vehicle. 0 means no timeout.| Altitude (MSL) min - if vehicle moves below this alt, the command will be aborted and the mission will continue. 0 means no lower altitude limit.| Altitude (MSL) max - if vehicle moves above this alt, the command will be aborted and the mission will continue. 0 means no upper altitude limit.| Horizontal move limit - if vehicle moves more than this distance from its location at the moment the command was executed, the command will be aborted and the mission will continue. 0 means no horizontal move limit.| Empty| Empty| Empty|  */
   MAV_CMD_DO_ENGINE_CONTROL=223, /* Control vehicle engine. This is interpreted by the vehicles engine controller to change the target engine state. It is intended for vehicles with internal combustion engines |0: Stop engine, 1:Start Engine| 0: Warm start, 1:Cold start. Controls use of choke where applicable| Height delay. This is for commanding engine start only after the vehicle has gained the specified height. Used in VTOL vehicles during takeoff to start engine after the aircraft is off the ground. Zero for no delay.| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_SET_MISSION_CURRENT=224, /* 
          Set the mission item with sequence number seq as the current item and emit MISSION_CURRENT (whether or not the mission number changed).
          If a mission is currently being executed, the system will continue to this new mission item on the shortest path, skipping any intermediate mission items.
	  Note that mission jump repeat counters are not reset unless param2 is set (see MAV_CMD_DO_JUMP param2).

          This command may trigger a mission state-machine change on some systems: for example from MISSION_STATE_NOT_STARTED or MISSION_STATE_PAUSED to MISSION_STATE_ACTIVE.
          If the system is in mission mode, on those systems this command might therefore start, restart or resume the mission.
          If the system is not in mission mode this command must not trigger a switch to mission mode.

          The mission may be "reset" using param2.
          Resetting sets jump counters to initial values (to reset counters without changing the current mission item set the param1 to `-1`).
          Resetting also explicitly changes a mission state of MISSION_STATE_COMPLETE to MISSION_STATE_PAUSED or MISSION_STATE_ACTIVE, potentially allowing it to resume when it is (next) in a mission mode.

	  The command will ACK with MAV_RESULT_FAILED if the sequence number is out of range (including if there is no mission item).
         |Mission sequence value to set. -1 for the current mission item (use to reset mission without changing current mission item).| Resets mission. 1: true, 0: false. Resets jump counters to initial values and changes mission state "completed" to be "active" or "paused".| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_DO_LAST=240, /* NOP - This command is only used to mark the upper limit of the DO commands in the enumeration |Empty| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_PREFLIGHT_CALIBRATION=241, /* Trigger calibration. This command will be only accepted if in pre-flight mode. Except for Temperature Calibration, only one sensor should be set in a single message and all others should be zero. |1: gyro calibration, 3: gyro temperature calibration| 1: magnetometer calibration| 1: ground pressure calibration| 1: radio RC calibration, 2: RC trim calibration| 1: accelerometer calibration, 2: board level calibration, 3: accelerometer temperature calibration, 4: simple accelerometer calibration| 1: APM: compass/motor interference calibration (PX4: airspeed calibration, deprecated), 2: airspeed calibration| 1: ESC calibration, 3: barometer temperature calibration|  */
   MAV_CMD_PREFLIGHT_SET_SENSOR_OFFSETS=242, /* Set sensor offsets. This command will be only accepted if in pre-flight mode. |Sensor to adjust the offsets for: 0: gyros, 1: accelerometer, 2: magnetometer, 3: barometer, 4: optical flow, 5: second magnetometer, 6: third magnetometer| X axis offset (or generic dimension 1), in the sensor's raw units| Y axis offset (or generic dimension 2), in the sensor's raw units| Z axis offset (or generic dimension 3), in the sensor's raw units| Generic dimension 4, in the sensor's raw units| Generic dimension 5, in the sensor's raw units| Generic dimension 6, in the sensor's raw units|  */
   MAV_CMD_PREFLIGHT_UAVCAN=243, /* Trigger UAVCAN configuration (actuator ID assignment and direction mapping). Note that this maps to the legacy UAVCAN v0 function UAVCAN_ENUMERATE, which is intended to be executed just once during initial vehicle configuration (it is not a normal pre-flight command and has been poorly named). |1: Trigger actuator ID assignment and direction mapping. 0: Cancel command.| Reserved| Reserved| Reserved| Reserved| Reserved| Reserved|  */
   MAV_CMD_PREFLIGHT_STORAGE=245, /* Request storage of different parameter values and logs. This command will be only accepted if in pre-flight mode. |Action to perform on the persistent parameter storage| Action to perform on the persistent mission storage| Onboard logging: 0: Ignore, 1: Start default rate logging, -1: Stop logging, > 1: logging rate (e.g. set to 1000 for 1000 Hz logging)| Reserved| Empty| Empty| Empty|  */
   MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN=246, /* Request the reboot or shutdown of system components. |0: Do nothing for autopilot, 1: Reboot autopilot, 2: Shutdown autopilot, 3: Reboot autopilot and keep it in the bootloader until upgraded.| 0: Do nothing for onboard computer, 1: Reboot onboard computer, 2: Shutdown onboard computer, 3: Reboot onboard computer and keep it in the bootloader until upgraded.| 0: Do nothing for component, 1: Reboot component, 2: Shutdown component, 3: Reboot component and keep it in the bootloader until upgraded| MAVLink Component ID targeted in param3 (0 for all components).| Reserved (set to 0)| Reserved (set to 0)| WIP: ID (e.g. camera ID -1 for all IDs)|  */
   MAV_CMD_OVERRIDE_GOTO=252, /* Override current mission with command to pause mission, pause mission and move to position, continue/resume mission. When param 1 indicates that the mission is paused (MAV_GOTO_DO_HOLD), param 2 defines whether it holds in place or moves to another position. |MAV_GOTO_DO_HOLD: pause mission and either hold or move to specified position (depending on param2), MAV_GOTO_DO_CONTINUE: resume mission.| MAV_GOTO_HOLD_AT_CURRENT_POSITION: hold at current position, MAV_GOTO_HOLD_AT_SPECIFIED_POSITION: hold at specified position.| Coordinate frame of hold point.| Desired yaw angle.| Latitude/X position.| Longitude/Y position.| Altitude/Z position.|  */
   MAV_CMD_OBLIQUE_SURVEY=260, /* Mission command to set a Camera Auto Mount Pivoting Oblique Survey (Replaces CAM_TRIGG_DIST for this purpose). The camera is triggered each time this distance is exceeded, then the mount moves to the next position. Params 4~6 set-up the angle limits and number of positions for oblique survey, where mount-enabled vehicles automatically roll the camera between shots to emulate an oblique camera setup (providing an increased HFOV). This command can also be used to set the shutter integration time for the camera. |Camera trigger distance. 0 to stop triggering.| Camera shutter integration time. 0 to ignore| The minimum interval in which the camera is capable of taking subsequent pictures repeatedly. 0 to ignore.| Total number of roll positions at which the camera will capture photos (images captures spread evenly across the limits defined by param5).| Angle limits that the camera can be rolled to left and right of center.| Fixed pitch angle that the camera will hold in oblique mode if the mount is actuated in the pitch axis.| Empty|  */
   MAV_CMD_MISSION_START=300, /* start running a mission |first_item: the first mission item to run| last_item:  the last mission item to run (after this item is run, the mission ends)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_ACTUATOR_TEST=310, /* Actuator testing command. This is similar to MAV_CMD_DO_MOTOR_TEST but operates on the level of output functions, i.e. it is possible to test Motor1 independent from which output it is configured on. Autopilots typically refuse this command while armed. |Output value: 1 means maximum positive output, 0 to center servos or minimum motor thrust (expected to spin), -1 for maximum negative (if not supported by the motors, i.e. motor is not reversible, smaller than 0 maps to NaN). And NaN maps to disarmed (stop the motors).| Timeout after which the test command expires and the output is restored to the previous value. A timeout has to be set for safety reasons. A timeout of 0 means to restore the previous value immediately.| Reserved (default:0)| Reserved (default:0)| Actuator Output function| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_CONFIGURE_ACTUATOR=311, /* Actuator configuration command. |Actuator configuration action| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Actuator Output function| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_COMPONENT_ARM_DISARM=400, /* Arms / Disarms a component |0: disarm, 1: arm| 0: arm-disarm unless prevented by safety checks (i.e. when landed), 21196: force arming/disarming (e.g. allow arming to override preflight checks and disarming in flight)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_RUN_PREARM_CHECKS=401, /* Instructs a target system to run pre-arm checks.
          This allows preflight checks to be run on demand, which may be useful on systems that normally run them at low rate, or which do not trigger checks when the armable state might have changed.
          This command should return MAV_RESULT_ACCEPTED if it will run the checks.
          The results of the checks are usually then reported in SYS_STATUS messages (this is system-specific).
          The command should return MAV_RESULT_TEMPORARILY_REJECTED if the system is already armed.
         |Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_ILLUMINATOR_ON_OFF=405, /* Turns illuminators ON/OFF. An illuminator is a light source that is used for lighting up dark areas external to the system: e.g. a torch or searchlight (as opposed to a light source for illuminating the system itself, e.g. an indicator light). |0: Illuminators OFF, 1: Illuminators ON| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_ILLUMINATOR_CONFIGURE=406, /* Configures illuminator settings. An illuminator is a light source that is used for lighting up dark areas external to the system: e.g. a torch or searchlight (as opposed to a light source for illuminating the system itself, e.g. an indicator light). |Mode| 0%: Off, 100%: Max Brightness| Strobe period in seconds where 0 means strobing is not used| Strobe duty cycle where 100% means it is on constantly and 0 means strobing is not used| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_GET_HOME_POSITION=410, /* Request the home position from the vehicle.
	  The vehicle will ACK the command and then emit the HOME_POSITION message. |Reserved| Reserved| Reserved| Reserved| Reserved| Reserved| Reserved|  */
   MAV_CMD_INJECT_FAILURE=420, /* Inject artificial failure for testing purposes. Note that autopilots should implement an additional protection before accepting this command such as a specific param setting. |The unit which is affected by the failure.| The type how the failure manifests itself.| Instance affected by failure (0 to signal all).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_START_RX_PAIR=500, /* Starts receiver pairing. |0:Spektrum.| RC type.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_GET_MESSAGE_INTERVAL=510, /* 
          Request the interval between messages for a particular MAVLink message ID.
          The receiver should ACK the command and then emit its response in a MESSAGE_INTERVAL message.
         |The MAVLink message ID| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_SET_MESSAGE_INTERVAL=511, /* Set the interval between messages for a particular MAVLink message ID. This interface replaces REQUEST_DATA_STREAM. |The MAVLink message ID| The interval between two messages. -1: disable. 0: request default rate (which may be zero).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Target address of message stream (if message has target address fields). 0: Flight-stack default (recommended), 1: address of requestor, 2: broadcast.|  */
   MAV_CMD_REQUEST_MESSAGE=512, /* Request the target system(s) emit a single instance of a specified message (i.e. a "one-shot" version of MAV_CMD_SET_MESSAGE_INTERVAL). |The MAVLink message ID of the requested message.| Use for index ID, if required. Otherwise, the use of this parameter (if any) must be defined in the requested message. By default assumed not used (0).| The use of this parameter (if any), must be defined in the requested message. By default assumed not used (0).| The use of this parameter (if any), must be defined in the requested message. By default assumed not used (0).| The use of this parameter (if any), must be defined in the requested message. By default assumed not used (0).| The use of this parameter (if any), must be defined in the requested message. By default assumed not used (0).| Target address for requested message (if message has target address fields). 0: Flight-stack default, 1: address of requestor, 2: broadcast.|  */
   MAV_CMD_REQUEST_PROTOCOL_VERSION=519, /* Request MAVLink protocol version compatibility. All receivers should ACK the command and then emit their capabilities in an PROTOCOL_VERSION message |1: Request supported protocol versions by all nodes on the network| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES=520, /* Request autopilot capabilities. The receiver should ACK the command and then emit its capabilities in an AUTOPILOT_VERSION message |1: Request autopilot version| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_CAMERA_INFORMATION=521, /* Request camera information (CAMERA_INFORMATION). |0: No action 1: Request camera capabilities| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_CAMERA_SETTINGS=522, /* Request camera settings (CAMERA_SETTINGS). |0: No Action 1: Request camera settings| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_STORAGE_INFORMATION=525, /* Request storage information (STORAGE_INFORMATION). Use the command's target_component to target a specific component's storage. |Storage ID (0 for all, 1 for first, 2 for second, etc.)| 0: No Action 1: Request storage information| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_STORAGE_FORMAT=526, /* Format a storage medium. Once format is complete, a STORAGE_INFORMATION message is sent. Use the command's target_component to target a specific component's storage. |Storage ID (1 for first, 2 for second, etc.)| Format storage (and reset image log). 0: No action 1: Format storage| Reset Image Log (without formatting storage medium). This will reset CAMERA_CAPTURE_STATUS.image_count and CAMERA_IMAGE_CAPTURED.image_index. 0: No action 1: Reset Image Log| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS=527, /* Request camera capture status (CAMERA_CAPTURE_STATUS) |0: No Action 1: Request camera capture status| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_FLIGHT_INFORMATION=528, /* Request flight information (FLIGHT_INFORMATION) |1: Request flight information| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_RESET_CAMERA_SETTINGS=529, /* Reset all camera settings to Factory Default |0: No Action 1: Reset all settings| Reserved (all remaining params)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_SET_CAMERA_MODE=530, /* Set camera running mode. Use NaN for reserved values. GCS will send a MAV_CMD_REQUEST_VIDEO_STREAM_STATUS command after a mode change if the camera supports video streaming. |Target camera ID. 7 to 255: MAVLink camera component id. 1 to 6 for cameras that don't have a distinct component id (such as autopilot-attached cameras). 0: all cameras. This is used to specifically target autopilot-connected cameras or individual sensors in a multi-sensor MAVLink camera. It is also used to target specific cameras when the MAV_CMD is used in a mission| Camera mode| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_SET_CAMERA_ZOOM=531, /* Set camera zoom. Camera must respond with a CAMERA_SETTINGS message (on success). |Zoom type| Zoom value. The range of valid values depend on the zoom type.| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_SET_CAMERA_FOCUS=532, /* Set camera focus. Camera must respond with a CAMERA_SETTINGS message (on success). |Focus type| Focus value| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_SET_STORAGE_USAGE=533, /* Set that a particular storage is the preferred location for saving photos, videos, and/or other media (e.g. to set that an SD card is used for storing videos).
          There can only be one preferred save location for each particular media type: setting a media usage flag will clear/reset that same flag if set on any other storage.
          If no flag is set the system should use its default storage.
          A target system can choose to always use default storage, in which case it should ACK the command with MAV_RESULT_UNSUPPORTED.
          A target system can choose to not allow a particular storage to be set as preferred storage, in which case it should ACK the command with MAV_RESULT_DENIED. |Storage ID (1 for first, 2 for second, etc.)| Usage flags| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_SET_CAMERA_SOURCE=534, /* Set camera source. Changes the camera's active sources on cameras with multiple image sensors. |Component Id of camera to address or 1-6 for non-MAVLink cameras, 0 for all cameras.| Primary Source| Secondary Source. If non-zero the second source will be displayed as picture-in-picture.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_JUMP_TAG=600, /* Tagged jump target. Can be jumped to with MAV_CMD_DO_JUMP_TAG. |Tag.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_JUMP_TAG=601, /* Jump to the matching tag in the mission list. Repeat this action for the specified number of times. A mission should contain a single matching tag for each jump. If this is not the case then a jump to a missing tag should complete the mission, and a jump where there are multiple matching tags should always select the one with the lowest mission sequence number. |Target tag to jump to.| Repeat count.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW=1000, /* Set gimbal manager pitch/yaw setpoints (low rate command). It is possible to set combinations of the values below. E.g. an angle as well as a desired angular rate can be used to get to this angle at a certain angular rate, or an angular rate only will result in continuous turning. NaN is to be used to signal unset. Note: only the gimbal manager will react to this command - it will be ignored by a gimbal device. Use GIMBAL_MANAGER_SET_PITCHYAW if you need to stream pitch/yaw setpoints at higher rate.  |Pitch angle (positive to pitch up, relative to vehicle for FOLLOW mode, relative to world horizon for LOCK mode).| Yaw angle (positive to yaw to the right, relative to vehicle for FOLLOW mode, absolute to North for LOCK mode).| Pitch rate (positive to pitch up).| Yaw rate (positive to yaw to the right).| Gimbal manager flags to use.| Reserved (default:0)| Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).|  */
   MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE=1001, /* Gimbal configuration to set which sysid/compid is in primary and secondary control. |Sysid for primary control (0: no one in control, -1: leave unchanged, -2: set itself in control (for missions where the own sysid is still unknown), -3: remove control if currently in control).| Compid for primary control (0: no one in control, -1: leave unchanged, -2: set itself in control (for missions where the own sysid is still unknown), -3: remove control if currently in control).| Sysid for secondary control (0: no one in control, -1: leave unchanged, -2: set itself in control (for missions where the own sysid is still unknown), -3: remove control if currently in control).| Compid for secondary control (0: no one in control, -1: leave unchanged, -2: set itself in control (for missions where the own sysid is still unknown), -3: remove control if currently in control).| Reserved (default:0)| Reserved (default:0)| Component ID of gimbal device to address (or 1-6 for non-MAVLink gimbal), 0 for all gimbal device components. Send command multiple times for more than one gimbal (but not all gimbals).|  */
   MAV_CMD_IMAGE_START_CAPTURE=2000, /* Start image capture sequence. CAMERA_IMAGE_CAPTURED must be emitted after each capture.

          Param1 (id) may be used to specify the target camera: 0: all cameras, 1 to 6: autopilot-connected cameras, 7-255: MAVLink camera component ID.
          It is needed in order to target specific cameras connected to the autopilot, or specific sensors in a multi-sensor camera (neither of which have a distinct MAVLink component ID).
          It is also needed to specify the target camera in missions.

          When used in a mission, an autopilot should execute the MAV_CMD for a specified local camera (param1 = 1-6), or resend it as a command if it is intended for a MAVLink camera (param1 = 7 - 255), setting the command's target_component as the param1 value (and setting param1 in the command to zero).
          If the param1 is 0 the autopilot should do both.

          When sent in a command the target MAVLink address is set using target_component.
          If addressed specifically to an autopilot: param1 should be used in the same way as it is for missions (though command should NACK with MAV_RESULT_DENIED if a specified local camera does not exist).
          If addressed to a MAVLink camera, param 1 can be used to address all cameras (0), or to separately address 1 to 7 individual sensors. Other values should be NACKed with MAV_RESULT_DENIED.
          If the command is broadcast (target_component is 0) then param 1 should be set to 0 (any other value should be NACKED with MAV_RESULT_DENIED). An autopilot would trigger any local cameras and forward the command to all channels.
         |Target camera ID. 7 to 255: MAVLink camera component id. 1 to 6 for cameras that don't have a distinct component id (such as autopilot-attached cameras). 0: all cameras. This is used to specifically target autopilot-connected cameras or individual sensors in a multi-sensor MAVLink camera. It is also used to target specific cameras when the MAV_CMD is used in a mission| Desired elapsed time between two consecutive pictures (in seconds). Minimum values depend on hardware (typically greater than 2 seconds).| Total number of images to capture. 0 to capture forever/until MAV_CMD_IMAGE_STOP_CAPTURE.| Capture sequence number starting from 1. This is only valid for single-capture (param3 == 1), otherwise set to 0. Increment the capture ID for each capture command to prevent double captures when a command is re-transmitted.| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_IMAGE_STOP_CAPTURE=2001, /* Stop image capture sequence.

          Param1 (id) may be used to specify the target camera: 0: all cameras, 1 to 6: autopilot-connected cameras, 7-255: MAVLink camera component ID.
          It is needed in order to target specific cameras connected to the autopilot, or specific sensors in a multi-sensor camera (neither of which have a distinct MAVLink component ID).
          It is also needed to specify the target camera in missions.

          When used in a mission, an autopilot should execute the MAV_CMD for a specified local camera (param1 = 1-6), or resend it as a command if it is intended for a MAVLink camera (param1 = 7 - 255), setting the command's target_component as the param1 value (and setting param1 in the command to zero).
          If the param1 is 0 the autopilot should do both.

          When sent in a command the target MAVLink address is set using target_component.
          If addressed specifically to an autopilot: param1 should be used in the same way as it is for missions (though command should NACK with MAV_RESULT_DENIED if a specified local camera does not exist).
          If addressed to a MAVLink camera, param1 can be used to address all cameras (0), or to separately address 1 to 7 individual sensors. Other values should be NACKed with MAV_RESULT_DENIED.
          If the command is broadcast (target_component is 0) then param 1 should be set to 0 (any other value should be NACKED with MAV_RESULT_DENIED). An autopilot would trigger any local cameras and forward the command to all channels.
         |Target camera ID. 7 to 255: MAVLink camera component id. 1 to 6 for cameras that don't have a distinct component id (such as autopilot-attached cameras). 0: all cameras. This is used to specifically target autopilot-connected cameras or individual sensors in a multi-sensor MAVLink camera. It is also used to target specific cameras when the MAV_CMD is used in a mission| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_REQUEST_CAMERA_IMAGE_CAPTURE=2002, /* Re-request a CAMERA_IMAGE_CAPTURED message. |Sequence number for missing CAMERA_IMAGE_CAPTURED message| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_DO_TRIGGER_CONTROL=2003, /* Enable or disable on-board camera triggering system. |Trigger enable/disable (0 for disable, 1 for start), -1 to ignore| 1 to reset the trigger sequence, -1 or 0 to ignore| 1 to pause triggering, but without switching the camera off or retracting it. -1 to ignore| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_CAMERA_TRACK_POINT=2004, /* If the camera supports point visual tracking (CAMERA_CAP_FLAGS_HAS_TRACKING_POINT is set), this command allows to initiate the tracking. |Point to track x value (normalized 0..1, 0 is left, 1 is right).| Point to track y value (normalized 0..1, 0 is top, 1 is bottom).| Point radius (normalized 0..1, 0 is one pixel, 1 is full image width).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_CAMERA_TRACK_RECTANGLE=2005, /* If the camera supports rectangle visual tracking (CAMERA_CAP_FLAGS_HAS_TRACKING_RECTANGLE is set), this command allows to initiate the tracking. |Top left corner of rectangle x value (normalized 0..1, 0 is left, 1 is right).| Top left corner of rectangle y value (normalized 0..1, 0 is top, 1 is bottom).| Bottom right corner of rectangle x value (normalized 0..1, 0 is left, 1 is right).| Bottom right corner of rectangle y value (normalized 0..1, 0 is top, 1 is bottom).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_CAMERA_STOP_TRACKING=2010, /* Stops ongoing tracking. |Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_VIDEO_START_CAPTURE=2500, /* Starts video capture (recording). |Video Stream ID (0 for all streams)| Frequency CAMERA_CAPTURE_STATUS messages should be sent while recording (0 for no messages, otherwise frequency)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_VIDEO_STOP_CAPTURE=2501, /* Stop the current video capture (recording). |Video Stream ID (0 for all streams)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_VIDEO_START_STREAMING=2502, /* Start video streaming |Video Stream ID (0 for all streams, 1 for first, 2 for second, etc.)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_VIDEO_STOP_STREAMING=2503, /* Stop the given video stream |Video Stream ID (0 for all streams, 1 for first, 2 for second, etc.)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION=2504, /* Request video stream information (VIDEO_STREAM_INFORMATION) |Video Stream ID (0 for all streams, 1 for first, 2 for second, etc.)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_REQUEST_VIDEO_STREAM_STATUS=2505, /* Request video stream status (VIDEO_STREAM_STATUS) |Video Stream ID (0 for all streams, 1 for first, 2 for second, etc.)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_LOGGING_START=2510, /* Request to start streaming logging data over MAVLink (see also LOGGING_DATA message) |Format: 0: ULog| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)|  */
   MAV_CMD_LOGGING_STOP=2511, /* Request to stop streaming log data over MAVLink |Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)|  */
   MAV_CMD_AIRFRAME_CONFIGURATION=2520, /*  |Landing gear ID (default: 0, -1 for all)| Landing gear position (Down: 0, Up: 1, NaN for no change)| Reserved (default:NaN)| Reserved (default:NaN)| Reserved (default:0)| Reserved (default:0)| Reserved (default:NaN)|  */
   MAV_CMD_CONTROL_HIGH_LATENCY=2600, /* Request to start/stop transmitting over the high latency telemetry |Control transmission over high latency telemetry (0: stop, 1: start)| Empty| Empty| Empty| Empty| Empty| Empty|  */
   MAV_CMD_PANORAMA_CREATE=2800, /* Create a panorama at the current position |Viewing angle horizontal of the panorama (+- 0.5 the total angle)| Viewing angle vertical of panorama.| Speed of the horizontal rotation.| Speed of the vertical rotation.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DO_VTOL_TRANSITION=3000, /* Request VTOL transition |The target VTOL state. For normal transitions, only MAV_VTOL_STATE_MC and MAV_VTOL_STATE_FW can be used.| Force immediate transition to the specified MAV_VTOL_STATE. 1: Force immediate, 0: normal transition. Can be used, for example, to trigger an emergency "Quadchute". Caution: Can be dangerous/damage vehicle, depending on autopilot implementation of this command.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_ARM_AUTHORIZATION_REQUEST=3001, /* Request authorization to arm the vehicle to a external entity, the arm authorizer is responsible to request all data that is needs from the vehicle before authorize or deny the request.
		If approved the COMMAND_ACK message progress field should be set with period of time that this authorization is valid in seconds.
		If the authorization is denied COMMAND_ACK.result_param2 should be set with one of the reasons in ARM_AUTH_DENIED_REASON.
         |Vehicle system id, this way ground station can request arm authorization on behalf of any vehicle| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_SET_GUIDED_SUBMODE_STANDARD=4000, /* This command sets the submode to standard guided when vehicle is in guided mode. The vehicle holds position and altitude and the user can input the desired velocities along all three axes.
                   |Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_SET_GUIDED_SUBMODE_CIRCLE=4001, /* This command sets submode circle when vehicle is in guided mode. Vehicle flies along a circle facing the center of the circle. The user can input the velocity along the circle and change the radius. If no input is given the vehicle will hold position.
                   |Radius of desired circle in CIRCLE_MODE| User defined| User defined| User defined| Target latitude of center of circle in CIRCLE_MODE| Target longitude of center of circle in CIRCLE_MODE| Reserved (default:0)|  */
   MAV_CMD_CONDITION_GATE=4501, /* Delay mission state machine until gate has been reached. |Geometry: 0: orthogonal to path between previous and next waypoint.| Altitude: 0: ignore altitude| Empty| Empty| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_FENCE_RETURN_POINT=5000, /* Fence return point (there can only be one such point in a geofence definition). If rally points are supported they should be used instead. |Reserved| Reserved| Reserved| Reserved| Latitude| Longitude| Altitude|  */
   MAV_CMD_NAV_FENCE_POLYGON_VERTEX_INCLUSION=5001, /* Fence vertex for an inclusion polygon (the polygon must not be self-intersecting). The vehicle must stay within this area. Minimum of 3 vertices required.
         |Polygon vertex count| Vehicle must be inside ALL inclusion zones in a single group, vehicle must be inside at least one group, must be the same for all points in each polygon| Reserved| Reserved| Latitude| Longitude| Reserved|  */
   MAV_CMD_NAV_FENCE_POLYGON_VERTEX_EXCLUSION=5002, /* Fence vertex for an exclusion polygon (the polygon must not be self-intersecting). The vehicle must stay outside this area. Minimum of 3 vertices required.
         |Polygon vertex count| Reserved| Reserved| Reserved| Latitude| Longitude| Reserved|  */
   MAV_CMD_NAV_FENCE_CIRCLE_INCLUSION=5003, /* Circular fence area. The vehicle must stay inside this area.
         |Radius.| Vehicle must be inside ALL inclusion zones in a single group, vehicle must be inside at least one group| Reserved| Reserved| Latitude| Longitude| Reserved|  */
   MAV_CMD_NAV_FENCE_CIRCLE_EXCLUSION=5004, /* Circular fence area. The vehicle must stay outside this area.
         |Radius.| Reserved| Reserved| Reserved| Latitude| Longitude| Reserved|  */
   MAV_CMD_NAV_RALLY_POINT=5100, /* Rally point. You can have multiple rally points defined.
         |Reserved| Reserved| Reserved| Reserved| Latitude| Longitude| Altitude|  */
   MAV_CMD_UAVCAN_GET_NODE_INFO=5200, /* Commands the vehicle to respond with a sequence of messages UAVCAN_NODE_INFO, one message per every UAVCAN node that is online. Note that some of the response messages can be lost, which the receiver can detect easily by checking whether every received UAVCAN_NODE_STATUS has a matching message UAVCAN_NODE_INFO received earlier; if not, this command should be sent again in order to request re-transmission of the node information messages. |Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)|  */
   MAV_CMD_DO_SET_SAFETY_SWITCH_STATE=5300, /* Change state of safety switch. |New safety switch state.| Empty.| Empty.| Empty| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_ADSB_OUT_IDENT=10001, /* Trigger the start of an ADSB-out IDENT. This should only be used when requested to do so by an Air Traffic Controller in controlled airspace. This starts the IDENT which is then typically held for 18 seconds by the hardware per the Mode A, C, and S transponder spec. |Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)| Reserved (set to 0)|  */
   MAV_CMD_PAYLOAD_PREPARE_DEPLOY=30001, /* Deploy payload on a Lat / Lon / Alt position. This includes the navigation to reach the required release position and velocity. |Operation mode. 0: prepare single payload deploy (overwriting previous requests), but do not execute it. 1: execute payload deploy immediately (rejecting further deploy commands during execution, but allowing abort). 2: add payload deploy to existing deployment list.| Desired approach vector in compass heading. A negative value indicates the system can define the approach vector at will.| Desired ground speed at release time. This can be overridden by the airframe in case it needs to meet minimum airspeed. A negative value indicates the system can define the ground speed at will.| Minimum altitude clearance to the release position. A negative value indicates the system can define the clearance at will.| Latitude.| Longitude.| Altitude (MSL)|  */
   MAV_CMD_PAYLOAD_CONTROL_DEPLOY=30002, /* Control the payload deployment. |Operation mode. 0: Abort deployment, continue normal mission. 1: switch to payload deployment mode. 100: delete first payload deployment request. 101: delete all payload deployment requests.| Reserved| Reserved| Reserved| Reserved| Reserved| Reserved|  */
   MAV_CMD_WAYPOINT_USER_1=31000, /* User defined waypoint item. Ground Station will show the Vehicle as flying through this item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_WAYPOINT_USER_2=31001, /* User defined waypoint item. Ground Station will show the Vehicle as flying through this item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_WAYPOINT_USER_3=31002, /* User defined waypoint item. Ground Station will show the Vehicle as flying through this item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_WAYPOINT_USER_4=31003, /* User defined waypoint item. Ground Station will show the Vehicle as flying through this item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_WAYPOINT_USER_5=31004, /* User defined waypoint item. Ground Station will show the Vehicle as flying through this item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_SPATIAL_USER_1=31005, /* User defined spatial item. Ground Station will not show the Vehicle as flying through this item. Example: ROI item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_SPATIAL_USER_2=31006, /* User defined spatial item. Ground Station will not show the Vehicle as flying through this item. Example: ROI item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_SPATIAL_USER_3=31007, /* User defined spatial item. Ground Station will not show the Vehicle as flying through this item. Example: ROI item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_SPATIAL_USER_4=31008, /* User defined spatial item. Ground Station will not show the Vehicle as flying through this item. Example: ROI item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_SPATIAL_USER_5=31009, /* User defined spatial item. Ground Station will not show the Vehicle as flying through this item. Example: ROI item. |User defined| User defined| User defined| User defined| Latitude unscaled| Longitude unscaled| Altitude (MSL)|  */
   MAV_CMD_USER_1=31010, /* User defined command. Ground Station will not show the Vehicle as flying through this item. Example: MAV_CMD_DO_SET_PARAMETER item. |User defined| User defined| User defined| User defined| User defined| User defined| User defined|  */
   MAV_CMD_USER_2=31011, /* User defined command. Ground Station will not show the Vehicle as flying through this item. Example: MAV_CMD_DO_SET_PARAMETER item. |User defined| User defined| User defined| User defined| User defined| User defined| User defined|  */
   MAV_CMD_USER_3=31012, /* User defined command. Ground Station will not show the Vehicle as flying through this item. Example: MAV_CMD_DO_SET_PARAMETER item. |User defined| User defined| User defined| User defined| User defined| User defined| User defined|  */
   MAV_CMD_USER_4=31013, /* User defined command. Ground Station will not show the Vehicle as flying through this item. Example: MAV_CMD_DO_SET_PARAMETER item. |User defined| User defined| User defined| User defined| User defined| User defined| User defined|  */
   MAV_CMD_USER_5=31014, /* User defined command. Ground Station will not show the Vehicle as flying through this item. Example: MAV_CMD_DO_SET_PARAMETER item. |User defined| User defined| User defined| User defined| User defined| User defined| User defined|  */
   MAV_CMD_CAN_FORWARD=32000, /* Request forwarding of CAN packets from the given CAN bus to this component. CAN Frames are sent using CAN_FRAME and CANFD_FRAME messages |Bus number (0 to disable forwarding, 1 for first bus, 2 for 2nd bus, 3 for 3rd bus).| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_POWER_OFF_INITIATED=42000, /* A system wide power-off event has been initiated. |Empty.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SOLO_BTN_FLY_CLICK=42001, /* FLY button has been clicked. |Empty.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SOLO_BTN_FLY_HOLD=42002, /* FLY button has been held for 1.5 seconds. |Takeoff altitude.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SOLO_BTN_PAUSE_CLICK=42003, /* PAUSE button has been clicked. |1 if Solo is in a shot mode, 0 otherwise.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_FIXED_MAG_CAL=42004, /* Magnetometer calibration based on fixed position
        in earth field given by inclination, declination and intensity. |Magnetic declination.| Magnetic inclination.| Magnetic intensity.| Yaw.| Empty.| Empty.| Empty.|  */
   MAV_CMD_FIXED_MAG_CAL_FIELD=42005, /* Magnetometer calibration based on fixed expected field values. |Field strength X.| Field strength Y.| Field strength Z.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_FIXED_MAG_CAL_YAW=42006, /* Magnetometer calibration based on provided known yaw. This allows for fast calibration using WMM field tables in the vehicle, given only the known yaw of the vehicle. If Latitude and longitude are both zero then use the current vehicle location. |Yaw of vehicle in earth frame.| CompassMask, 0 for all.| Latitude.| Longitude.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SET_EKF_SOURCE_SET=42007, /* Set EKF sensor source set. |Source Set Id.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_START_MAG_CAL=42424, /* Initiate a magnetometer calibration. |Bitmask of magnetometers to calibrate. Use 0 to calibrate all sensors that can be started (sensors may not start if disabled, unhealthy, etc.). The command will NACK if calibration does not start for a sensor explicitly specified by the bitmask.| Automatically retry on failure (0=no retry, 1=retry).| Save without user input (0=require input, 1=autosave).| Delay.| Autoreboot (0=user reboot, 1=autoreboot).| Empty.| Empty.|  */
   MAV_CMD_DO_ACCEPT_MAG_CAL=42425, /* Accept a magnetometer calibration. |Bitmask of magnetometers that calibration is accepted (0 means all).| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_CANCEL_MAG_CAL=42426, /* Cancel a running magnetometer calibration. |Bitmask of magnetometers to cancel a running calibration (0 means all).| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SET_FACTORY_TEST_MODE=42427, /* Command autopilot to get into factory test/diagnostic mode. |0: activate test mode, 1: exit test mode.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_DO_SEND_BANNER=42428, /* Reply with the version banner. |Empty.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_ACCELCAL_VEHICLE_POS=42429, /* Used when doing accelerometer calibration. When sent to the GCS tells it what position to put the vehicle in. When sent to the vehicle says what position the vehicle is in. |Position.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_GIMBAL_RESET=42501, /* Causes the gimbal to reset and boot as if it was just powered on. |Empty.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_GIMBAL_AXIS_CALIBRATION_STATUS=42502, /* Reports progress and success or failure of gimbal axis calibration procedure. |Gimbal axis we're reporting calibration progress for.| Current calibration progress for this axis.| Status of the calibration.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_GIMBAL_REQUEST_AXIS_CALIBRATION=42503, /* Starts commutation calibration on the gimbal. |Empty.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_GIMBAL_FULL_RESET=42505, /* Erases gimbal application and parameters. |Magic number.| Magic number.| Magic number.| Magic number.| Magic number.| Magic number.| Magic number.|  */
   MAV_CMD_DO_WINCH=42600, /* Command to operate winch. |Winch instance number.| Action to perform.| Length of line to release (negative to wind).| Release rate (negative to wind).| Empty.| Empty.| Empty.|  */
   MAV_CMD_FLASH_BOOTLOADER=42650, /* Update the bootloader |Empty| Empty| Empty| Empty| Magic number - set to 290876 to actually flash| Empty| Empty|  */
   MAV_CMD_BATTERY_RESET=42651, /* Reset battery capacity for batteries that accumulate consumed battery via integration. |Bitmask of batteries to reset. Least significant bit is for the first battery.| Battery percentage remaining to set.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_DEBUG_TRAP=42700, /* Issue a trap signal to the autopilot process, presumably to enter the debugger. |Magic number - set to 32451 to actually trap.| Empty.| Empty.| Empty.| Empty.| Empty.| Empty.|  */
   MAV_CMD_SCRIPTING=42701, /* Control onboard scripting. |Scripting command to execute| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_NAV_SCRIPT_TIME=42702, /* Scripting command as NAV command with wait for completion. |integer command number (0 to 255)| timeout for operation in seconds. Zero means no timeout (0 to 255)| argument1.| argument2.| Empty| Empty| Empty|  */
   MAV_CMD_NAV_ATTITUDE_TIME=42703, /* Maintain an attitude for a specified time. |Time to maintain specified attitude and climb rate| Roll angle in degrees (positive is lean right, negative is lean left)| Pitch angle in degrees (positive is lean back, negative is lean forward)| Yaw angle| Climb rate| Empty| Empty|  */
   MAV_CMD_GUIDED_CHANGE_SPEED=43000, /* Change flight speed at a given rate. This slews the vehicle at a controllable rate between it's previous speed and the new one. (affects GUIDED only. Outside GUIDED, aircraft ignores these commands. Designed for onboard companion-computer command-and-control, not normally operator/GCS control.) |Airspeed or groundspeed.| Target Speed| Acceleration rate, 0 to take effect instantly| Empty| Empty| Empty| Empty|  */
   MAV_CMD_GUIDED_CHANGE_ALTITUDE=43001, /* Change target altitude at a given rate. This slews the vehicle at a controllable rate between it's previous altitude and the new one. (affects GUIDED only. Outside GUIDED, aircraft ignores these commands. Designed for onboard companion-computer command-and-control, not normally operator/GCS control.) |Empty| Empty| Rate of change, toward new altitude. 0 for maximum rate change. Positive numbers only, as negative numbers will not converge on the new target alt.| Empty| Empty| Empty| Target Altitude|  */
   MAV_CMD_GUIDED_CHANGE_HEADING=43002, /* Change to target heading at a given rate, overriding previous heading/s. This slews the vehicle at a controllable rate between it's previous heading and the new one. (affects GUIDED only. Exiting GUIDED returns aircraft to normal behaviour defined elsewhere. Designed for onboard companion-computer command-and-control, not normally operator/GCS control.) |course-over-ground or raw vehicle heading.| Target heading.| Maximum centripetal accelearation, ie rate of change,  toward new heading.| Empty| Empty| Empty| Empty|  */
   MAV_CMD_EXTERNAL_POSITION_ESTIMATE=43003, /* Provide an external position estimate for use when dead-reckoning. This is meant to be used for occasional position resets that may be provided by a external system such as a remote pilot using landmarks over a video link. |Timestamp that this message was sent as a time in the transmitters time domain. The sender should wrap this time back to zero based on required timing accuracy for the application and the limitations of a 32 bit float. For example, wrapping at 10 hours would give approximately 1ms accuracy. Recipient must handle time wrap in any timing jitter correction applied to this field. Wrap rollover time should not be at not more than 250 seconds, which would give approximately 10 microsecond accuracy.| The time spent in processing the sensor data that is the basis for this position. The recipient can use this to improve time alignment of the data. Set to zero if not known.| estimated one standard deviation accuracy of the measurement. Set to NaN if not known.| Empty| Latitude| Longitude| Altitude, not used. Should be sent as NaN. May be supported in a future version of this message.|  */
   MAV_CMD_STORM32_DO_GIMBAL_MANAGER_CONTROL_PITCHYAW=60002, /* Command to a gimbal manager to control the gimbal tilt and pan angles. It is possible to set combinations of the values below. E.g. an angle as well as a desired angular rate can be used to get to this angle at a certain angular rate, or an angular rate only will result in continuous turning. NaN is to be used to signal unset. A gimbal device is never to react to this command. |Pitch/tilt angle (positive: tilt up). NaN to be ignored.| Yaw/pan angle (positive: pan to the right). NaN to be ignored. The frame is determined by the GIMBAL_DEVICE_FLAGS_YAW_IN_xxx_FRAME flags.| Pitch/tilt rate (positive: tilt up). NaN to be ignored.| Yaw/pan rate (positive: pan to the right). NaN to be ignored. The frame is determined by the GIMBAL_DEVICE_FLAGS_YAW_IN_xxx_FRAME flags.| Gimbal device flags to be applied.| Gimbal manager flags to be applied.| Gimbal ID of the gimbal manager to address (component ID or 1-6 for non-MAVLink gimbal, 0 for all gimbals). Send command multiple times for more than one but not all gimbals. The client is copied into bits 8-15.|  */
   MAV_CMD_STORM32_DO_GIMBAL_MANAGER_SETUP=60010, /* Command to configure a gimbal manager. A gimbal device is never to react to this command. The selected profile is reported in the STORM32_GIMBAL_MANAGER_STATUS message. |Gimbal manager profile (0 = default).| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Gimbal ID of the gimbal manager to address (component ID or 1-6 for non-MAVLink gimbal, 0 for all gimbals). Send command multiple times for more than one but not all gimbals.|  */
   MAV_CMD_QSHOT_DO_CONFIGURE=60020, /* Command to set the shot manager mode. |Set shot mode.| Set shot state or command. The allowed values are specific to the selected shot mode.| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)| Reserved (default:0)|  */
   MAV_CMD_ENUM_END=60021, /*  | */
} MAV_CMD;
#endif

// Qml Singleton factories

static QObject* screenToolsControllerSingletonFactory(QQmlEngine*, QJSEngine*)
{
    ScreenToolsController* screenToolsController = new ScreenToolsController;
    return screenToolsController;
}

static QObject* qgroundcontrolQmlGlobalSingletonFactory(QQmlEngine*, QJSEngine*)
{
    // We create this object as a QGCTool even though it isn't in the toolbox
    QGroundControlQmlGlobal* qmlGlobal = new QGroundControlQmlGlobal(qgcApp(), qgcApp()->toolbox());
    qmlGlobal->setToolbox(qgcApp()->toolbox());

    return qmlGlobal;
}

static QObject* shapeFileHelperSingletonFactory(QQmlEngine*, QJSEngine*)
{
    return new ShapeFileHelper;
}

QGCApplication::QGCApplication(int &argc, char* argv[], bool unitTesting)
    : QApplication(argc, argv)
    , _runningUnitTests(unitTesting)
{
    _msecsElapsedTime.start();

    // Setup for network proxy support
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    // Parse command line options
    bool fClearSettingsOptions = false; // Clear stored settings
    bool fClearCache = false;           // Clear parameter/airframe caches
    bool logging = false;               // Turn on logging
    QString loggingOptions;

    CmdLineOpt_t rgCmdLineOptions[] = {
        { "--clear-settings",   &fClearSettingsOptions, nullptr },
        { "--clear-cache",      &fClearCache,           nullptr },
        { "--logging",          &logging,               &loggingOptions },
        { "--fake-mobile",      &_fakeMobile,           nullptr },
        { "--log-output",       &_logOutput,            nullptr },
        // Add additional command line option flags here
    };

    ParseCmdLineOptions(argc, argv, rgCmdLineOptions, sizeof(rgCmdLineOptions)/sizeof(rgCmdLineOptions[0]), false);

    // Set up timer for delayed missing fact display
    _missingParamsDelayedDisplayTimer.setSingleShot(true);
    _missingParamsDelayedDisplayTimer.setInterval(_missingParamsDelayedDisplayTimerTimeout);
    connect(&_missingParamsDelayedDisplayTimer, &QTimer::timeout, this, &QGCApplication::_missingParamsDisplay);

    // Set application information
    QString applicationName;
    if (_runningUnitTests) {
        // We don't want unit tests to use the same QSettings space as the normal app. So we tweak the app
        // name. Also we want to run unit tests with clean settings every time.
        applicationName = QStringLiteral("%1_unittest").arg(QGC_APP_NAME);
    } else {
#ifdef DAILY_BUILD
        // This gives daily builds their own separate settings space. Allowing you to use daily and stable builds
        // side by side without daily screwing up your stable settings.
        applicationName = QStringLiteral("%1 Daily").arg(QGC_APP_NAME);
#else
        applicationName = QGC_APP_NAME;
#endif
    }
    setApplicationName(applicationName);
    setOrganizationName(QGC_ORG_NAME);
    setOrganizationDomain(QGC_ORG_DOMAIN);
    setApplicationVersion(QString(QGC_APP_VERSION_STR));
    #ifdef Q_OS_LINUX
        setWindowIcon(QIcon(":/res/qgroundcontrol.ico"));
    #endif

    // Set settings format
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    qCDebug(QGCApplicationLog) << "Settings location" << settings.fileName() << "Is writable?:" << settings.isWritable();

    if (!settings.isWritable()) {
        qCWarning(QGCApplicationLog) << "Setings location is not writable";
    }

    // The setting will delete all settings on this boot
    fClearSettingsOptions |= settings.contains(_deleteAllSettingsKey);

    if (_runningUnitTests) {
        // Unit tests run with clean settings
        fClearSettingsOptions = true;
    }

    if (fClearSettingsOptions) {
        // User requested settings to be cleared on command line
        settings.clear();

        // Clear parameter cache
        QDir paramDir(ParameterManager::parameterCacheDir());
        paramDir.removeRecursively();
        paramDir.mkpath(paramDir.absolutePath());
    } else {
        // Determine if upgrade message for settings version bump is required. Check and clear must happen before toolbox is started since
        // that will write some settings.
        if (settings.contains(_settingsVersionKey)) {
            if (settings.value(_settingsVersionKey).toInt() != QGC_SETTINGS_VERSION) {
                settings.clear();
                _settingsUpgraded = true;
            }
        }
    }
    settings.setValue(_settingsVersionKey, QGC_SETTINGS_VERSION);

    if (fClearCache) {
        QDir dir(ParameterManager::parameterCacheDir());
        dir.removeRecursively();
        QFile airframe(cachedAirframeMetaDataFile());
        airframe.remove();
        QFile parameter(cachedParameterMetaDataFile());
        parameter.remove();
    }

    // Set up our logging filters
    QGCLoggingCategoryRegister::instance()->setFilterRulesFromSettings(loggingOptions);

    // We need to set language as early as possible prior to loading on JSON files.
    setLanguage();

    _toolbox = new QGCToolbox(this);
    _toolbox->setChildToolboxes();

#ifndef DAILY_BUILD
    _checkForNewVersion();
#endif
}

void QGCApplication::setLanguage()
{
    _locale = QLocale::system();
    qCDebug(QGCApplicationLog) << "System reported locale:" << _locale << "; Name" << _locale.name() << "; Preffered (used in maps): " << (QLocale::system().uiLanguages().length() > 0 ? QLocale::system().uiLanguages()[0] : "None");

    QLocale::Language possibleLocale = AppSettings::_qLocaleLanguageEarlyAccess();
    if (possibleLocale != QLocale::AnyLanguage) {
        _locale = QLocale(possibleLocale);
    }
    //-- We have specific fonts for Korean
    if(_locale == QLocale::Korean) {
        qCDebug(LocalizationLog) << "Loading Korean fonts" << _locale.name();
        if(QFontDatabase::addApplicationFont(":/fonts/NanumGothic-Regular") < 0) {
            qCWarning(LocalizationLog) << "Could not load /fonts/NanumGothic-Regular font";
        }
        if(QFontDatabase::addApplicationFont(":/fonts/NanumGothic-Bold") < 0) {
            qCWarning(LocalizationLog) << "Could not load /fonts/NanumGothic-Bold font";
        }
    }
    qCDebug(LocalizationLog) << "Loading localizations for" << _locale.name();
    removeTranslator(JsonHelper::translator());
    removeTranslator(&_qgcTranslatorSourceCode);
    removeTranslator(&_qgcTranslatorQtLibs);
    if (_locale.name() != "en_US") {
        QLocale::setDefault(_locale);
        if(_qgcTranslatorQtLibs.load("qt_" + _locale.name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            installTranslator(&_qgcTranslatorQtLibs);
        } else {
            qCWarning(LocalizationLog) << "Qt lib localization for" << _locale.name() << "is not present";
        }
        if(_qgcTranslatorSourceCode.load(_locale, QLatin1String("qgc_source_"), "", ":/i18n")) {
            installTranslator(&_qgcTranslatorSourceCode);
        } else {
            qCWarning(LocalizationLog) << "Error loading source localization for" << _locale.name();
        }
        if(JsonHelper::translator()->load(_locale, QLatin1String("qgc_json_"), "", ":/i18n")) {
            installTranslator(JsonHelper::translator());
        } else {
            qCWarning(LocalizationLog) << "Error loading json localization for" << _locale.name();
        }
    }
    if(_qmlAppEngine) {
        _qmlAppEngine->retranslate();
    }
    emit languageChanged(_locale);
}

QGCApplication::~QGCApplication()
{

}

void QGCApplication::init()
{
    // Register our Qml objects

    qmlRegisterType<Fact>               ("QGroundControl.FactSystem", 1, 0, "Fact");
    qmlRegisterType<FactMetaData>       ("QGroundControl.FactSystem", 1, 0, "FactMetaData");
    qmlRegisterType<FactPanelController>("QGroundControl.FactSystem", 1, 0, "FactPanelController");

    qmlRegisterUncreatableType<FactGroup>               ("QGroundControl.FactSystem",   1, 0, "FactGroup",                  "Reference only");
    qmlRegisterUncreatableType<FactValueSliderListModel>("QGroundControl.FactControls", 1, 0, "FactValueSliderListModel",   "Reference only");
    qmlRegisterUncreatableType<ParameterManager>        ("QGroundControl.Vehicle",      1, 0, "ParameterManager",           "Reference only");

    qmlRegisterUncreatableType<FactValueGrid>        ("QGroundControl.Templates",             1, 0, "FactValueGrid",       "Reference only");
    qmlRegisterUncreatableType<FlightPathSegment>    ("QGroundControl",                       1, 0, "FlightPathSegment",   "Reference only");
    qmlRegisterUncreatableType<InstrumentValueData>  ("QGroundControl",                       1, 0, "InstrumentValueData", "Reference only");
    qmlRegisterUncreatableType<QGCGeoBoundingCube>   ("QGroundControl.FlightMap",             1, 0, "QGCGeoBoundingCube",  "Reference only");
    qmlRegisterUncreatableType<QGCMapPolygon>        ("QGroundControl.FlightMap",             1, 0, "QGCMapPolygon",       "Reference only");
    qmlRegisterUncreatableType<QmlObjectListModel>   ("QGroundControl",                       1, 0, "QmlObjectListModel",  "Reference only");
    qmlRegisterType<CustomAction>                    ("QGroundControl.Controllers",           1, 0, "CustomAction");
    qmlRegisterType<CustomActionManager>             ("QGroundControl.Controllers",           1, 0, "CustomActionManager");
    qmlRegisterType<EditPositionDialogController>    ("QGroundControl.Controllers",           1, 0, "EditPositionDialogController");
    qmlRegisterType<HorizontalFactValueGrid>         ("QGroundControl.Templates",             1, 0, "HorizontalFactValueGrid");
    qmlRegisterType<ParameterEditorController>       ("QGroundControl.Controllers",           1, 0, "ParameterEditorController");
    qmlRegisterType<QGCFileDialogController>         ("QGroundControl.Controllers",           1, 0, "QGCFileDialogController");
    qmlRegisterType<QGCMapCircle>                    ("QGroundControl.FlightMap",             1, 0, "QGCMapCircle");
    qmlRegisterType<QGCMapPalette>                   ("QGroundControl.Palette",               1, 0, "QGCMapPalette");
    qmlRegisterType<QGCPalette>                      ("QGroundControl.Palette",               1, 0, "QGCPalette");
    qmlRegisterType<RCChannelMonitorController>      ("QGroundControl.Controllers",           1, 0, "RCChannelMonitorController");
    qmlRegisterType<RCToParamDialogController>       ("QGroundControl.Controllers",           1, 0, "RCToParamDialogController");
    qmlRegisterType<ScreenToolsController>           ("QGroundControl.Controllers",           1, 0, "ScreenToolsController");
    qmlRegisterType<TerrainProfile>                  ("QGroundControl.Controls",              1, 0, "TerrainProfile");
    qmlRegisterType<ToolStripAction>                 ("QGroundControl.Controls",              1, 0, "ToolStripAction");
    qmlRegisterType<ToolStripActionList>             ("QGroundControl.Controls",              1, 0, "ToolStripActionList");
    qmlRegisterSingletonType<QGroundControlQmlGlobal>("QGroundControl",                       1, 0, "QGroundControl",         qgroundcontrolQmlGlobalSingletonFactory);
    qmlRegisterSingletonType<ScreenToolsController>  ("QGroundControl.ScreenToolsController", 1, 0, "ScreenToolsController",  screenToolsControllerSingletonFactory);


    // #ifdef QGC_VIEWER3D
    Viewer3DManager::registerQmlTypes();
    // #endif

    qmlRegisterUncreatableType<Autotune>              ("QGroundControl.Vehicle",   1, 0, "Autotune",               "Reference only");
    qmlRegisterUncreatableType<RemoteIDManager>       ("QGroundControl.Vehicle",   1, 0, "RemoteIDManager",        "Reference only");
    qmlRegisterUncreatableType<TrajectoryPoints>      ("QGroundControl.FlightMap", 1, 0, "TrajectoryPoints",       "Reference only");
    qmlRegisterUncreatableType<VehicleObjectAvoidance>("QGroundControl.Vehicle",   1, 0, "VehicleObjectAvoidance", "Reference only");


    qmlRegisterUncreatableType<CameraCalc>          ("QGroundControl",              1, 0, "CameraCalc",           "Reference only");
    qmlRegisterUncreatableType<GeoFenceController>  ("QGroundControl.Controllers",  1, 0, "GeoFenceController",   "Reference only");
    qmlRegisterUncreatableType<MissionController>   ("QGroundControl.Controllers",  1, 0, "MissionController",    "Reference only");
    qmlRegisterUncreatableType<MissionItem>         ("QGroundControl",              1, 0, "MissionItem",          "Reference only");
    qmlRegisterUncreatableType<MissionManager>      ("QGroundControl.Vehicle",      1, 0, "MissionManager",       "Reference only");
    qmlRegisterUncreatableType<RallyPointController>("QGroundControl.Controllers",  1, 0, "RallyPointController", "Reference only");
    qmlRegisterUncreatableType<VisualMissionItem>   ("QGroundControl",              1, 0, "VisualMissionItem",    "Reference only");
    qmlRegisterType<PlanMasterController>           ("QGroundControl.Controllers",  1, 0, "PlanMasterController");


    qmlRegisterUncreatableType<MavlinkCameraControl>("QGroundControl.Vehicle", 1, 0, "MavlinkCameraControl", "Reference only");
    qmlRegisterUncreatableType<QGCCameraManager>    ("QGroundControl.Vehicle", 1, 0, "QGCCameraManager",     "Reference only");
    qmlRegisterUncreatableType<QGCVideoStreamInfo>  ("QGroundControl.Vehicle", 1, 0, "QGCVideoStreamInfo",   "Reference only");
    qmlRegisterUncreatableType<GimbalController>    ("QGroundControl.Vehicle", 1, 0, "GimbalController",     "Reference only");

#if !defined(QGC_DISABLE_MAVLINK_INSPECTOR)
    qmlRegisterUncreatableType<MAVLinkChartController>("QGroundControl",             1, 0, "MAVLinkChart", "Reference only");
    qmlRegisterType<MAVLinkInspectorController>       ("QGroundControl.Controllers", 1, 0, "MAVLinkInspectorController");
#endif
    qmlRegisterType<GeoTagController>        ("QGroundControl.Controllers", 1, 0, "GeoTagController");
    qmlRegisterType<LogDownloadController>   ("QGroundControl.Controllers", 1, 0, "LogDownloadController");
    qmlRegisterType<MAVLinkConsoleController>("QGroundControl.Controllers", 1, 0, "MAVLinkConsoleController");


    qmlRegisterUncreatableType<AutoPilotPlugin>("QGroundControl.AutoPilotPlugin", 1, 0, "AutoPilotPlugin", "Reference only");
    qmlRegisterType<ESP8266ComponentController>("QGroundControl.Controllers",     1, 0, "ESP8266ComponentController");
    qmlRegisterType<SyslinkComponentController>("QGroundControl.Controllers",     1, 0, "SyslinkComponentController");


    qmlRegisterUncreatableType<VehicleComponent>("QGroundControl.AutoPilotPlugin", 1, 0, "VehicleComponent", "Reference only");
#ifndef NO_SERIAL_LINK
    qmlRegisterType<FirmwareUpgradeController>("QGroundControl.Controllers",       1, 0, "FirmwareUpgradeController");
#endif
    qmlRegisterType<JoystickConfigController>("QGroundControl.Controllers",        1, 0, "JoystickConfigController");


    qmlRegisterSingletonType<ShapeFileHelper>("QGroundControl.ShapeFileHelper", 1, 0, "ShapeFileHelper", shapeFileHelperSingletonFactory);


    // Although this should really be in _initForNormalAppBoot putting it here allowws us to create unit tests which pop up more easily
    if(QFontDatabase::addApplicationFont(":/fonts/opensans") < 0) {
        qWarning() << "Could not load /fonts/opensans font";
    }
    if(QFontDatabase::addApplicationFont(":/fonts/opensans-demibold") < 0) {
        qWarning() << "Could not load /fonts/opensans-demibold font";
    }

    if (!_runningUnitTests) {
        _initForNormalAppBoot();
    } else {
        AudioOutput::instance()->setMuted(true);
    }

    // Setup switch/case lists
    axisList << "pitch" << "yaw" << "roll";
    this->commandsList << "OPEN_STREAM" << "STOP_STREAM" << "RESET_GIMBAL" << "MOVE_GIMBAL" << "GET_CAMERAS" << "SET_CAMERA" << "SET_CAMERA_INTRINSICS" << "GET_CAMERA" << "ZOOM_CAMERA" << "TAKE_PHOTO" << "START_RECORDING" << "STOP_RECORDING" << "MAV_CMD_DO_SET_SERVO";

    // Setup MqttClient
    m_client = new QMqttClient(this);
    m_client->setHostname("152.228.246.204");
    m_client->setPort(1883);
    m_client->setUsername(QString(""));
    m_client->setCleanSession(false);
    m_client->setAutoKeepAlive(true); 
    m_client->setKeepAlive(60);
    m_client->setClientId(QUuid::createUuid().toString());
    m_client->setProtocolVersion(QMqttClient::MQTT_5_0);

    connect(m_client, &QMqttClient::stateChanged, this, &QGCApplication::updateLogStateChange);
    connect(m_client, &QMqttClient::disconnected, this, &QGCApplication::brokerDisconnected);
    connect(m_client, &QMqttClient::connected, this, &QGCApplication::brokerConnected);
    m_client->connectToHost();

    // Setup Position & Remote Pilote TIMER
    QTimer *timer = new QTimer(this);

    QObject::connect(timer, &QTimer::timeout, this, &QGCApplication::sendInfos);

    timer->start(2000);

}

void QGCApplication::updateLogStateChange()
{
    qCWarning(QGCApplicationLog) << "State Change : " + QString::number(m_client->state());
}

void QGCApplication::brokerConnected()
{

    // Setup Subscription
    QString topic = "REQUEST/+/" + this->uavSn + "/+";
    QMqttSubscription *subscription = m_client->subscribe(topic, 1);
    if(!subscription) {
        qCWarning(QGCApplicationLog) << "============== here ==============";
    }
    QObject::connect(subscription, &QMqttSubscription::messageReceived, this, &QGCApplication::updateMessage);

    qCWarning(QGCApplicationLog) << "Mqtt Connected";
}

void QGCApplication::brokerDisconnected()
{
    qCWarning(QGCApplicationLog) << m_client->error();
    qCWarning(QGCApplicationLog) << "Mqtt Disconnected";
    m_client->connectToHost();
}

void QGCApplication::updateMessage(const QMqttMessage &msg)
{
    QString payload = QString(msg.payload());
    QJsonDocument d = QJsonDocument::fromJson(payload.toUtf8());
    QJsonObject message = d.object();
    MavlinkCameraControl *activeCamera;
    switch (this->commandsList.indexOf(message["instruction"].toString())){
        case 0:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved OPEN_STREAM";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::startStream();
            break;
        case 1:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved STOP_STREAM";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::stopStream();
            break;
        case 2:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved RESET_GIMBAL";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::resetGimbal();
            break;
        case 3:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved MOVE_GIMBAL";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::moveGimbal(message["axis"].toString(), message["value"].toString());
            break;
        case 4:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved GET_CAMERAS";
            qCWarning(QGCApplicationLog) << "=================================================";
            message.insert("availableCameraListData", QGCApplication::getCameras());
            break;
        case 5:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved SET_CAMERA";
            qCWarning(QGCApplicationLog) << "=================================================";
            break;
        case 6:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved SET_CAMERA_INTRINSICS";
            qCWarning(QGCApplicationLog) << "=================================================";
            break;
        case 7:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved GET_CAMERA";
            qCWarning(QGCApplicationLog) << "=================================================";
            message.insert("gimbalRange", QGCApplication::getGimbalCapabilities());
            activeCamera = QGCApplication::getActiveCamera();
            if(!activeCamera) {
                qCWarning(QGCApplicationLog) << "============== camera ranges ==============";
                message.insert("hasZoom", activeCamera->hasZoom());
                if(activeCamera->modelName() != "Caméra intégrée Tundra II"){
                    QJsonObject iso;
                    QJsonObject aperture;
                    iso.insert("min", activeCamera->iso()->cookedMinString());
                    iso.insert("max", activeCamera->iso()->cookedMaxString());
                    aperture.insert("min", activeCamera->aperture()->cookedMinString());
                    aperture.insert("max", activeCamera->aperture()->cookedMaxString());
                    message.insert("isoRange", iso);
                    message.insert("aperture", aperture);
                }
            }
            break;
        case 8:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved ZOOM_CAMERA";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::setZoom(message["zoomValue"].toDouble());
            break;
        case 9:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved TAKE_PHOTO";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::takePhoto();
            break;
        case 10:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved START_RECORDING";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::startRecording();
            break;
        case 11:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved STOP_RECORDING";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::stopRecording();
            break;
        case 12:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved MAV_CMD_DO_SET_SERVO";
            qCWarning(QGCApplicationLog) << "=================================================";
            QGCApplication::servoCmd(message["param1"].toDouble(), message["param2"].toDouble()); 
            break; // ************** SERVO ID, SURTOUT PAS 1 2 3 4 13 14 **********************
        default:
            message.insert("status","KO");
            message.insert("error","KO");
    }

    QJsonDocument doc(message);
    QString responseMessage(doc.toJson(QJsonDocument::Compact));

    QString responseTopic = msg.publishProperties().responseTopic();

    QMqttPublishProperties properties;
    properties.setCorrelationData(msg.publishProperties().correlationData());
    
    // Set the qos to 1 (important!)
    m_client->publish(responseTopic, properties, responseMessage.toUtf8(), 1, false);
}

void QGCApplication::updateStatus(QMqttSubscription::SubscriptionState state)
{
    switch (state) {
    case QMqttSubscription::Unsubscribed:
        qCDebug(QGCApplicationLog) << "Unsubscribed";
        break;
    case QMqttSubscription::SubscriptionPending:
        qCDebug(QGCApplicationLog) << "Pending";
        break;
    case QMqttSubscription::Subscribed:
        qCDebug(QGCApplicationLog) << "Subscribed";
        break;
    case QMqttSubscription::Error:
        qCDebug(QGCApplicationLog) << "Error";
        break;
    case QMqttSubscription::UnsubscriptionPending:
        qCDebug(QGCApplicationLog) << "Pending Unsubscription";
        break;
    default:
        qCDebug(QGCApplicationLog) << "--Unknown--";
        break;
    }
}

void QGCApplication::sendInfos(){

    qCWarning(QGCApplicationLog) << "============== start send infos ==============";

    if(!m_client) {
        qCWarning(QGCApplicationLog) << "*****   Mqtt not available   *****";
        return;
    }

    QGCApplication::sendAircraftPositionInfos();
    QGCApplication::sendRemotePilote();
    
    qCWarning(QGCApplicationLog) << "==============  end send infos  ==============";
}

void QGCApplication:: sendRemotePilote() {
    QJsonObject newResponse;
    newResponse.insert("email", this->loggedEmail);
    newResponse.insert("registrationNumber", this->registrationNumber);

    QJsonDocument doc(newResponse);
    QString responseMessage(doc.toJson(QJsonDocument::Compact));
    m_client->publish("REMOTE_PILOT/"+this->uavSn, responseMessage.toUtf8());
}

void QGCApplication:: sendAircraftPositionInfos() {
    qCWarning(QGCApplicationLog) << "============== start send position ==============";
    Vehicle* activeVehicle = QGCApplication::getActiveVehicle();
    if(!activeVehicle) {
        qCWarning(QGCApplicationLog) << "*****   No vehicle available   *****";
        return;
    };

    QJsonObject newResponse;
    newResponse.insert("registrationNumber", this->registrationNumber);
    newResponse.insert("emailRemotePilot", this->loggedEmail);
    newResponse.insert("isStreaming", this->isStreaming);
    newResponse.insert("system", activeVehicle->firmwareTypeString());
    newResponse.insert("systemVersion", "V1"); // TODO ???
    newResponse.insert("simulated", true);
    newResponse.insert("systemOS", "Android"); // TODO change to include Windows
    newResponse.insert("productType", activeVehicle->vehicleTypeString());
    newResponse.insert("rtmpUrl", this->rtmpUrl);
    newResponse.insert("latitude", activeVehicle->coordinate().latitude());
    newResponse.insert("longitude", activeVehicle->coordinate().longitude());
    newResponse.insert("altitude", activeVehicle->coordinate().altitude());
    newResponse.insert("isFlying", activeVehicle->flying());
    newResponse.insert("gpsSatelliteCount", qobject_cast<VehicleGPSFactGroup*>(activeVehicle->gpsFactGroup())->count()->rawValueString());
    newResponse.insert("firmwareVersionUav", activeVehicle->firmwarePatchVersion());
    newResponse.insert("firmwareVersion", this->_buildVersion);
    newResponse.insert("velocity", qobject_cast<VehicleFactGroup*>(activeVehicle->vehicleFactGroup())->airSpeed()->rawValueString());
    
    
    bool hasCamera = activeVehicle->cameraManager()->cameras()->count() != 0;
    newResponse.insert("hasCamera", hasCamera);
    if(hasCamera) {
        MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
        if(activeCamera) {
            qCWarning(QGCApplicationLog) << "============== current camera values ==============";
            newResponse.insert("sensorName", activeCamera->modelName());
            newResponse.insert("hasZoom", activeCamera->hasZoom());
            if(activeCamera->modelName() != "Caméra intégrée Tundra II"){
                QJsonObject currentValues;
                currentValues.insert("ISO", activeCamera->iso()->rawValueString());
                currentValues.insert("whiteBalance", activeCamera->wb()->rawValueString());
                currentValues.insert("aperture", activeCamera->aperture()->rawValueString());
                newResponse.insert("intrinsics", currentValues);
            }
        }
    }

    bool hasGimbal = activeVehicle->gimbalController()->gimbals()->count() != 0;
    newResponse.insert("hasGimbal", hasGimbal);
    if(hasGimbal) {
        Gimbal *activeGimbal = activeVehicle->gimbalController()->activeGimbal();
        if(activeGimbal) {
            qCWarning(QGCApplicationLog) << "============== current gimbal values ==============";
            QJsonObject currentState;
            QJsonObject attitude;
            attitude.insert("yaw", activeGimbal->absoluteYaw()->rawValueString());
            attitude.insert("pitch", activeGimbal->absolutePitch()->rawValueString());
            attitude.insert("roll", activeGimbal->absoluteRoll()->rawValueString());
            currentState.insert("KeyGimbalReset", "null");
            currentState.insert("attitude", attitude);
            currentState.insert("keyYawRelativeToAircraftHeading", activeGimbal->bodyYaw()->rawValueString()); // TODO
            newResponse.insert("gimbal", currentState);
        }
    }
    QmlObjectListModel* batteries = activeVehicle->batteries();
    int res = 0;
    for (int i=0; i<batteries->count(); i++) {
        VehicleBatteryFactGroup* battery = qobject_cast<VehicleBatteryFactGroup*>(batteries->get(i));
        res += battery->percentRemaining()->rawValue().toInt();
    }
    newResponse.insert("batteryPowerPercentUav", res/batteries->count());

    QJsonDocument doc(newResponse);
    QString responseMessage(doc.toJson(QJsonDocument::Compact));
    m_client->publish("POSITION/"+this->uavSn, responseMessage.toUtf8());
   

    // Might not do that
    
    /* newResponse.insert("batteryPowerPercentRC", batteryRCLevel); // TODO
    newResponse.insert("batteryBehavior",batteryBehavior); // TODO
    currentValues.insert("sharpness", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("orientation", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("videoResolutionAndFrameRate", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("videoFileFormat", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("photoFileFormat", Vehicule::cameraManager().currentCameraInstance().); // TODO
    newResponse.insert("isZooming", Vehicule::cameraManager().currentCameraInstance().); // TODO
    newResponse.insert("isMovingGimbal", Vehicule::gimbalController().activeGimbal()); // TODO 
            obj.put("gimbalRange", GimbalUtil.gimbalRange);
            obj.put("gimbalSN", GimbalUtil.gimbalSN);
            obj.put("hasLens", CameraUtil.hasLens);
            obj.put("photoFileFormatList", CameraUtil.photoFormats);
            obj.put("videoFileFormatList", CameraUtil.videoFormats);
            obj.put("streamSource", CameraUtil.streamSource);
            obj.put("zoomRatiosRange", new int[]{1});*/
}
        /* 
    switch ((obj.getString("instruction"))){
         case "OPEN_STREAM":
            Log.i("openStream", "=================================================");
            Log.i("openStream", "recieved OPEN_STREAM");
            Log.i("openStream", "================================================="); _updateVideoUri()
            startLiveShow(obj.getString("rtmpChannel"));
            rtmpUrl = BaseUrl + obj.getString("rtmpChannel");
            obj.put("url", rtmpUrl);
            break;
        case "STOP_STREAM":
            Log.i("stopStream", "=================================================");
            Log.i("stopStream", "recieved STOP_STREAM");
            Log.i("stopStream", "=================================================");
            stopLiveShow();
            obj.put("url", "null");
            break;
        case "SET_CAMERA": // TODO rework
            Log.i("setCams", "=================================================");
            Log.i("setCams", "recieved SET_CAMERA");
            Log.i("setCams", "=================================================");
            break; 
        case "SET_CAMERA_INTRINSICS":
            Log.i("getCam", "=================================================");
            Log.i("getCam", "recieved SET_CAMERA_INTRINSICS");
            Log.i("getCam", "=================================================");
            //Log.i("getCam", "recieved photo "+obj.getString("imageFormat"));
            // CameraUtil.setPhotoFormat(obj.getString("imageFormat"));
            // Log.i("getCam", "recieved video "+obj.getString("videoFormat"));
            // CameraUtil.setVideoFormat(obj.getString("videoFormat"));
            //Log.i("getCam", "recieved resolution and framerate "+obj.getString("resolutionAndFramerate"));
            //CameraUtil.setVideoResolutionAndFrameRate(obj.getString("resolutionAndFramerate"));
            Log.i("getCam", "recieved whiteBalance "+obj.getString("whiteBalance"));
            CameraUtil.setWhiteBalancePreset(obj.getString("whiteBalance"));
            //Log.i("getCam", "recieved sharpness "+obj.getString("sharpness"));
            //CameraUtil.setSharpness(obj.getString("sharpness"));
            // Log.i("getCam", "recieved ISO "+obj.getString("iso"));
            // CameraUtil.setISO(obj.getString("iso"));
            //Log.i("getCam", "recieved orientation "+obj.getString("imageOrientation"));
            //CameraUtil.setOrientation(obj.getString("imageOrientation"));
            CameraUtil.getCurrentValues();
            break; 
    }

void QGCApplication::setCamera(int i){
    Vehicule::cameraManager().setCurrentCamera(i);
} */


QJsonObject QGCApplication::getGimbalCapabilities(){
    QJsonObject capabilities;
    Gimbal* activeGimbal = QGCApplication::getActiveGimbal();
    if(activeGimbal) {
        QJsonObject yawCap;
        QJsonObject pitchCap;
        QJsonObject rollCap;
        qCWarning(QGCApplicationLog) << "minYaw : " << activeGimbal->absoluteYaw()->cookedMinString();
        qCWarning(QGCApplicationLog) << "maxYaw : " << activeGimbal->absoluteYaw()->cookedMaxString();
        yawCap.insert("min",activeGimbal->bodyYaw()->cookedMinString());
        yawCap.insert("max",activeGimbal->bodyYaw()->cookedMaxString());
        pitchCap.insert("min",activeGimbal->absolutePitch()->cookedMinString());
        pitchCap.insert("max",activeGimbal->absolutePitch()->cookedMaxString());
        rollCap.insert("min",activeGimbal->absoluteRoll()->cookedMinString());
        rollCap.insert("max",activeGimbal->absoluteRoll()->cookedMaxString());
        capabilities.insert("yaw",yawCap);
        capabilities.insert("pitch",pitchCap);
        capabilities.insert("roll",rollCap);
    }
    return capabilities;
}

Vehicle* QGCApplication::getActiveVehicle(){
    MultiVehicleManager* vehicleManager = toolbox()->multiVehicleManager();
    if(vehicleManager->vehicles()->count() == 0)  {
        qCWarning(QGCApplicationLog) << "*****   No vehicle found   *****";
        return nullptr;
    }
    Vehicle* activeVehicle = vehicleManager->activeVehicle();
    if(!activeVehicle) {
        qCWarning(QGCApplicationLog) << "*****   No active vehicle   *****";
        return nullptr;
    }
    return activeVehicle;
}

MavlinkCameraControl* QGCApplication::getActiveCamera(){
    Vehicle* activeVehicle = QGCApplication::getActiveVehicle();
    if(!activeVehicle || activeVehicle->cameraManager()->cameras()->count() <= 0) {
        qCWarning(QGCApplicationLog) << "*****   No camera available   *****";
        return nullptr;
    }
    QmlObjectListModel *cameras = activeVehicle->cameraManager()->cameras();
    MavlinkCameraControl *activeCamera = qobject_cast<MavlinkCameraControl*>(cameras->get((activeVehicle->cameraManager()->currentCamera())));
    if(!activeCamera) {
        qCWarning(QGCApplicationLog) << "*****   No active camera   *****";
        return nullptr;
    }
    return activeCamera;
}

Gimbal* QGCApplication::getActiveGimbal(){
    Vehicle* activeVehicle = QGCApplication::getActiveVehicle();
    if(!activeVehicle || activeVehicle->gimbalController()->gimbals()->count() <= 0) {
        qCWarning(QGCApplicationLog) << "*****   No gimbal available   *****";
        return nullptr;
    }
    Gimbal *activeGimbal = activeVehicle->gimbalController()->activeGimbal();
    if(!activeGimbal) {
        qCWarning(QGCApplicationLog) << "*****   No active gimbal   *****";
        return nullptr;
    }
    return activeGimbal;
}

QJsonArray QGCApplication::getCameras() {
    QJsonArray cameraList;
    Vehicle* activeVehicle = QGCApplication::getActiveVehicle();
    if(!activeVehicle || activeVehicle->cameraManager()->cameras()->count() <= 0) return cameraList;
    QmlObjectListModel *cameras = activeVehicle->cameraManager()->cameras();
    for (int i = 0; i < cameras->count(); i++) {
        qCWarning(QGCApplicationLog) << "*****   Here   *****";
        MavlinkCameraControl *camera = qobject_cast<MavlinkCameraControl*>(cameras->get(i));
        QJsonObject thisCamera;
        thisCamera.insert("index",i);
        thisCamera.insert("name",camera->modelName());
        cameraList.append(thisCamera);
    }
    return cameraList;
}

void QGCApplication::takePhoto(){
    qCWarning(QGCApplicationLog) << "==============  START TAKE_PHOTO  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) {
        qCWarning(QGCApplicationLog) << "*****   No active camera   *****";
        return;
    }
    activeCamera->setCameraModePhoto();
    activeCamera->takePhoto();
    qCWarning(QGCApplicationLog) << "==============   END TAKE_PHOTO   ==============";
}

void QGCApplication::setZoom(float value){
    qCWarning(QGCApplicationLog) << "==============  START TAKE_PHOTO  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) {
        qCWarning(QGCApplicationLog) << "*****   No active camera   *****";
        return;
    }
    activeCamera->setZoomLevel(value);
    qCWarning(QGCApplicationLog) << "==============  END TAKE_PHOTO  ==============";
}

void QGCApplication::testingStream(){ // TODO remove this
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) return;
    QGCVideoStreamInfo *streamInstance = activeCamera->currentStreamInstance();
    if(!streamInstance) return;
    qCWarning(QGCApplicationLog) << "stream name : " <<streamInstance->name();
    qCWarning(QGCApplicationLog) << "stream uri : " <<streamInstance->uri();
    qCWarning(QGCApplicationLog) << "stream type : " <<streamInstance->type();
}

void QGCApplication::startStream(){
    qCWarning(QGCApplicationLog) << "==============  START OPEN_STREAM  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) return;
    this->rtmpUrl = "rtmp://ome.stationdrone.net/app/" + this->uavSn;
    /* // QString ffmpegPath = Q;
    // QStringList arguments;
    // arguments << "-style" << "fusion";

    QProcess *streamingProcess = new QProcess(this);
    connect(streamingProcess, &QProcess::errorOccurred, this, &QGCApplication::QProcessErrHandler);
    connect(streamingProcess, &QProcess::started, this, &QGCApplication::QProcessStarted);
    connect(streamingProcess, &QProcess::finished, this, &QGCApplication::QProcessFinishHandler);
    // streamingProcess->setProgram(ffmpegPath);
    // streamingProcess->setArguments(arguments);
    // streamingProcess->start(); */
    
    
    /* GError *error = nullptr;

    // const gchar *pipeline_desc = "-e rtspsrc location='rtsp://192.168.144.25:8554/main.264' ! rtph264depay ! h264parse ! flvmux streamable=true ! rtmpsink location='rtmp://ome.stationdrone.net/app/1600FTR2STD24289930B live=1'";
    
    pipeline = gst_parse_launch("-e rtspsrc location='rtsp://192.168.144.25:8554/main.264' ! rtph264depay ! h264parse ! flvmux streamable=true ! rtmpsink location='rtmp://ome.stationdrone.net/app/1600FTR2STD24289930B live=1'", &error);

    if(!pipeline){
        qCWarning(QGCApplicationLog) << "==============  ERREUR GSTREAMER  ==============";
        qCWarning(QGCApplicationLog) << error->message;
        g_clear_error(&error);
        return;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING); */

    qCWarning(QGCApplicationLog) << " gst_is_initialized : ";
    qCWarning(QGCApplicationLog) << gst_is_initialized();
    /* GstElement *source, *encoder, *converter, *sink, *queue1, *queue2, *queue3, *flvmux, *bin;
    GstMessage *message;
    GstStateChangeReturn ret;

    converter = gst_element_factory_make("videoconvert", "converter");
    encoder = gst_element_factory_make("x264enc", "encoder");
    sink = gst_element_factory_make("rtmpsink", "sink");
    queue1 = gst_element_factory_make("queue", "queue1");
    queue2 = gst_element_factory_make("queue", "queue2");
    queue3 = gst_element_factory_make("queue", "queue3");
    flvmux = gst_element_factory_make("flvmux", "flvmux");

    if (!converter || !encoder || !sink || !queue1 || !queue2 || !queue3 || !flvmux)
    {
        qCWarning(QGCApplicationLog) << "Not all elements could be created.";

        return;
    }

    g_object_set(sink, "location", "rtmp://ome.stationdrone.net/app/1600FTR2STD24289930B", NULL);
    g_object_set(flvmux, "streamable", true, NULL);

    pipeline = gst_pipeline_new("pipeline");

    if (!pipeline)
    {
        qCWarning(QGCApplicationLog) << "Pipeline could not be created.";

        return;
    }

    source = gst_element_factory_make("rtspsrc", "source");
    
    g_object_set(source, "location", "rtsp://192.168.144.25:8554/main.264", NULL);

    if (!source)
    {
        qCWarning(QGCApplicationLog) << "Screen capture source could not be created.";
        gst_object_unref(pipeline);

        return;
    }

    bin = gst_bin_new("my_bin");

    if (!source)
    {
        qCWarning(QGCApplicationLog) << "Screen capture source could not be created.";
        gst_object_unref(pipeline);

        return;
    }



    gst_bin_add_many(GST_BIN(bin), source, sink, NULL);

    gst_bin_add(GST_BIN(pipeline), bin);

    if (!gst_element_link_many(source, sink, NULL))
    {
        qCWarning(QGCApplicationLog) << "Elements could not be linked";
        gst_object_unref(pipeline);

        return;
    }

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        qCWarning(QGCApplicationLog) << "Unable to set the pipeline to playing state";
        gst_object_unref(pipeline);

        return;
    }

    bus = gst_element_get_bus(pipeline);
    message = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (message != NULL)
    {
        gst_message_unref(message);
    }

    gst_object_unref(bus);
    
     */

    /* GstStateChangeReturn ret;
    GError *error = nullptr;
    const gchar* pipelineDesc = "rtspsrc location='rtsp://localhost:8554/city-traffic' ! rtph264depay ! h264parse ! flvmux streamable=true ! rtmpsink location='rtmp://ome.stationdrone.net/app/city-traffic live=1'";

    pipeline = gst_parse_launch(pipelineDesc, &error);

    if(!pipeline){
        qCWarning(QGCApplicationLog) << "Error pipeline: " << error->message;
        g_clear_error(&error);
        return;
    }

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        qCWarning(QGCApplicationLog) << "Unable to set the pipeline to playing state";
        gst_object_unref(pipeline);
    } */

    const gchar* pipelineDesc = "rtspsrc location=rtsp://localhost:8554/city-traffic ! rtph264depay ! h264parse ! flvmux streamable=true ! rtmpsink location=rtmp://ome.stationdrone.net/app/city-traffic live=1";
    GError *err = nullptr;
    this->data.pipeline = gst_parse_launch(pipelineDesc, &err);

    // Play the pipeline
    gst_element_set_state(this->data.pipeline, GST_STATE_PLAYING);

    // Start the bus thread
    thread threadBus([&this->data]() -> void {
        codeThreadBus(this->data.pipeline, this->data, "GOBLIN");
    });

    // Wait for threads
    threadBus.join();

    this->isStreaming = true;
}

//======================================================================================================================
/// Process a single bus message, log messages, exit on error, return false on eof
bool QGCApplication::busProcessMsg(GstElement *pipeline, GstMessage *msg, const char* &prefix) {
    GstMessageType mType = GST_MESSAGE_TYPE(msg);
    qCWarning(QGCApplicationLog) << "[" << prefix << "] : mType = " << mType << " ";
    switch (mType) {
        case (GST_MESSAGE_ERROR):
            // Parse error and exit program, hard exit
            GError *err;
            gchar *dbg;
            gst_message_parse_error(msg, &err, &dbg);
            qCWarning(QGCApplicationLog) << "ERR = " << err->message << " FROM " << GST_OBJECT_NAME(msg->src);
            qCWarning(QGCApplicationLog) << "DBG = " << dbg;
            g_clear_error(&err);
            g_free(dbg);
            exit(1);
        case (GST_MESSAGE_EOS) :
            // Soft exit on EOS
            qCWarning(QGCApplicationLog) << " EOS !";
            return false;
        case (GST_MESSAGE_STATE_CHANGED):
            // Parse state change, print extra info for pipeline only
            qCWarning(QGCApplicationLog) << "State changed !";
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                GstState sOld, sNew, sPenging;
                gst_message_parse_state_changed(msg, &sOld, &sNew, &sPenging);
                qCWarning(QGCApplicationLog) << "Pipeline changed from " << gst_element_state_get_name(sOld) << " to " << gst_element_state_get_name(sNew);
            }
            break;
        case (GST_MESSAGE_STEP_START):
            qCWarning(QGCApplicationLog) << "STEP START !";
            break;
        case (GST_MESSAGE_STREAM_STATUS):
            qCWarning(QGCApplicationLog) << "STREAM STATUS !";
            break;
        case (GST_MESSAGE_ELEMENT):
            qCWarning(QGCApplicationLog) << "MESSAGE ELEMENT !";
            break;

            // You can add more stuff here if you want

        default:
            qCWarning(QGCApplicationLog) << "default";
    }
    return true;
}

//======================================================================================================================
/// Run the message loop for one bus
void QGCApplication::codeThreadBus(GstElement *pipeline, GoblinData &data, const char* &prefix) {
    GstBus *bus = gst_element_get_bus(pipeline);
    int res;
    while (true) {
        GstMessage *msg = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
        res = busProcessMsg(pipeline, msg, prefix);
        gst_message_unref(msg);
        if (!res)
            break;
    }
    gst_object_unref(bus);
    qCWarning(QGCApplicationLog) << "BUS THREAD FINISHED : " << prefix;
}
/* 
void QGCApplication::QProcessErrHandler(const QProcess::ProcessError &error){
    qCWarning(QGCApplicationLog) << "**********  STREAM ERROR  **********";
    qCWarning(QGCApplicationLog) << error;
}

void QGCApplication::QProcessStarted(){
    qCWarning(QGCApplicationLog) << "==============  STREAM STARTED  ==============";
}

void QGCApplication::QProcessFinishHandler(const int &exitCode, const QProcess::ExitStatus &exitStatus = QProcess::NormalExit){
    qCWarning(QGCApplicationLog) << "==============  STREAM ENDED  ==============";
    qCWarning(QGCApplicationLog) << "Code : " << exitCode << ", Status : " << exitStatus;
}
 */
void QGCApplication::stopStream(){
    qCWarning(QGCApplicationLog) << "==============  START STOP_STREAM  ==============";
    /* if(!streamingProcess) return;
    streamingProcess->kill();
    streamingProcess = nullptr;
    this->isStreaming = false;
    this->rtmpUrl = ""; */


    gst_element_set_state(this->data.pipeline, GST_STATE_NULL);
    gst_object_unref(this->data.pipeline);
    this->isStreaming = false;
    this->rtmpUrl = "";
}

void QGCApplication::startRecording(){
    qCWarning(QGCApplicationLog) << "==============  START START_RECORDING  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) {
        qCWarning(QGCApplicationLog) << "*****   No active camera   *****";
        return;
    }
    activeCamera->setCameraModeVideo();
    activeCamera->startVideoRecording();
    qCWarning(QGCApplicationLog) << "==============   END START_RECORDING   ==============";
}

void QGCApplication::stopRecording(){
    qCWarning(QGCApplicationLog) << "==============  START STOP_RECORDING  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) return;
    activeCamera->stopVideoRecording();
    qCWarning(QGCApplicationLog) << "==============   END STOP_RECORDING   ==============";
}

void QGCApplication::resetGimbal() {
    qCWarning(QGCApplicationLog) << "==============  START RESET_GIMBAL  ==============";
    Gimbal* activeGimbal = QGCApplication::getActiveGimbal();
    if(!activeGimbal) return;

    activeGimbal->setAbsolutePitch(0);
    activeGimbal->setBodyYaw(0);
    activeGimbal->setAbsoluteRoll(0);
    qCWarning(QGCApplicationLog) << "==============   END RESET_GIMBAL   ==============";
}

void QGCApplication::moveGimbal(QString axis, QString value) {
    qCWarning(QGCApplicationLog) << "==============  START MOVE_GIMBAL  ==============";
    Gimbal* activeGimbal = QGCApplication::getActiveGimbal();
    if(!activeGimbal) return;

    switch (axisList.indexOf(axis)) {
        case 0:
            qCWarning(QGCApplicationLog) << "==============   MOVE_GIMBAL CASE PITCH  ==============";
            activeGimbal->setAbsolutePitch(value.toFloat());
            break;
        case 1:
            qCWarning(QGCApplicationLog) << "==============   MOVE_GIMBAL CASE YAW   ==============";
            activeGimbal->setBodyYaw(value.toFloat());
            break;
        case 2:
            qCWarning(QGCApplicationLog) << "==============   MOVE_GIMBAL CASE ROLL   ==============";
            activeGimbal->setAbsoluteRoll(value.toFloat());
            break;
    }
    qCWarning(QGCApplicationLog) << "==============   END MOVE_GIMBAL   ==============";
}

void QGCApplication::servoCmd(float servoId, float pwmValue){
    Vehicle* activeVehicle = QGCApplication::getActiveVehicle();
    if(!activeVehicle) {
        qCWarning(QGCApplicationLog) << "*****   No vehicle found   *****";
        return;
    }
    
    // Sends the MAV_CMD_DO_SET_SERVO command to the vehicle.
    // If no acknowledgment (Ack) is received, the command will be retried.
    // If another sendMavCommand is already in progress,
    // the current command will be queued and sent once the previous one completes.
    
    // @param compId : Component ID to send the command to.
    // @param command : The MAV_CMD to send.
    // @param showError : true to display an error if the command fails, false to suppress the error display.
    // @param param1 to param7 : Optional parameters to send with the MAV_CMD.
    
    // Signals: mavCommandResult emitted on success or failure of the command.
    activeVehicle->sendMavCommand(
        activeVehicle->defaultComponentId(),  // compId: Default vehicle component ID
        MAV_CMD_DO_SET_SERVO,            // command: MAV_CMD to set servo
        true,                            // showError: Display error if command fails
        servoId,                         // param1: Specify which servo to set (e.g., 1)
        pwmValue,                        // param2: PWM value to set for the servo (e.g., 1500)
        0,                               // param3: Not used (set to 0)
        0,                               // param4: Not used (set to 0)
        0,                               // param5: Not used (set to 0)
        0,                               // param6: Not used (set to 0)
        0                                // param7: Not used (set to 0)
    ); // ************** SERVO ID, SURTOUT PAS 1 2 3 4 13 14 **********************
}

void QGCApplication::_initForNormalAppBoot()
{
#ifdef QGC_GST_STREAMING
    // Gstreamer video playback requires OpenGL
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

    QQuickStyle::setStyle("Basic");
    _qmlAppEngine = _toolbox->corePlugin()->createQmlApplicationEngine(this);
    QObject::connect(_qmlAppEngine, &QQmlApplicationEngine::objectCreationFailed, this, QCoreApplication::quit, Qt::QueuedConnection);
    _toolbox->corePlugin()->createRootWindow(_qmlAppEngine);

    AudioOutput::instance()->init(_toolbox->settingsManager()->appSettings()->audioMuted());
    FollowMe::instance()->init();

    // Image provider for Optical Flow
    _qmlAppEngine->addImageProvider(qgcImageProviderId, new QGCImageProvider());

    QQuickWindow* rootWindow = mainRootWindow();
    if (rootWindow) {
        rootWindow->scheduleRenderJob(new FinishVideoInitialization(_toolbox->videoManager()),
                QQuickWindow::BeforeSynchronizingStage);
    }

    // Safe to show popup error messages now that main window is created
    _showErrorsInToolbar = true;

    #ifdef Q_OS_LINUX
    #ifndef Q_OS_ANDROID
    #ifndef NO_SERIAL_LINK
        if (!_runningUnitTests) {
            // Determine if we have the correct permissions to access USB serial devices
            QFile permFile("/etc/group");
            if(permFile.open(QIODevice::ReadOnly)) {
                while(!permFile.atEnd()) {
                    const QString line = permFile.readLine();
                    if (line.contains("dialout") && !line.contains(getenv("USER"))) {
                        permFile.close();
                        showAppMessage(QString(
                            tr("The current user does not have the correct permissions to access serial devices. "
                               "You should also remove modemmanager since it also interferes.<br/><br/>"
                               "If you are using Ubuntu, execute the following commands to fix these issues:<br/>"
                               "<pre>sudo usermod -a -G dialout $USER<br/>"
                               "sudo apt-get remove modemmanager</pre>")));
                        break;
                    }
                }
                permFile.close();
            }
        }
    #endif
    #endif
    #endif

    // Now that main window is up check for lost log files
    connect(this, &QGCApplication::checkForLostLogFiles, _toolbox->mavlinkProtocol(), &MAVLinkProtocol::checkForLostLogFiles);
    emit checkForLostLogFiles();

    // Load known link configurations
    _toolbox->linkManager()->loadLinkConfigurationList();

    // Probe for joysticks
    _toolbox->joystickManager()->init();

    if (_settingsUpgraded) {
        showAppMessage(QString(tr("The format for %1 saved settings has been modified. "
                    "Your saved settings have been reset to defaults.")).arg(applicationName()));
    }

    // Connect links with flag AutoconnectLink
    _toolbox->linkManager()->startAutoConnectedLinks();
}

void QGCApplication::deleteAllSettingsNextBoot(void)
{
    QSettings settings;
    settings.setValue(_deleteAllSettingsKey, true);
}

void QGCApplication::clearDeleteAllSettingsNextBoot(void)
{
    QSettings settings;
    settings.remove(_deleteAllSettingsKey);
}

void QGCApplication::informationMessageBoxOnMainThread(const QString& /*title*/, const QString& msg)
{
    showAppMessage(msg);
}

void QGCApplication::warningMessageBoxOnMainThread(const QString& /*title*/, const QString& msg)
{
    showAppMessage(msg);
}

void QGCApplication::criticalMessageBoxOnMainThread(const QString& /*title*/, const QString& msg)
{
    showAppMessage(msg);
}

void QGCApplication::saveTelemetryLogOnMainThread(const QString &tempLogfile)
{
    // The vehicle is gone now and we are shutting down so we need to use a message box for errors to hold shutdown and show the error
    if (_checkTelemetrySavePath(true /* useMessageBox */)) {

        const QString saveDirPath = _toolbox->settingsManager()->appSettings()->telemetrySavePath();
        const QDir saveDir(saveDirPath);

        const QString nameFormat("%1%2.%3");
        const QString dtFormat("yyyy-MM-dd hh-mm-ss");

        int tryIndex = 1;
        QString saveFileName = nameFormat.arg(
            QDateTime::currentDateTime().toString(dtFormat)).arg(QStringLiteral("")).arg(_toolbox->settingsManager()->appSettings()->telemetryFileExtension);
        while (saveDir.exists(saveFileName)) {
            saveFileName = nameFormat.arg(
                QDateTime::currentDateTime().toString(dtFormat)).arg(QStringLiteral(".%1").arg(tryIndex++)).arg(_toolbox->settingsManager()->appSettings()->telemetryFileExtension);
        }
        const QString saveFilePath = saveDir.absoluteFilePath(saveFileName);

        QFile tempFile(tempLogfile);
        if (!tempFile.copy(saveFilePath)) {
            const QString error = tr("Unable to save telemetry log. Error copying telemetry to '%1': '%2'.").arg(saveFilePath).arg(tempFile.errorString());
            showAppMessage(error);
        }
    }
    QFile::remove(tempLogfile);
}

void QGCApplication::checkTelemetrySavePathOnMainThread()
{
    // This is called with an active vehicle so don't pop message boxes which holds ui thread
    _checkTelemetrySavePath(false /* useMessageBox */);
}

bool QGCApplication::_checkTelemetrySavePath(bool /*useMessageBox*/)
{
    const QString saveDirPath = _toolbox->settingsManager()->appSettings()->telemetrySavePath();
    if (saveDirPath.isEmpty()) {
        const QString error = tr("Unable to save telemetry log. Application save directory is not set.");
        showAppMessage(error);
        return false;
    }

    const QDir saveDir(saveDirPath);
    if (!saveDir.exists()) {
        const QString error = tr("Unable to save telemetry log. Telemetry save directory \"%1\" does not exist.").arg(saveDirPath);
        showAppMessage(error);
        return false;
    }

    return true;
}

void QGCApplication::reportMissingParameter(int componentId, const QString& name)
{
    const QPair<int, QString> missingParam(componentId, name);

    if (!_missingParams.contains(missingParam)) {
        _missingParams.append(missingParam);
    }
    _missingParamsDelayedDisplayTimer.start();
}

/// Called when the delay timer fires to show the missing parameters warning
void QGCApplication::_missingParamsDisplay(void)
{
    if (_missingParams.count()) {
        QString params;
        for (QPair<int, QString>& missingParam: _missingParams) {
            const QString param = QStringLiteral("%1:%2").arg(missingParam.first).arg(missingParam.second);
            if (params.isEmpty()) {
                params += param;
            } else {
                params += QStringLiteral(", %1").arg(param);
            }

        }
        _missingParams.clear();

        showAppMessage(tr("Parameters are missing from firmware. You may be running a version of firmware which is not fully supported or your firmware has a bug in it. Missing params: %1").arg(params));
    }
}

QObject* QGCApplication::_rootQmlObject()
{
    if (_qmlAppEngine && _qmlAppEngine->rootObjects().size()) {
        return _qmlAppEngine->rootObjects()[0];
    }
    return nullptr;
}

void QGCApplication::showCriticalVehicleMessage(const QString& message)
{
    // PreArm messages are handled by Vehicle and shown in Map
    if (message.startsWith(QStringLiteral("PreArm")) || message.startsWith(QStringLiteral("preflight"), Qt::CaseInsensitive)) {
        return;
    }
    QObject* rootQmlObject = _rootQmlObject();
    if (rootQmlObject && _showErrorsInToolbar) {
        QVariant varReturn;
        QVariant varMessage = QVariant::fromValue(message);
        QMetaObject::invokeMethod(rootQmlObject, "showCriticalVehicleMessage", Q_RETURN_ARG(QVariant, varReturn), Q_ARG(QVariant, varMessage));
    } else if (runningUnitTests() || !_showErrorsInToolbar) {
        // Unit tests can run without UI
        qCDebug(QGCApplicationLog) << "QGCApplication::showCriticalVehicleMessage unittest" << message;
    } else {
        qCWarning(QGCApplicationLog) << "Internal error";
    }
}

void QGCApplication::showAppMessage(const QString& message, const QString& title)
{
    QString dialogTitle = title.isEmpty() ? applicationName() : title;

    QObject* rootQmlObject = _rootQmlObject();
    if (rootQmlObject) {
        QVariant varReturn;
        QVariant varMessage = QVariant::fromValue(message);
        QMetaObject::invokeMethod(_rootQmlObject(), "_showMessageDialog", Q_RETURN_ARG(QVariant, varReturn), Q_ARG(QVariant, dialogTitle), Q_ARG(QVariant, varMessage));
    } else if (runningUnitTests()) {
        // Unit tests can run without UI
        qCDebug(QGCApplicationLog) << "QGCApplication::showAppMessage unittest title:message" << dialogTitle << message;
    } else {
        // UI isn't ready yet
        _delayedAppMessages.append(QPair<QString, QString>(dialogTitle, message));
        QTimer::singleShot(200, this, &QGCApplication::_showDelayedAppMessages);
    }
}

void QGCApplication::showRebootAppMessage(const QString& message, const QString& title)
{
    static QTime lastRebootMessage;

    const QTime currentTime = QTime::currentTime();
    const QTime previousTime = lastRebootMessage;
    lastRebootMessage = currentTime;

    if (previousTime.isValid() && previousTime.msecsTo(currentTime) < 60 * 1000 * 2) {
        // Debounce reboot messages
        return;
    }

    showAppMessage(message, title);
}

void QGCApplication::_showDelayedAppMessages(void)
{
    if (_rootQmlObject()) {
        for (const QPair<QString, QString>& appMsg: _delayedAppMessages) {
            showAppMessage(appMsg.second, appMsg.first);
        }
        _delayedAppMessages.clear();
    } else {
        QTimer::singleShot(200, this, &QGCApplication::_showDelayedAppMessages);
    }
}

QQuickWindow* QGCApplication::mainRootWindow()
{
    if(!_mainRootWindow) {
        _mainRootWindow = qobject_cast<QQuickWindow*>(_rootQmlObject());
    }
    return _mainRootWindow;
}

void QGCApplication::showSetupView()
{
    if(_rootQmlObject()) {
      QVariant arg = "";
      QMetaObject::invokeMethod(_rootQmlObject(), "showVehicleSetupTool", Q_ARG(QVariant, arg));
    }
}

void QGCApplication::qmlAttemptWindowClose()
{
    if(_rootQmlObject()) {
        QMetaObject::invokeMethod(_rootQmlObject(), "attemptWindowClose");
    }
}

void QGCApplication::_checkForNewVersion()
{
    if (!_runningUnitTests) {
        if (_parseVersionText(applicationVersion(), _majorVersion, _minorVersion, _buildVersion)) {
            const QString versionCheckFile = _toolbox->corePlugin()->stableVersionCheckFileUrl();
            if (!versionCheckFile.isEmpty()) {
                QGCFileDownload* download = new QGCFileDownload(this);
                connect(download, &QGCFileDownload::downloadComplete, this, &QGCApplication::_qgcCurrentStableVersionDownloadComplete);
                download->download(versionCheckFile);
            }
        }
    }
}

void QGCApplication::_qgcCurrentStableVersionDownloadComplete(QString /*remoteFile*/, QString localFile, QString errorMsg)
{
    if (errorMsg.isEmpty()) {
        QFile versionFile(localFile);
        if (versionFile.open(QIODevice::ReadOnly)) {
            QTextStream textStream(&versionFile);
            const QString version = textStream.readLine();

            qCDebug(QGCApplicationLog) << version;

            int majorVersion, minorVersion, buildVersion;
            if (_parseVersionText(version, majorVersion, minorVersion, buildVersion)) {
                if (_majorVersion < majorVersion ||
                        (_majorVersion == majorVersion && _minorVersion < minorVersion) ||
                        (_majorVersion == majorVersion && _minorVersion == minorVersion && _buildVersion < buildVersion)) {
                    showAppMessage(tr("There is a newer version of %1 available. You can download it from %2.").arg(applicationName()).arg(_toolbox->corePlugin()->stableDownloadLocation()), tr("New Version Available"));
                }
            }
        }
    } else {
        qCDebug(QGCApplicationLog) << "Download QGC stable version failed" << errorMsg;
    }

    sender()->deleteLater();
}

bool QGCApplication::_parseVersionText(const QString& versionString, int& majorVersion, int& minorVersion, int& buildVersion)
{
    static const QRegularExpression regExp("v(\\d+)\\.(\\d+)\\.(\\d+)");
    const QRegularExpressionMatch match = regExp.match(versionString);
    if (match.hasMatch() && match.lastCapturedIndex() == 3) {
        majorVersion = match.captured(1).toInt();
        minorVersion = match.captured(2).toInt();
        buildVersion = match.captured(3).toInt();
        return true;
    }

    return false;
}

QString QGCApplication::cachedParameterMetaDataFile(void)
{
    QSettings settings;
    const QDir parameterDir = QFileInfo(settings.fileName()).dir();
    return parameterDir.filePath(QStringLiteral("ParameterFactMetaData.xml"));
}

QString QGCApplication::cachedAirframeMetaDataFile(void)
{
    QSettings settings;
    const QDir airframeDir = QFileInfo(settings.fileName()).dir();
    return airframeDir.filePath(QStringLiteral("PX4AirframeFactMetaData.xml"));
}

/// Returns a signal index that is can be compared to QMetaCallEvent.signalId
int QGCApplication::CompressedSignalList::_signalIndex(const QMetaMethod & method)
{
    if (method.methodType() != QMetaMethod::Signal) {
        qCWarning(QGCApplicationLog) << "Internal error: QGCApplication::CompressedSignalList::_signalIndex not a signal" << method.methodType();
        return -1;
    }

    int index = -1;
    const QMetaObject* metaObject = method.enclosingMetaObject();
    for (int i=0; i<=method.methodIndex(); i++) {
        if (metaObject->method(i).methodType() != QMetaMethod::Signal) {
            continue;
        }
        index++;
    }
    return index;
}

void QGCApplication::CompressedSignalList::add(const QMetaMethod & method)
{
    const QMetaObject*  metaObject  = method.enclosingMetaObject();
    int                 signalIndex = _signalIndex(method);

    if (signalIndex != -1 && !contains(metaObject, signalIndex)) {
        _signalMap[method.enclosingMetaObject()].insert(signalIndex);
    }
}

void QGCApplication::CompressedSignalList::remove(const QMetaMethod & method)
{
    const int signalIndex = _signalIndex(method);
    const QMetaObject*  metaObject  = method.enclosingMetaObject();

    if (signalIndex != -1 && _signalMap.contains(metaObject) && _signalMap[metaObject].contains(signalIndex)) {
        _signalMap[metaObject].remove(signalIndex);
        if (_signalMap[metaObject].count() == 0) {
            _signalMap.remove(metaObject);
        }
    }
}

bool QGCApplication::CompressedSignalList::contains(const QMetaObject* metaObject, int signalIndex)
{
    return _signalMap.contains(metaObject) && _signalMap[metaObject].contains(signalIndex);
}

void QGCApplication::addCompressedSignal(const QMetaMethod & method)
{
    _compressedSignals.add(method);
}

void QGCApplication::removeCompressedSignal(const QMetaMethod & method)
{
    _compressedSignals.remove(method);
}

bool QGCApplication::compressEvent(QEvent*event, QObject* receiver, QPostEventList* postedEvents)
{
    if (event->type() != QEvent::MetaCall) {
        return QApplication::compressEvent(event, receiver, postedEvents);
    }

    const QMetaCallEvent* mce = static_cast<QMetaCallEvent*>(event);
    if (!mce->sender() || !_compressedSignals.contains(mce->sender()->metaObject(), mce->signalId())) {
        return QApplication::compressEvent(event, receiver, postedEvents);
    }

    for (QPostEventList::iterator it = postedEvents->begin(); it != postedEvents->end(); ++it) {
        QPostEvent &cur = *it;
        if (cur.receiver != receiver || cur.event == 0 || cur.event->type() != event->type()) {
            continue;
        }
        const QMetaCallEvent *cur_mce = static_cast<QMetaCallEvent*>(cur.event);
        if (cur_mce->sender() != mce->sender() || cur_mce->signalId() != mce->signalId() || cur_mce->id() != mce->id()) {
            continue;
        }
        /* Keep The Newest Call */
        // We can't merely qSwap the existing posted event with the new one, since QEvent
        // keeps track of whether it has been posted. Deletion of a formerly posted event
        // takes the posted event list mutex and does a useless search of the posted event
        // list upon deletion. We thus clear the QEvent::posted flag before deletion.
        struct EventHelper : private QEvent {
            static void clearPostedFlag(QEvent * ev) {
                (&static_cast<EventHelper*>(ev)->t)[1] &= ~0x8001; // Hack to clear QEvent::posted
            }
        };
        EventHelper::clearPostedFlag(cur.event);
        delete cur.event;
        cur.event = event;
        return true;
    }

    return false;
}

bool QGCApplication::event(QEvent *e)
{
    if (e->type() == QEvent::Quit) {
        // On OSX if the user selects Quit from the menu (or Command-Q) the ApplicationWindow does not signal closing. Instead you get a Quit event here only.
        // This in turn causes the standard QGC shutdown sequence to not run. So in this case we close the window ourselves such that the
        // signal is sent and the normal shutdown sequence runs.
        const bool forceClose = _mainRootWindow->property("_forceClose").toBool();
        qCDebug(QGCApplicationLog) << "Quit event" << forceClose;
        // forceClose
        //  true:   Standard QGC shutdown sequence is complete. Let the app quit normally by falling through to the base class processing.
        //  false:  QGC shutdown sequence has not been run yet. Don't let this event close the app yet. Close the main window to kick off the normal shutdown.
        if (!forceClose) {
            //
            _mainRootWindow->close();
            e->ignore();
            return true;
        }
    }
    return QApplication::event(e);
}

QGCImageProvider* QGCApplication::qgcImageProvider()
{
    return dynamic_cast<QGCImageProvider*>(_qmlAppEngine->imageProvider(qgcImageProviderId));
}

void QGCApplication::shutdown()
{
    qCDebug(QGCApplicationLog) << "Exit";
    // This is bad, but currently qobject inheritances are incorrect and cause crashes on exit without
    delete _qmlAppEngine;
}

QString QGCApplication::numberToString(quint64 number)
{
    return getCurrentLanguage().toString(number);
}

QString QGCApplication::bigSizeToString(quint64 size)
{
    QString result;
    const QLocale kLocale = getCurrentLanguage();
    if (size < 1024) {
        result = kLocale.toString(size);
    } else if (size < pow(1024, 2)) {
        result = kLocale.toString(static_cast<double>(size) / 1024.0, 'f', 1) + "kB";
    } else if (size < pow(1024, 3)) {
        result = kLocale.toString(static_cast<double>(size) / pow(1024, 2), 'f', 1) + "MB";
    } else if (size < pow(1024, 4)) {
        result = kLocale.toString(static_cast<double>(size) / pow(1024, 3), 'f', 1) + "GB";
    } else {
        result = kLocale.toString(static_cast<double>(size) / pow(1024, 4), 'f', 1) + "TB";
    }
    return result;
}

QString QGCApplication::bigSizeMBToString(quint64 size_MB)
{
    QString result;
    const QLocale kLocale = getCurrentLanguage();
    if (size_MB < 1024) {
        result = kLocale.toString(static_cast<double>(size_MB) , 'f', 0) + " MB";
    } else if(size_MB < pow(1024, 2)) {
        result = kLocale.toString(static_cast<double>(size_MB) / 1024.0, 'f', 1) + " GB";
    } else {
        result = kLocale.toString(static_cast<double>(size_MB) / pow(1024, 2), 'f', 2) + " TB";
    }
    return result;
}
