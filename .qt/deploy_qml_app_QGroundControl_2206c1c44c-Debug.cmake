include(C:/Users/adrie/Desktop/Boulot/MAVLINK/forkQGroundControl/.qt/QtDeploySupport-Debug.cmake)
include("${CMAKE_CURRENT_LIST_DIR}/QGroundControl-plugins-Debug.cmake" OPTIONAL)
set(__QT_DEPLOY_ALL_MODULES_FOUND_VIA_FIND_PACKAGE "ZlibPrivate;EntryPointPrivate;Core;Network;Bluetooth;Gui;Widgets;OpenGL;OpenGLWidgets;Charts;Concurrent;Core5Compat;Positioning;QmlIntegration;Qml;QmlModels;Quick;PositioningQuick;QuickShapesPrivate;Location;Mqtt;Multimedia;QuickTemplates2;QuickControls2;QuickWidgets;Sensors;Sql;Svg;Test;TextToSpeech;Xml;SerialPort")

qt_deploy_qml_imports(TARGET QGroundControl PLUGINS_FOUND plugins_found)
qt_deploy_runtime_dependencies(
    EXECUTABLE C:/Users/adrie/Desktop/Boulot/MAVLINK/forkQGroundControl/Debug/QGroundControl.exe
    ADDITIONAL_MODULES ${plugins_found}
    GENERATE_QT_CONF
)