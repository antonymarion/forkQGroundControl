#include "DGGstreamerThread.h"
#include <gst/gst.h>
#include <thread>
#include <gst/app/gstappsink.h>

//======================================================================================================================
/// Process a single bus message, log messages, exit on error, return false on eof
bool QGCApplication::busProcessMsg(GstElement *pipeline, GstMessage *msg, QString prefix) {
    GstMessageType mType = GST_MESSAGE_TYPE(msg);
    qCWarning(QGCApplicationLog) << "[" << prefix << "] : mType = " << mType << " ";
    GError *err = nullptr;
    gchar *dbg = nullptr;
    switch (mType) {
        case (GST_MESSAGE_ERROR):
            qCWarning(QGCApplicationLog) << " ERROR !";
            // Parse error and exit program, hard exit
            gst_message_parse_error(msg, &err, &dbg);
            if(err) {
                qCWarning(QGCApplicationLog) << "ERR = " << err->message << " FROM " << GST_OBJECT_NAME(msg->src);
                g_clear_error(&err);
            } else {
                qCWarning(QGCApplicationLog) << "NO ERR";
            }
            if(dbg) {
                qCWarning(QGCApplicationLog) << "DBG = " << dbg;
                g_free(dbg);
            } else {
                qCWarning(QGCApplicationLog) << "NO DBG";
            }
            return false;
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
void QGCApplication::codeThreadBus(GstElement *pipeline, GoblinData &data, QString prefix) {
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