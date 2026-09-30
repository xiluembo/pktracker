#include "AudioEngine.h"

#include "AppConstants.h"

#include <QtCore/QBuffer>
#include <QtCore/QIODevice>
#include <QtMultimedia/QAudioSink>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

double smoothStep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

} // namespace

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

void AudioEngine::prewarm()
{
    if (m_prewarmed) {
        return;
    }

    for (ItemKind kind : {
             ItemKind::MusicLowDo,
             ItemKind::MusicRe,
             ItemKind::MusicMi,
             ItemKind::MusicFa,
             ItemKind::MusicSol,
             ItemKind::MusicLa,
             ItemKind::MusicTi,
             ItemKind::MusicHighDo,
         }) {
        for (Direction facing : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            toneForMidi(playedMidi(kind, facing));
        }
    }
    percussionPcm(PercussionKind::BooBox);
    percussionPcm(PercussionKind::BigDrum);
    m_prewarmed = true;
}

void AudioEngine::playMatPitch(ItemKind kind, Direction facing)
{
    playPcm(toneForMidi(playedMidi(kind, facing)));
}

void AudioEngine::playPercussion(PercussionKind kind)
{
    playPcm(percussionPcm(kind));
}

const QByteArray& AudioEngine::toneForMidi(int midi)
{
    auto it = m_toneCache.find(midi);
    if (it != m_toneCache.end()) {
        return it.value();
    }
    it = m_toneCache.insert(midi, renderTone(midiToFrequency(midi), 320, 0.20));
    return it.value();
}

const QByteArray& AudioEngine::percussionPcm(PercussionKind kind)
{
    const int key = static_cast<int>(kind);
    auto it = m_percussionCache.find(key);
    if (it != m_percussionCache.end()) {
        return it.value();
    }
    QByteArray pcm = (kind == PercussionKind::BigDrum) ? renderBigDrum() : renderBooBox();
    it = m_percussionCache.insert(key, std::move(pcm));
    return it.value();
}

void AudioEngine::playPcm(const QByteArray& pcm)
{
    auto* playback = new Playback;
    playback->pcm = pcm;
    playback->buffer = new QBuffer;
    playback->buffer->setData(playback->pcm);
    playback->buffer->open(QIODevice::ReadOnly);
    playback->sink = new QAudioSink(m_format, this);
    connect(playback->sink, &QAudioSink::stateChanged, this, [playback](QAudio::State state) {
        if (state == QAudio::IdleState) {
            playback->sink->stop();
            playback->buffer->close();
            playback->sink->deleteLater();
            playback->buffer->deleteLater();
            delete playback;
        }
    });
    playback->sink->start(playback->buffer);
}

QByteArray AudioEngine::allocatePcm(int sampleCount) const
{
    QByteArray data;
    data.resize(sampleCount * static_cast<int>(sizeof(qint16)));
    // QByteArray::resize does not zero-fill; leftover bytes become audible clicks.
    std::memset(data.data(), 0, static_cast<size_t>(data.size()));
    return data;
}

void AudioEngine::writeSample(QByteArray& data, int index, double value) const
{
    value = std::clamp(value, -1.0, 1.0);
    const auto sample = static_cast<qint16>(value * 32767.0);
    auto* raw = reinterpret_cast<qint16*>(data.data());
    raw[index] = sample;
}

QByteArray AudioEngine::renderTone(double frequency, int durationMs, double volume) const
{
    const int noteSamples = (m_format.sampleRate() * durationMs) / 1000;
    const int tailSilenceSamples = (m_format.sampleRate() * 12) / 1000;
    QByteArray data = allocatePcm(noteSamples + tailSilenceSamples);

    const int attackSamples = std::max(1, (m_format.sampleRate() * 6) / 1000);
    const int releaseSamples = std::max(1, (m_format.sampleRate() * 90) / 1000);
    const int releaseStart = std::max(0, noteSamples - releaseSamples);
    const std::array<double, 7> harmonicWeights = {1.00, 0.48, 0.28, 0.16, 0.09, 0.055, 0.03};

    for (int i = 0; i < noteSamples; ++i) {
        const double t = static_cast<double>(i) / m_format.sampleRate();
        double envelope = std::exp(-3.8 * t) * 0.82 + std::exp(-18.0 * t) * 0.18;
        if (i < attackSamples) {
            envelope *= smoothStep(static_cast<double>(i) / attackSamples);
        } else if (i >= releaseStart) {
            envelope *= smoothStep(static_cast<double>(noteSamples - i - 1) / releaseSamples);
        }
        double pianoWave = 0.0;
        double weightSum = 0.0;
        for (int harmonic = 1; harmonic <= static_cast<int>(harmonicWeights.size()); ++harmonic) {
            const double weight = harmonicWeights[harmonic - 1] * std::exp(-0.18 * harmonic * t);
            const double inharmonicity = 1.0 + 0.0009 * harmonic * harmonic;
            pianoWave += weight * std::sin(2.0 * kPi * frequency * harmonic * inharmonicity * t);
            weightSum += weight;
        }
        pianoWave /= std::max(0.0001, weightSum);
        // Hammer transient stays under the attack envelope so onset cannot spike.
        const double hammerClick = std::sin(2.0 * kPi * frequency * 9.0 * t) * std::exp(-95.0 * t) * 0.035;
        const double value = (pianoWave + hammerClick) * envelope * volume;
        writeSample(data, i, value);
    }
    return data;
}

QByteArray AudioEngine::renderBooBox() const
{
    const int durationMs = 90;
    const int samples = (m_format.sampleRate() * durationMs) / 1000;
    QByteArray data = allocatePcm(samples);
    const int attackSamples = std::max(1, (m_format.sampleRate() * 3) / 1000);
    quint32 state = 0x12345678u;
    for (int i = 0; i < samples; ++i) {
        state = 1664525u * state + 1013904223u;
        const double noise = (static_cast<int>((state >> 16) & 0xffff) / 32768.0) - 1.0;
        const double t = static_cast<double>(i) / m_format.sampleRate();
        const double chirp = std::sin(2.0 * kPi * (620.0 + 900.0 * t) * t);
        double envelope = std::exp(-35.0 * t);
        if (i < attackSamples) {
            envelope *= smoothStep(static_cast<double>(i) / attackSamples);
        }
        writeSample(data, i, (0.65 * noise + 0.35 * chirp) * envelope * 0.45);
    }
    return data;
}

QByteArray AudioEngine::renderBigDrum() const
{
    const int durationMs = 260;
    const int samples = (m_format.sampleRate() * durationMs) / 1000;
    QByteArray data = allocatePcm(samples);
    const int attackSamples = std::max(1, (m_format.sampleRate() * 2) / 1000);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / m_format.sampleRate();
        const double frequency = 95.0 * std::exp(-7.0 * t) + 42.0;
        double envelope = std::exp(-10.0 * t);
        if (i < attackSamples) {
            envelope *= smoothStep(static_cast<double>(i) / attackSamples);
        }
        const double thump = std::sin(2.0 * kPi * frequency * t);
        writeSample(data, i, thump * envelope * 0.72);
    }
    return data;
}
