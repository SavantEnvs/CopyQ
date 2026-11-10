// Fuzzes CopyQ's command import (src/common/commandstore.cpp): the INI text a user pastes or loads in the
// Commands dialog, the export/re-import round trip, and the binary Command stream (src/common/command.cpp).
#include "harness_common.h"

#include "common/command.h"
#include "common/commandstore.h"

#include <QByteArray>
#include <QDataStream>
#include <QString>

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    harnessInit();
    return 0;
}

// First byte: bit 0 = binary Command stream, clear = INI text.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
        return -1;
    const QByteArray payload(reinterpret_cast<const char *>(data) + 1, static_cast<qsizetype>(size) - 1);

    if (data[0] & 1) {
        QDataStream in(payload);
        Command command;
        in >> command;
        return in.status() == QDataStream::Ok ? 0 : -1;
    }

    const Commands commands = importCommandsFromText(QString::fromUtf8(payload));
    if (commands.isEmpty())
        return -1;
    importCommandsFromText(exportCommands(commands));
    return 0;
}
