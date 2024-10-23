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
#include <QtNetwork/QNetworkProxyFactory>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlApplicationEngine>
#include <QTimer>
#include <QtMqtt/QtMqtt>
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttMessage>
#include <QtMqtt/QMqttSubscription>
#include <QJsonObject>

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
    this->commandsList << "OPEN_STREAM" << "STOP_STREAM" << "RESET_GIMBAL" << "MOVE_GIMBAL" << "GET_CAMERAS" << "SET_CAMERA" << "SET_CAMERA_INTRINSICS" << "GET_CAMERA" << "ZOOM_CAMERA" << "TAKE_PHOTO" << "START_RECORDING" << "STOP_RECORDING";

    // Setup MqttClient
    m_client = new QMqttClient(this);
    m_client->setHostname("tcp://152.228.246.204");
    m_client->setPort(1883);
    connect(m_client, &QMqttClient::stateChanged, this, &QGCApplication::updateLogStateChange);
    connect(m_client, &QMqttClient::disconnected, this, &QGCApplication::brokerDisconnected);
    connect(m_client, &QMqttClient::connected, this, &QGCApplication::brokerConnected);
    m_client->connectToHost();

    // Setup Subscription
    QString topic = "REQUEST/+/" + this->uavSn + "/+";
    QMqttSubscription *subscription = m_client->subscribe(topic, 1); // TODO regex here
    if(!subscription) {
        qCWarning(QGCApplicationLog) << "============== here ==============";
    }
    // updateStatus(subscription->state());
    // QObject::connect(subscription, &QMqttSubscription::stateChanged, this, &QGCApplication::updateStatus);
    QObject::connect(subscription, &QMqttSubscription::messageReceived, this, &QGCApplication::updateMessage);

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
    qCWarning(QGCApplicationLog) << "Mqtt Connected";
}

void QGCApplication::brokerDisconnected()
{
    qCWarning(QGCApplicationLog) << "Mqtt Disconnected";
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
            break;
        case 1:
            qCWarning(QGCApplicationLog) << "=================================================";
            qCWarning(QGCApplicationLog) << "recieved STOP_STREAM";
            qCWarning(QGCApplicationLog) << "=================================================";
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
                if(activeCamera->modelName() != "Simulated Camera"){
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

    // QGCApplication::sendAircraftPositionInfos();
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
    newResponse.insert("simulated", false);
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
        MavlinkCameraControl *activeCamera = activeVehicle->cameraManager()->currentCameraInstance();
        if(activeCamera) {
            qCWarning(QGCApplicationLog) << "============== current camera values ==============";
            newResponse.insert("sensorName", activeCamera->modelName());
            newResponse.insert("hasZoom", activeCamera->hasZoom());
            if(activeCamera->modelName() != "Simulated Camera"){
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
    qCWarning(QGCApplicationLog) << "batteryPowerPercentUav : " << res/batteries->count();
    newResponse.insert("batteryPowerPercentUav", res/batteries->count());

    QJsonDocument doc(newResponse);
    QString responseMessage(doc.toJson(QJsonDocument::Compact));
    m_client->publish("REMOTE_PILOT/"+this->uavSn, responseMessage.toUtf8());


    // Might not do that
    
    /* newResponse.insert("batteryPowerPercentRC", batteryRCLevel); // TODO
    newResponse.insert("batteryBehavior",batteryBehavior); // TODO
    currentValues.insert("sharpness", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("orientation", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("videoResolutionAndFrameRate", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("videoFileFormat", Vehicule::cameraManager().currentCameraInstance().); // TODO
    currentValues.insert("photoFileFormat", Vehicule::cameraManager().currentCameraInstance().); // TODO
    newResponse.insert("isZooming", Vehicule::cameraManager().currentCameraInstance().); // TODO
    newResponse.insert("isMovingGimbal", Vehicule::gimbalController().activeGimbal()); // TODO */
}
        /* 
        Log.d("POSITION",newResponse.toString());
        MqttMessage message = new MqttMessage(newResponse.toString().getBytes(StandardCharsets.UTF_8));
        try {
            mqttClient.publish("POSITION/"+uavSn, message);
        } catch (MqttException e) {
            Log.e("POSITION", "************** Exception **************");
            Log.e("POSITION", String.valueOf(e));
        }
    }

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
        case "GET_CAMERA": // TODO to finish
            Log.i("getCam", "=================================================");
            Log.i("getCam", "recieved GET_CAMERA");
            Log.i("getCam", "=================================================");
            obj.put("gimbalRange", GimbalUtil.gimbalRange);
            obj.put("gimbalSN", GimbalUtil.gimbalSN);
            obj.put("hasZoom", CameraUtil.hasZoom);
            obj.put("hasLens", CameraUtil.hasLens);
            obj.put("isoRange", CameraUtil.ISO);
            obj.put("aperture", CameraUtil.aperture);
            obj.put("photoFileFormatList", CameraUtil.photoFormats);
            obj.put("videoFileFormatList", CameraUtil.videoFormats);
            obj.put("streamSource", CameraUtil.streamSource);
            obj.put("zoomRatiosRange", new int[]{1});
            Log.d("GET_CAMERA", "end");
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
    MavlinkCameraControl *activeCamera = activeVehicle->cameraManager()->currentCameraInstance();
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

void QGCApplication::startStream(){
    qCWarning(QGCApplicationLog) << "==============  START OPEN_STREAM  ==============";
    MavlinkCameraControl *activeCamera = QGCApplication::getActiveCamera();
    if(!activeCamera) return;
    QGCVideoStreamInfo *streamInstance = activeCamera->currentStreamInstance();
    if(!streamInstance) return;
    qCWarning(QGCApplicationLog) << "stream name : " <<streamInstance->name();
    qCWarning(QGCApplicationLog) << "stream uri : " <<streamInstance->uri();
    qCWarning(QGCApplicationLog) << "stream type : " <<streamInstance->type();
    qCWarning(QGCApplicationLog) << "==============   END OPEN_STREAM   ==============";
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
