#include "archive.h"
#include <QDir>
#include <QMap>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>
#include <zlib.h>

namespace pet::packs {
bool portablePath(const QString &path) {
    static const QRegularExpression component("^[A-Za-z0-9_-][A-Za-z0-9._ -]*$");
    static const QRegularExpression reserved("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    if (path.isEmpty() || path.size() > 240) return false;
    for (const auto &part : path.split('/'))
        if (!component.match(part).hasMatch() || part.endsWith('.') || part.endsWith(' ') || reserved.match(part).hasMatch()) return false;
    return true;
}
namespace {
quint16 u16(const QByteArray &b, int n) { return qFromLittleEndian<quint16>(b.constData() + n); }
quint32 u32(const QByteArray &b, int n) { return qFromLittleEndian<quint32>(b.constData() + n); }
constexpr qint64 MiB = 1024 * 1024;
struct Entry { QByteArray name; QString path; quint32 crc, packed, size, offset; quint16 method, flags; bool directory; };
}
bool extract(const QString &path, const QString &destination, QString &error) {
    auto fail = [&](const QString &why) { error = why; return false; };
    QFile zip(path);
    if (!zip.open(QIODevice::ReadOnly) || zip.size() > 544 * MiB || zip.size() < 22) return fail("Cannot read archive, or archive exceeds 544 MiB.");
    const qint64 length = zip.size();
    zip.seek(qMax(qint64(0), length - 65557));
    const auto tail = zip.readAll();
    int end = -1;
    for (int i = tail.size() - 22; i >= 0; --i)
        if (u32(tail, i) == 0x06054b50 && i + 22 + u16(tail, i + 20) == tail.size()) { end = i; break; }
    if (end < 0) return fail("Missing ZIP directory.");
    const auto count = u16(tail, end + 10);
    const qint64 centralSize = u32(tail, end + 12), central = u32(tail, end + 16);
    if (u16(tail, end + 4) || u16(tail, end + 6) || count != u16(tail, end + 8) || !count || count > 20000
        || central + centralSize != length - tail.size() + end) return fail("Unsupported ZIP layout, ZIP64, or entry count.");
    QVector<Entry> entries; QSet<QString> names, files; QSet<QString> directories; QMap<QString, QString> spellings;
    qint64 total = 0, png = 0, terms = 0, json = 0;
    zip.seek(central);
    for (int i = 0; i < count; ++i) {
        const auto h = zip.read(46);
        if (h.size() != 46 || u32(h, 0) != 0x02014b50) return fail("Invalid ZIP entry.");
        Entry e; e.flags = u16(h, 8); e.method = u16(h, 10); e.crc = u32(h, 16);
        e.packed = u32(h, 20); e.size = u32(h, 24); e.offset = u32(h, 42);
        const auto nameSize = u16(h, 28), extraSize = u16(h, 30), commentSize = u16(h, 32);
        if (!nameSize || nameSize > 241 || u16(h, 34) || (e.flags & ~quint16(0x808)) || (e.method != 0 && e.method != 8)) return fail("Unsupported ZIP encoding, encryption or compression.");
        e.name = zip.read(nameSize);
        QStringDecoder decoder(QStringDecoder::Utf8); e.path = decoder(e.name);
        if (decoder.hasError() || e.name.size() != nameSize) return fail("Invalid ZIP filename.");
        e.directory = e.path.endsWith('/'); if (e.directory) e.path.chop(1);
        const auto mode = (u32(h, 38) >> 16) & 0170000;
        if ((mode && mode != (e.directory ? 0040000 : 0100000)) || !portablePath(e.path)) return fail("Unsafe ZIP path or file type: " + e.path);
        auto spelling = e.path;
        while (!spelling.isEmpty()) {
            const auto folded = spelling.toCaseFolded();
            if (spellings.contains(folded) && spellings.value(folded) != spelling) return fail("Case-insensitive ZIP path collision.");
            spellings.insert(folded, spelling);
            spelling = spelling.contains('/') ? spelling.left(spelling.lastIndexOf('/')) : QString();
        }
        const auto key = e.path.toCaseFolded();
        if (names.contains(key) || (e.directory ? files.contains(key) : directories.contains(key))) return fail("Duplicate or conflicting ZIP path: " + e.path);
        names.insert(key);
        if (e.directory) directories.insert(key); else files.insert(key);
        auto parent = key;
        while (parent.contains('/')) { parent = parent.left(parent.lastIndexOf('/')); if (files.contains(parent)) return fail("ZIP file/directory collision."); directories.insert(parent); }
        qint64 limit = 8 * MiB;
        if (e.directory) { if (e.size || e.packed) return fail("Nonempty ZIP directory."); }
        else if (e.path.endsWith(".png")) { limit = 4 * MiB; png += e.size; }
        else if (e.path.endsWith(".txt") || e.path.endsWith(".md")) { limit = MiB; terms += e.size; }
        else if (e.path.endsWith(".json")) {
            if (QStringList{"pack.json", "pet.json", "plugin.json", "events.json"}.contains(e.path)) limit = 64 * 1024;
            else if (e.path != "animations.json") json += e.size;
        } else return fail("Unsupported pack file: " + e.path);
        total += e.size;
        if (e.size > limit || total > 544 * MiB || png > 512 * MiB || terms > MiB || json > 8 * MiB
            || e.size > qMax(qint64(1), qint64(e.packed)) * 200 || e.offset + qint64(30) + e.packed > central)
            return fail("Archive exceeds file, byte or 200:1 expansion limits.");
        if (!zip.seek(zip.pos() + extraSize + commentSize) || zip.pos() > central + centralSize) return fail("Invalid ZIP directory size.");
        entries.append(e);
    }
    if (zip.pos() != central + centralSize) return fail("Invalid ZIP directory size.");
    // Sorted local records must not overlap or hide unlisted records.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.offset < b.offset; });
    qint64 previous = 0;
    for (const auto &e : entries) {
        if (e.offset != previous || !zip.seek(e.offset)) return fail("Overlapping or unlisted ZIP records.");
        const auto h = zip.read(30);
        if (h.size() != 30 || u32(h, 0) != 0x04034b50 || u16(h, 6) != e.flags || u16(h, 8) != e.method
            || u16(h, 26) != e.name.size() || zip.read(e.name.size()) != e.name) return fail("ZIP local header mismatch.");
        if (!(e.flags & 8) && (u32(h, 14) != e.crc || u32(h, 18) != e.packed || u32(h, 22) != e.size)) return fail("ZIP size/checksum mismatch.");
        if (!zip.seek(zip.pos() + u16(h, 28)) || zip.pos() + e.packed > central) return fail("Invalid ZIP payload.");
        const auto target = destination + '/' + e.path;
        if (e.directory) { if (!QDir().mkpath(target)) return fail("Cannot create staging directory."); }
        else {
            if (!QDir().mkpath(QFileInfo(target).path())) return fail("Cannot create staging directory.");
            QFile out(target); if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Cannot create staged file.");
            z_stream stream{};
            if (e.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) return fail("Cannot initialize ZIP decoder.");
            qint64 left = e.packed, written = 0; uLong crc = crc32(0, nullptr, 0); bool good = true; int result = Z_OK;
            while (left > 0 && good) {
                const auto input = zip.read(qMin(left, qint64(65536)));
                if (input.isEmpty()) { good = false; break; }
                left -= input.size();
                stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData())); stream.avail_in = input.size();
                do {
                    char buffer[65536]; qint64 size;
                    if (e.method == 8) {
                        stream.next_out = reinterpret_cast<Bytef *>(buffer); stream.avail_out = sizeof buffer;
                        result = inflate(&stream, Z_NO_FLUSH); size = sizeof buffer - stream.avail_out;
                        if (result != Z_OK && result != Z_STREAM_END) { good = false; break; }
                    } else { size = input.size(); std::copy(input.begin(), input.end(), buffer); stream.avail_in = 0; }
                    written += size;
                    if (written > e.size || out.write(buffer, size) != size) { good = false; break; }
                    crc = crc32(crc, reinterpret_cast<const Bytef *>(buffer), size);
                    if (result == Z_STREAM_END) { if (left || stream.avail_in) good = false; break; }
                } while (stream.avail_in || (e.method == 8 && stream.avail_out == 0));
            }
            if (e.method == 8) inflateEnd(&stream);
            if (!good || written != e.size || crc != e.crc || (e.method == 8 && result != Z_STREAM_END)) return fail("Corrupt ZIP data or checksum.");
        }
        if (e.flags & 8) {
            auto descriptor = zip.read(12);
            if (descriptor.size() != 12) return fail("Invalid ZIP descriptor.");
            if (u32(descriptor, 0) == 0x08074b50) descriptor = descriptor.mid(4) + zip.read(4);
            if (descriptor.size() != 12 || u32(descriptor, 0) != e.crc || u32(descriptor, 4) != e.packed || u32(descriptor, 8) != e.size) return fail("ZIP descriptor mismatch.");
        }
        previous = zip.pos();
    }
    if (previous != central) return fail("Unlisted ZIP data.");
    return true;
}
}
