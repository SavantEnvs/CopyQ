#pragma once

#include <QCoreApplication>
#include <QtGlobal>

// One QCoreApplication per process; Qt's log output is dropped so a run is not dominated by stderr writes.
inline void harnessInit()
{
    static int argc = 1;
    static char arg0[] = "copyq-fuzz";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
}
