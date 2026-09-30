#pragma once

#include "ItemTypes.h"

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtMultimedia/QAudioFormat>

class QAudioSink;
class QBuffer;

class AudioEngine : public QObject {
public:
    explicit AudioEngine(QObject* parent = nullptr);

    void prewarm();
    void playMatPitch(ItemKind kind, Direction facing);
    void playPercussion(PercussionKind kind);

private:
    struct Playback {
        QByteArray pcm;
        QBuffer* buffer = nullptr;
        QAudioSink* sink = nullptr;
    };

    void playPcm(const QByteArray& pcm);
    QByteArray allocatePcm(int sampleCount) const;
    void writeSample(QByteArray& data, int index, double value) const;
    QByteArray renderTone(double frequency, int durationMs, double volume) const;
    QByteArray renderBooBox() const;
    QByteArray renderBigDrum() const;
    const QByteArray& toneForMidi(int midi);
    const QByteArray& percussionPcm(PercussionKind kind);

    QAudioFormat m_format;
    QHash<int, QByteArray> m_toneCache;
    QHash<int, QByteArray> m_percussionCache;
    bool m_prewarmed = false;
};
