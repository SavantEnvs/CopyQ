// Fuzzes CopyQ's item serialization reader (src/item/serialize.cpp): the v1/v2 formats used for tab
// files, import/export and IPC, and the encrypted v3 envelope.
#include "harness_common.h"

#include "common/encryption.h"
#include "item/serialize.h"

#include <QByteArray>
#include <QDataStream>
#include <QVariantMap>

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

// First byte selects how the rest is read: bit 0 set = the rest is the plaintext of an encrypted (v3)
// envelope built with a process-local key, so the fuzzer reaches the v2 body parser under a key
// (DataFile + DEK import); clear = the rest is the raw stream.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
        return -1;
    const bool encrypted = (data[0] & 1) && g_haveKey;
    const QByteArray payload(reinterpret_cast<const char *>(data) + 1, static_cast<qsizetype>(size) - 1);

    QVariantMap map;
    bool ok;
    if (encrypted) {
        const QByteArray enc = Encryption::encrypt(payload, g_key);
        if (enc.isEmpty())
            return -1;
        QByteArray envelope;
        {
            QDataStream out(&envelope, QIODevice::WriteOnly);
            out << static_cast<qint32>(-3) << static_cast<qint32>(enc.size()) << enc;
        }
        ok = deserializeData(&map, envelope, &g_key);
    } else {
        ok = deserializeData(&map, payload);
    }
    if (!ok)
        return -1;

    // Whatever was accepted must serialize and read back.
    if (!encrypted) {
        QVariantMap again;
        deserializeData(&again, serializeData(map));
    }
    return 0;
}
