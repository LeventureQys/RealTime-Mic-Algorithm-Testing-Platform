#pragma once

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <cmath>

/**
 * Vertical stereo-style level meter with peak hold.
 * Feed it dBFS values (0 = full scale, -96 = silence).
 */
class LevelMeter : public QWidget {
    Q_OBJECT

public:
    explicit LevelMeter(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_level(-96.0f)
        , m_peak(-96.0f)
        , m_peakHoldMs(1500)
        , m_peakAge(0)
    {
        setMinimumSize(22, 80);

        m_peakTimer = new QTimer(this);
        m_peakTimer->setInterval(50);
        connect(m_peakTimer, &QTimer::timeout, this, [this]() {
            m_peakAge += 50;
            if (m_peakAge > m_peakHoldMs) {
                m_peak = qMax(m_peak - 1.5f, -96.0f);
                if (m_peak <= -96.0f) m_peakTimer->stop();
            }
            update();
        });
    }

    QString label() const { return m_label; }
    void setLabel(const QString& s) { m_label = s; update(); }

public slots:
    void setLevel(float dBFS) {
        m_level = qBound(-96.0f, dBFS, 0.0f);
        if (m_level >= m_peak) {
            m_peak    = m_level;
            m_peakAge = 0;
            if (!m_peakTimer->isActive()) m_peakTimer->start();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int w    = width();
        const int h    = height();
        const int lblH = m_label.isEmpty() ? 0 : 14;
        const int barH = h - lblH - 2;
        const int barX = 2;
        const int barW = w - 4;

        // Background
        p.fillRect(rect(), QColor(28, 28, 32));

        // Draw gradient bar (background track)
        QLinearGradient bg(barX, barX + barH, barX, barX);
        bg.setColorAt(0.0, QColor(30, 30, 30));
        bg.setColorAt(1.0, QColor(50, 50, 55));
        p.fillRect(barX, 2, barW, barH, bg);

        // Active level fill
        float norm = dbToNorm(m_level);           // 0..1
        int   fillH = static_cast<int>(norm * barH);
        if (fillH > 0) {
            QLinearGradient grad(barX, 2, barX, barH + 2);
            grad.setColorAt(0.0,  QColor(220,  40,  40));   // red  top
            grad.setColorAt(0.15, QColor(240, 160,  20));   // yellow
            grad.setColorAt(0.35, QColor( 80, 200,  80));   // green
            grad.setColorAt(1.0,  QColor( 40, 180,  60));
            p.fillRect(barX, 2 + barH - fillH, barW, fillH, grad);
        }

        // Peak hold line
        if (m_peak > -96.0f) {
            float pNorm = dbToNorm(m_peak);
            int   py    = 2 + barH - static_cast<int>(pNorm * barH) - 1;
            p.setPen(QPen(QColor(255, 220, 100), 2));
            p.drawLine(barX, py, barX + barW - 1, py);
        }

        // dB tick marks  (0, -6, -12, -24, -48)
        static const float ticks[] = { 0, -6, -12, -24, -48 };
        p.setPen(QColor(90, 90, 100));
        QFont f = p.font();
        f.setPixelSize(8);
        p.setFont(f);
        for (float db : ticks) {
            int ty = 2 + barH - static_cast<int>(dbToNorm(db) * barH);
            p.drawLine(barX, ty, barX + 3, ty);
        }

        // Label
        if (!m_label.isEmpty()) {
            p.setPen(QColor(160, 160, 180));
            QFont lf = p.font();
            lf.setPixelSize(9);
            p.setFont(lf);
            p.drawText(0, h - lblH, w, lblH, Qt::AlignHCenter | Qt::AlignVCenter, m_label);
        }

        // Border
        p.setPen(QPen(QColor(60, 60, 70), 1));
        p.drawRect(barX, 2, barW - 1, barH - 1);
    }

private:
    float   m_level;
    float   m_peak;
    int     m_peakHoldMs;
    int     m_peakAge;
    QString m_label;
    QTimer* m_peakTimer;

    static float dbToNorm(float db) {
        // Map -96..0 dBFS to 0..1 with a square-root curve for visual comfort
        float linear = (db + 96.0f) / 96.0f;
        return qBound(0.0f, linear * linear, 1.0f);
    }
};
