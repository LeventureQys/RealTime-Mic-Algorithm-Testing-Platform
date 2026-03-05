#pragma once

#include <QWidget>
#include <QPainter>
#include <QVector>
#include <QFont>
#include <cmath>

/**
 * Real-time magnitude spectrum display.
 * Accepts a vector of dBFS magnitudes (FFT bins, half spectrum).
 * Renders as a filled area plot with frequency axis labels.
 */
class SpectrumWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_sampleRate(48000)
        , m_dbMin(-80.0f)
        , m_dbMax(0.0f)
    {
        setMinimumSize(200, 100);
        // Smooth decay: blend new frame with previous
        m_smoothFactor = 0.4f;
    }

    void setSampleRate(int sr)  { m_sampleRate = sr; }
    void setDbRange(float min, float max) { m_dbMin = min; m_dbMax = max; update(); }

public slots:
    void updateSpectrum(const QVector<float>& magnitudes) {
        if (magnitudes.isEmpty()) return;

        if (m_smoothed.size() != magnitudes.size()) {
            m_smoothed = magnitudes;
        } else {
            // Exponential smoothing
            for (int i = 0; i < magnitudes.size(); ++i) {
                m_smoothed[i] = m_smoothed[i] * (1.0f - m_smoothFactor)
                              + magnitudes[i] * m_smoothFactor;
            }
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const int w  = width();
        const int h  = height();

        // Background gradient
        QLinearGradient bg(0, 0, 0, h);
        bg.setColorAt(0.0, QColor(18, 20, 28));
        bg.setColorAt(1.0, QColor(12, 14, 20));
        p.fillRect(rect(), bg);

        if (m_smoothed.isEmpty()) {
            drawNoSignal(p, w, h);
            return;
        }

        const int N       = m_smoothed.size();   // FFT_SIZE / 2
        const int padL    = 42;  // left for dB labels
        const int padR    = 8;
        const int padT    = 8;
        const int padB    = 20; // bottom for freq labels
        const int plotW   = w - padL - padR;
        const int plotH   = h - padT - padB;

        // Grid lines  (dB)
        static const float dbTicks[] = {0, -10, -20, -40, -60, -80};
        QFont tickFont;
        tickFont.setPixelSize(9);
        p.setFont(tickFont);

        for (float db : dbTicks) {
            if (db < m_dbMin || db > m_dbMax) continue;
            float norm = (db - m_dbMin) / (m_dbMax - m_dbMin);
            int   gy   = padT + static_cast<int>((1.0f - norm) * plotH);
            p.setPen(QPen(QColor(40, 42, 55), 1, Qt::DashLine));
            p.drawLine(padL, gy, padL + plotW, gy);
            p.setPen(QColor(90, 95, 120));
            p.drawText(0, gy - 6, padL - 4, 12, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(static_cast<int>(db)));
        }

        // Frequency axis labels
        static const float freqTicks[] = {100, 500, 1000, 2000, 4000, 8000, 16000};
        p.setPen(QColor(90, 95, 120));
        for (float f : freqTicks) {
            if (f > m_sampleRate / 2.0f) continue;
            // Log mapping: bin index
            float binF  = f / (m_sampleRate / 2.0f) * N;  // linear for now
            // Use log-frequency x mapping
            float logX  = logFreqX(f, m_sampleRate / 2.0f, plotW);
            int   tx    = padL + static_cast<int>(logX);
            p.setPen(QPen(QColor(40, 42, 55), 1, Qt::DashLine));
            p.drawLine(tx, padT, tx, padT + plotH);
            p.setPen(QColor(90, 95, 120));
            QString label = f >= 1000.0f
                ? QString("%1k").arg(static_cast<int>(f / 1000))
                : QString::number(static_cast<int>(f));
            p.drawText(tx - 16, padT + plotH + 2, 32, 16,
                       Qt::AlignHCenter | Qt::AlignTop, label);
        }

        // Spectrum fill path
        QPainterPath fillPath;
        QPainterPath linePath;

        bool first = true;
        const float fMax = m_sampleRate / 2.0f;

        for (int i = 1; i < N; ++i) {
            float freq = static_cast<float>(i) / N * fMax;
            float lx   = logFreqX(freq, fMax, plotW);
            int   px   = padL + static_cast<int>(lx);
            if (px < padL || px > padL + plotW) continue;

            float db   = qBound(m_dbMin, m_smoothed[i], m_dbMax);
            float norm = (db - m_dbMin) / (m_dbMax - m_dbMin);
            int   py   = padT + static_cast<int>((1.0f - norm) * plotH);

            if (first) {
                fillPath.moveTo(px, padT + plotH);
                fillPath.lineTo(px, py);
                linePath.moveTo(px, py);
                first = false;
            } else {
                fillPath.lineTo(px, py);
                linePath.lineTo(px, py);
            }
        }
        fillPath.lineTo(padL + plotW, padT + plotH);
        fillPath.closeSubpath();

        // Gradient fill
        QLinearGradient fillGrad(0, padT, 0, padT + plotH);
        fillGrad.setColorAt(0.0, QColor(80, 160, 255, 180));
        fillGrad.setColorAt(0.5, QColor(40, 100, 200, 120));
        fillGrad.setColorAt(1.0, QColor(20,  60, 140,  60));
        p.setBrush(fillGrad);
        p.setPen(Qt::NoPen);
        p.drawPath(fillPath);

        // Line on top
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(130, 200, 255, 220), 1.5f));
        p.drawPath(linePath);

        // Border
        p.setPen(QPen(QColor(50, 55, 70), 1));
        p.drawRect(padL, padT, plotW, plotH);

        // Title
        p.setPen(QColor(140, 145, 170));
        QFont titleFont;
        titleFont.setPixelSize(10);
        p.setFont(titleFont);
        p.drawText(padL, 0, plotW, padT, Qt::AlignHCenter | Qt::AlignVCenter,
                   "Spectrum (dBFS)");
    }

private:
    QVector<float> m_smoothed;
    int            m_sampleRate;
    float          m_dbMin;
    float          m_dbMax;
    float          m_smoothFactor;

    // Logarithmic frequency → x pixel mapping
    static float logFreqX(float freq, float fMax, int plotW) {
        const float fMin = 20.0f;
        if (freq <= fMin) return 0.0f;
        float logMin = std::log10(fMin);
        float logMax = std::log10(fMax);
        float logF   = std::log10(freq);
        return (logF - logMin) / (logMax - logMin) * plotW;
    }

    void drawNoSignal(QPainter& p, int w, int h) {
        p.setPen(QColor(70, 75, 95));
        p.drawText(rect(), Qt::AlignCenter, "No Signal");
    }
};
