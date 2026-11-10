// Known-answer tests for the CopyQ units the fuzz harnesses link: item serialization, command import and
// text helpers. Prints one "PASS <name>" / "FAIL <name>" line per check; mayhem/test.sh counts them.
#include "harness_common.h"

#include "common/command.h"
#include "common/commandstore.h"
#include "common/contenttype.h"
#include "common/textdata.h"
#include "item/clipboardmodel.h"
#include "item/serialize.h"

#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QStringList>
#include <QVariantMap>

#include <cstdio>

namespace {

int g_failed = 0;

void check(const char *name, bool ok)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failed;
}

QByteArray hex(const char *s) { return QByteArray::fromHex(s); }

// {"text/plain": "hello"} in the v2 format: -2, 1 entry, mime "4" (id 4 = text/plain), not compressed, "hello".
const char *const itemHello = "fffffffe" "00000001" "0000000200" "34" "00" "00000005" "68656c6c6f";
const char *const itemWorld = "fffffffe" "00000001" "0000000200" "34" "00" "00000005" "776f726c64";
// What serializeData writes for the same item: mime "text/" is abbreviated to "8" and "plain" kept literally.
const char *const writtenHello = "fffffffe" "00000001" "0000000c" "00380070006c00610069006e" "00" "00000005" "68656c6c6f";
const char *const writtenWorld = "fffffffe" "00000001" "0000000c" "00380070006c00610069006e" "00" "00000005" "776f726c64";

void testSerialize()
{
    QVariantMap data;
    data.insert("text/plain", QByteArray("hello"));
    check("serialize_v2_bytes", serializeData(data) == hex(writtenHello));

    QVariantMap out;
    check("deserialize_v2_ok", deserializeData(&out, hex(itemHello)));
    check("deserialize_v2_value", out.value("text/plain").toByteArray() == "hello" && out.size() == 1);

    QVariantMap many;
    many.insert("text/html", QByteArray("<b>x</b>"));
    many.insert("application/x-copyq-test", QByteArray("\0\1\2", 3));
    many.insert("image/png", QByteArray(1000, 'z'));
    QVariantMap back;
    check("roundtrip_ok", deserializeData(&back, serializeData(many)));
    check("roundtrip_equal", back == many);

    // Legacy v1: count, then (mime, qCompress'ed bytes).
    QByteArray v1;
    {
        QDataStream s(&v1, QIODevice::WriteOnly);
        s << qint32(1) << QString("text/plain") << qCompress(QByteArray("legacy"));
    }
    QVariantMap legacy;
    check("deserialize_v1", deserializeData(&legacy, v1) && legacy.value("text/plain").toByteArray() == "legacy");

    QVariantMap bad;
    check("reject_truncated", !deserializeData(&bad, hex(itemHello).left(20)));
    check("reject_negative_length", !deserializeData(&bad, hex("fffffffb")));
    check("reject_empty", !deserializeData(&bad, QByteArray()));
    check("reject_bad_mime_id", !deserializeData(&bad, hex("fffffffe" "00000001" "0000000200" "7a" "00" "00000000")));
}

void testItems()
{
    QByteArray tab = hex("00000002");
    tab += hex(itemHello);
    tab += hex(itemWorld);
    QByteArray written = hex("00000002");
    written += hex(writtenHello);
    written += hex(writtenWorld);

    ClipboardModel model;
    QBuffer in(&tab);
    in.open(QIODevice::ReadOnly);
    check("model_load_ok", deserializeData(&model, &in));
    check("model_rows", model.rowCount() == 2);
    const QVariantMap first = model.data(model.index(0), contentType::data).toMap();
    const QVariantMap second = model.data(model.index(1), contentType::data).toMap();
    check("model_item0", first.value("text/plain").toByteArray() == "hello");
    check("model_item1", second.value("text/plain").toByteArray() == "world");

    QByteArray saved;
    QBuffer out(&saved);
    out.open(QIODevice::WriteOnly);
    check("model_save_bytes", serializeData(model, &out) && saved == written);

    ClipboardModel truncatedModel;
    QByteArray cut = tab.left(tab.size() - 3);
    QBuffer cutIn(&cut);
    cutIn.open(QIODevice::ReadOnly);
    check("model_reject_truncated", !deserializeData(&truncatedModel, &cutIn));

    // One item with a FILE: reference: mime "0FILE:text/plain" (id 0 = literal), path stored as bytes.
    QByteArray fileTab = hex("00000001" "fffffffe" "00000001");
    {
        QDataStream s(&fileTab, QIODevice::Append);
        s << QString("0FILE:text/plain") << false << QByteArray("/tmp/copyq-x.dat");
    }
    QBuffer fileIn(&fileTab);
    fileIn.open(QIODevice::ReadOnly);
    QStringList files;
    check("files_ok", itemDataFiles(&fileIn, &files));
    check("files_value", files == QStringList{"/tmp/copyq-x.dat"});
}

