#pragma once

#include <QMainWindow>
#include <QAudioSource>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>

#include "Audio.h"
#include "LevelMeter.h"
#include "SpectrumWidget.h"

class AudioCondition : public QMainWindow
{
    Q_OBJECT

public:
    explicit AudioCondition(QWidget* parent = nullptr);
    ~AudioCondition();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onRecordClicked();
    void onAlgorithmToggled(bool checked);
    void onMonitoringToggled(bool checked);
    void onInputLevelChanged(float dBFS);
    void onOutputLevelChanged(float dBFS);
    void onSpectrumUpdated(const QVector<float>& magnitudes);
    void updateRecordingTime();

private:
    void buildUI();
    void applyDarkTheme();
    void setRunning(bool running);

    // Audio engine
    QAudioSource*  m_audioSource  = nullptr;
    AudioDevice_*  m_audioDevice  = nullptr;

    // -- UI widgets (built in code, no .ui file) --
    // Control panel
    QPushButton*   m_btnStart     = nullptr;
    QPushButton*   m_btnStop      = nullptr;
    QPushButton*   m_btnRecord    = nullptr;
    QCheckBox*     m_cbxAlgo      = nullptr;
    QCheckBox*     m_cbxMonitor   = nullptr;
    QLabel*        m_lblStatus    = nullptr;
    QLabel*        m_lblRecTime   = nullptr;

    // Visualisation
    LevelMeter*    m_meterIn      = nullptr;
    LevelMeter*    m_meterOut     = nullptr;
    SpectrumWidget* m_spectrum    = nullptr;

    // Recording state
    QTimer*        m_recTimer     = nullptr;
    QElapsedTimer  m_recElapsed;
    bool           m_isRunning    = false;
};
