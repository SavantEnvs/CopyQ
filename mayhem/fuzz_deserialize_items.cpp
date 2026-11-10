// Fuzzes loading a tab file (src/item/serialize.cpp): the item list reader into a ClipboardModel and the
// file-reference enumeration used by the sync/encrypted-tab plugins.
#include "harness_common.h"

#include "common/encryption.h"
#include "item/clipboardmodel.h"
#include "item/serialize.h"

#include <QBuffer>
#include <QByteArray>
#include <QStringList>

namespace {
Encryption::EncryptionKey g_key;
bool g_haveKey = false;
}

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    harnessInit();
    registerDataFileConverter();
    if (Encryption::initialize())
        g_haveKey = g_key.generateRandomDEK();
    return 0;
}

// First byte: bit 0 = enumerate files instead of loading the model; bit 1 = pass the key.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
        return -1;
    const bool listFiles = data[0] & 1;
    const Encryption::EncryptionKey *key = ((data[0] & 2) && g_haveKey) ? &g_key : nullptr;
    QByteArray bytes(reinterpret_cast<const char *>(data) + 1, static_cast<qsizetype>(size) - 1);
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);

    if (listFiles) {
        QStringList files;
        return itemDataFiles(&buffer, &files, key) ? 0 : -1;
    }

    ClipboardModel model;
    return deserializeData(&model, &buffer, key) ? 0 : -1;
}