void testCommands()
{
    const QString ini =
        "[Commands]\n"
        "1\\Name=Say hi\n"
        "1\\Command=\"\n    copyq: add('hi')\"\n"
        "1\\Input=text/plain\n"
        "1\\InMenu=true\n"
        "1\\Shortcut=ctrl+f1, ctrl+f2\n"
        "2\\Name=Ignore secrets\n"
        "2\\Match=^secret\n"
        "2\\Automatic=true\n"
        "2\\Remove=true\n"
        "2\\Name_cs=Ignorovat\n"
        "size=2\n";
    const Commands commands = importCommandsFromText(ini);
    check("commands_count", commands.size() == 2);
    if (commands.size() == 2) {
        check("commands_name", commands[0].name == "Say hi" && commands[1].name == "Ignore secrets");
        check("commands_cmd_normalized", commands[0].cmd == "copyq: add('hi')");
        check("commands_flags", commands[0].inMenu && !commands[0].automatic
              && commands[1].automatic && commands[1].remove);
        check("commands_input", commands[0].input == "text/plain");
        check("commands_shortcuts", commands[0].shortcuts == QStringList({"ctrl+f1", "ctrl+f2"}));
        check("commands_match", commands[1].re.pattern() == "^secret");
        check("commands_localization", commands[1].nameLocalization.value("cs") == "Ignorovat");

        const Commands again = importCommandsFromText(exportCommands(commands));
        check("commands_roundtrip", again == commands);
    }

    const Commands single = importCommandsFromText("[Command]\nName=Solo\nInput=true\nOutput=false\n");
    check("command_single", single.size() == 1 && single[0].name == "Solo"
          && single[0].input == "text/plain" && single[0].output.isEmpty());

    check("commands_garbage_empty", importCommandsFromText("not an ini file").isEmpty());

    Command c;
    c.name = "Streamed";
    c.cmd = "copyq: x";
    c.shortcuts = QStringList{"a"};
    QByteArray bytes;
    {
        QDataStream s(&bytes, QIODevice::WriteOnly);
        s << c;
    }
    QDataStream in(bytes);
    Command loaded;
    in >> loaded;
    check("command_stream_roundtrip", in.status() == QDataStream::Ok && loaded == c && loaded.name == "Streamed");
}

void testText()
{
    check("text_getTextData_nul", getTextData(QByteArray("a\0b", 3)).size() == 3);
    check("text_getTextData_utf8", getTextData(QByteArray("\xc4\x8d")) == QString::fromUtf8("\xc4\x8d"));
    check("text_accents", accentsRemoved(QString::fromUtf8("\xc4\x8c" "e\xc5\xa1tina")) == "Cestina");
    check("text_escapeHtml", escapeHtml("a b\n<") == "a&nbsp;b<br />&lt;");

    QVariantMap map;
    setTextData(&map, "abc");
    check("text_setTextData", map.value("text/plain").toByteArray() == "abc" && getTextData(map) == "abc");
    check("text_hash_stable", hash(map) == hash(createDataMap("text/plain", QString("abc"))));
    check("text_hash_differs", hash(map) != hash(createDataMap("text/plain", QString("abd"))));
}

} // namespace

int main()
{
    harnessInit();
    registerDataFileConverter();
    testSerialize();
    testItems();
    testCommands();
    testText();
    std::printf("SELFTEST failed=%d\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
