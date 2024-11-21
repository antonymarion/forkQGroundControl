#include <QtCore/QObject>
#include <gst/gst.h>
#include <thread>
#include <gst/app/gstappsink.h>

class DGGstreamerThread : public QObject 
{
    Q_OBJECT

public slots:
    void process();

private:

    //======================================================================================================================
    /// Our global data, serious gstreamer apps should always have this !
    struct GlobalData {
        GstElement *pipeline = nullptr;
        GstElement *sinkVideo = nullptr;
    };

    void codeThreadBus(GstElement *pipeline, GlobalData &data, QString prefix);
    bool busProcessMsg(GstElement *pipeline, GstMessage *msg, QString prefix);
    GlobalData data;
};