#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QTimer>
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QQueue>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <cmath>
#include <vector>
#include <complex>
#include <algorithm>

// ============================================================
// Simple FFT (Cooley-Tukey, power-of-two)
// ============================================================
static void fft(std::vector<std::complex<float>>& x)
{
    const size_t N = x.size();
    if (N <= 1) return;
    std::vector<std::complex<float>> even(N / 2), odd(N / 2);
    for (size_t i = 0; i < N / 2; ++i) {
        even[i] = x[2 * i];
        odd[i]  = x[2 * i + 1];
    }
    fft(even);
    fft(odd);
    for (size_t k = 0; k < N / 2; ++k) {
        std::complex<float> t = std::polar(1.0f, static_cast<float>(-2.0 * M_PI * static_cast<double>(k) / static_cast<double>(N))) * odd[k];
        x[k]         = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

// ============================================================
// WAV file writer helper
// ============================================================
class WavWriter {
public:
    WavWriter() : file(nullptr), dataSize(0) {}

    bool open(const QString& path, int sampleRate, int channels) {
        file = new QFile(path);
        if (!file->open(QIODevice::WriteOnly)) {
            delete file;
            file = nullptr;
            return false;
        }
        dataSize = 0;
        this->sampleRate = sampleRate;
        this->channels   = channels;
        writeHeader(); // placeholder
        return true;
    }

    void write(const short* data, int sampleCount) {
        if (!file) return;
        file->write(reinterpret_cast<const char*>(data), sampleCount * sizeof(short));
        dataSize += sampleCount * sizeof(short);
    }

    void close() {
        if (!file) return;
        // Rewind and write real header
        file->seek(0);
        writeHeader();
        file->close();
        delete file;
        file = nullptr;
    }

    bool isOpen() const { return file && file->isOpen(); }

private:
    QFile*  file;
    quint32 dataSize;
    int     sampleRate;
    int     channels;

    void writeHeader() {
        QDataStream ds(file);
        ds.setByteOrder(QDataStream::LittleEndian);

        quint16 bitsPerSample = 16;
        quint16 blockAlign    = channels * bitsPerSample / 8;
        quint32 byteRate      = sampleRate * channels * bitsPerSample / 8;
        quint32 chunkSize     = 36 + dataSize;

        // RIFF
        file->write("RIFF", 4);
        ds << chunkSize;
        file->write("WAVE", 4);
        // fmt
        file->write("fmt ", 4);
        ds << quint32(16);          // sub-chunk size
        ds << quint16(1);           // PCM
        ds << quint16(channels);
        ds << quint32(sampleRate);
        ds << quint32(byteRate);
        ds << quint16(blockAlign);
        ds << quint16(bitsPerSample);
        // data
        file->write("data", 4);
        ds << dataSize;
    }
};

// ============================================================
// Audio output thread
// ============================================================
class AudioOutputThread : public QThread {
    Q_OBJECT

public:
    AudioOutputThread(QMutex* mutex, QWaitCondition* condition, QObject* parent = nullptr)
        : QThread(parent), mutex(mutex), condition(condition), audioSink(nullptr), outputIODevice(nullptr)
    {
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelConfig(QAudioFormat::ChannelConfigMono);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice outputDeviceInfo = QMediaDevices::defaultAudioOutput();
        if (!outputDeviceInfo.isFormatSupported(format)) {
            qDebug() << "Output format not supported";
        }

        audioSink      = new QAudioSink(outputDeviceInfo, format);
        outputIODevice = audioSink->start();
    }

    ~AudioOutputThread() {
        if (audioSink) {
            audioSink->stop();
            delete audioSink;
        }
    }

    void run() override {
        while (!isInterruptionRequested()) {
            mutex->lock();
            while (buffer.size() < 4800 && !isInterruptionRequested()) {
                condition->wait(mutex, 100);
            }
            if (isInterruptionRequested()) {
                mutex->unlock();
                break;
            }
            short output[4800];
            for (int i = 0; i < 4800; ++i) {
                output[i] = buffer.dequeue();
            }
            mutex->unlock();

            if (outputIODevice && outputIODevice->isWritable()) {
                outputIODevice->write(reinterpret_cast<const char*>(output), sizeof(output));
            }
        }
    }

    void enqueueData(const short* data, int size) {
        mutex->lock();
        for (int i = 0; i < size; ++i) {
            buffer.enqueue(data[i]);
        }
        condition->wakeAll();
        mutex->unlock();
    }

private:
    QQueue<short>    buffer;
    QMutex*          mutex;
    QWaitCondition*  condition;
    QAudioSink*      audioSink;
    QIODevice*       outputIODevice;
};

// ============================================================
// Main audio device  (QIODevice written to by QAudioSource)
// ============================================================
class AudioDevice_ : public QIODevice {
    Q_OBJECT

public:
    // FFT size for spectrum – must be power of 2
    static constexpr int FFT_SIZE = 1024;

    explicit AudioDevice_(QObject* parent = nullptr)
        : QIODevice(parent)
        , blnReturn(false)
        , blnDF(false)
        , blnRecording(false)
        , fftAccum(0)
    {
        outputThread = new AudioOutputThread(&mutex, &condition);
        outputThread->start();

        fftWindow.resize(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i) {
            // Hann window
            fftWindow[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
        }
        fftBuf.resize(FFT_SIZE, 0);
    }

    ~AudioDevice_() {
        stopRecording();
        outputThread->requestInterruption();
        outputThread->quit();
        outputThread->wait();
        delete outputThread;
    }

    bool open(OpenMode mode) override {
        return QIODevice::open(mode | QIODevice::ReadWrite);
    }

    qint64 readData(char* data, qint64 maxlen) override {
        Q_UNUSED(data); Q_UNUSED(maxlen);
        return 0;
    }

    qint64 writeData(const char* data, qint64 len) override {
        const short* samples   = reinterpret_cast<const short*>(data);
        qint64       sampleCnt = len / sizeof(short);

        for (qint64 i = 0; i < sampleCnt; ++i) {
            short s = samples[i];
            buffer.enqueue(s);

            // Feed FFT accumulation buffer
            fftBuf[fftAccum++] = s;
            if (fftAccum >= FFT_SIZE) {
                emitSpectrum();
                fftAccum = 0;
            }

            if (buffer.size() >= 480) {
                short input[480];
                short output[480];
                for (int j = 0; j < 480; ++j) input[j] = buffer.dequeue();

                if (blnDF) {
                    // Algorithm placeholder – copy for now
                    memcpy(output, input, 480 * sizeof(short));
                } else {
                    memcpy(output, input, 480 * sizeof(short));
                }

                // Level (input)
                float rms = computeRMS(input, 480);
                emit inputLevelChanged(rmsToDb(rms));

                // Level (output / processed)
                float rmsOut = computeRMS(output, 480);
                emit outputLevelChanged(rmsToDb(rmsOut));

                // Recording
                if (blnRecording) {
                    wavWriter.write(output, 480);
                }

                // Monitoring
                if (blnReturn) {
                    outputThread->enqueueData(output, 480);
                }
            }
        }
        return len;
    }

    // ---- public API ----
    void setReturn(bool v)  { blnReturn = v; }
    void setDF(bool v)      { blnDF     = v; }

    bool startRecording(const QString& path) {
        if (blnRecording) return false;
        if (!wavWriter.open(path, 48000, 1)) return false;
        blnRecording = true;
        return true;
    }

    void stopRecording() {
        if (!blnRecording) return;
        blnRecording = false;
        wavWriter.close();
    }

    bool isRecording() const { return blnRecording; }

signals:
    // dB value: 0 = full scale, -96 = silence
    void inputLevelChanged(float dBFS);
    void outputLevelChanged(float dBFS);
    // Magnitude spectrum in dB, FFT_SIZE/2 bins
    void spectrumUpdated(QVector<float> magnitudes);

private:
    QQueue<short>          buffer;
    AudioOutputThread*     outputThread;
    QMutex                 mutex;
    QWaitCondition         condition;
    bool                   blnReturn;
    bool                   blnDF;

    // Recording
    bool                   blnRecording;
    WavWriter              wavWriter;

    // FFT
    std::vector<float>     fftWindow;
    std::vector<short>     fftBuf;
    int                    fftAccum;

    static float computeRMS(const short* data, int n) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            double s = data[i] / 32768.0;
            sum += s * s;
        }
        return static_cast<float>(std::sqrt(sum / n));
    }

    static float rmsToDb(float rms) {
        if (rms < 1e-10f) return -96.0f;
        return 20.0f * std::log10(rms);
    }

    void emitSpectrum() {
        std::vector<std::complex<float>> cx(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i) {
            cx[i] = std::complex<float>(fftBuf[i] / 32768.0f * fftWindow[i], 0.0f);
        }
        fft(cx);

        QVector<float> mag(FFT_SIZE / 2);
        for (int i = 0; i < FFT_SIZE / 2; ++i) {
            float m = std::abs(cx[i]);
            mag[i]  = (m < 1e-10f) ? -96.0f : 20.0f * std::log10(m);
        }
        emit spectrumUpdated(mag);
    }
};

#endif // AUDIODEVICE_H
