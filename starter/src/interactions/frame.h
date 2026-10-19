#pragma once
#include <QByteArray>
#include <QList>
#include "interactions/model.h"

namespace pet::interactions {

// Length-prefixed frames: a 4-byte big-endian payload size, then that many bytes of UTF-8 JSON.
QByteArray encodeFrame(const QByteArray &payload);

// Incremental decoder. The size is checked before any payload is buffered, so a hostile length never
// allocates; once a frame is oversized or empty the stream is poisoned and the connection must be closed.
class FrameDecoder {
public:
    explicit FrameDecoder(int maxPayload = Limits::frameBytes) : max_(maxPayload) {}
    // Appends bytes; returns false when the stream is invalid. Complete payloads are queued.
    bool feed(const QByteArray &bytes);
    bool next(QByteArray &payload);
    bool failed() const { return failed_; }
    int buffered() const { return int(buffer_.size()); }
private:
    int max_;
    QByteArray buffer_;
    QList<QByteArray> ready_;
    bool failed_ = false;
};

}
