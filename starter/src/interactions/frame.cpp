#include "interactions/frame.h"

namespace pet::interactions {

QByteArray encodeFrame(const QByteArray &payload) {
    const quint32 n = quint32(payload.size());
    QByteArray out;
    out.reserve(4 + payload.size());
    out.append(char(n >> 24)).append(char(n >> 16)).append(char(n >> 8)).append(char(n));
    return out + payload;
}

bool FrameDecoder::feed(const QByteArray &bytes) {
    if (failed_) return false;
    buffer_ += bytes;
    for (;;) {
        if (buffer_.size() < 4) return true;
        const auto *p = reinterpret_cast<const uchar *>(buffer_.constData());
        const quint32 n = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
        if (n == 0 || n > quint32(max_)) { failed_ = true; buffer_.clear(); ready_.clear(); return false; }
        if (buffer_.size() < qsizetype(4 + n)) return true;
        ready_.append(buffer_.mid(4, int(n)));
        buffer_.remove(0, int(4 + n));
    }
}

bool FrameDecoder::next(QByteArray &payload) {
    if (ready_.isEmpty()) return false;
    payload = ready_.takeFirst();
    return true;
}

}
