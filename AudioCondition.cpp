#include "AudioCondition.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>

// ─────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────
AudioCondition::AudioCondition(QWidget* parent)
    : QMainWindow(parent)
{
    // --- Audio setup ---
    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelConfig(QAudioFormat::ChannelConfigMono);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!inputDevice.isFormatSupported(format)) {
        qWarning() << "Requested audio format not supported, system will try to adapt.";
    }

    m_audioDevice = new AudioDevice_(this);
    m_audioDevice->open(QIODevice::ReadWrite);

    m_audioSource = new QAudioSource(inputDevice, format, this);

    // Wire audio signals → UI
    connect(m_audioDevice, &AudioDevice_::inputLevelChanged,
            this, &AudioCondition::onInputLevelChanged, Qt::QueuedConnection);
    connect(m_audioDevice, &AudioDevice_::outputLevelChanged,
            this, &AudioCondition::onOutputLevelChanged, Qt::QueuedConnection);
    connect(m_audioDevice, &AudioDevice_::spectrumUpdated,
            this, &AudioCondition::onSpectrumUpdated, Qt::QueuedConnection);

    // Recording timer
    m_recTimer = new QTimer(this);
    m_recTimer->setInterval(1000);
    connect(m_recTimer, &QTimer::timeout, this, &AudioCondition::updateRecordingTime);

    buildUI();
    applyDarkTheme();
    setRunning(false);
}

AudioCondition::~AudioCondition()
{
    if (m_audioSource) m_audioSource->stop();
    if (m_audioDevice && m_audioDevice->isRecording()) m_audioDevice->stopRecording();
}

// ─────────────────────────────────────────────
// UI construction (pure code, no .ui file)
// ─────────────────────────────────────────────
void AudioCondition::buildUI()
{
    setWindowTitle("RealTime Mic Algorithm Testing Platform");
    resize(860, 560);

    // Centre on screen
    if (auto* scr = QApplication::primaryScreen()) {
        QRect sg = scr->availableGeometry();
        move(sg.center() - rect().center());
    }

    // ── Central widget & root layout ──────────────
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootVBox = new QVBoxLayout(central);
    rootVBox->setContentsMargins(10, 10, 10, 6);
    rootVBox->setSpacing(8);

    // ── Title bar ─────────────────────────────────
    auto* titleLbl = new QLabel("  Mic Algorithm Testing Platform", central);
    titleLbl->setObjectName("titleLabel");
    rootVBox->addWidget(titleLbl);

    // ── Main horizontal splitter ───────────────────
    auto* hSplit = new QSplitter(Qt::Horizontal, central);
    hSplit->setHandleWidth(4);
    rootVBox->addWidget(hSplit, 1);

    // ── LEFT: controls + level meters ─────────────
    auto* leftPanel = new QWidget;
    auto* leftVBox  = new QVBoxLayout(leftPanel);
    leftVBox->setContentsMargins(0, 0, 0, 0);
    leftVBox->setSpacing(8);

    // -- Transport group --
    auto* grpTransport = new QGroupBox("Transport", leftPanel);
    auto* transportGrid = new QGridLayout(grpTransport);
    transportGrid->setSpacing(6);

    m_btnStart  = new QPushButton("▶  Start", grpTransport);
    m_btnStop   = new QPushButton("■  Stop",  grpTransport);
    m_btnRecord = new QPushButton("⏺  Record", grpTransport);
    m_btnStart->setObjectName("btnStart");
    m_btnStop->setObjectName("btnStop");
    m_btnRecord->setObjectName("btnRecord");
    m_btnStart->setMinimumHeight(34);
    m_btnStop->setMinimumHeight(34);
    m_btnRecord->setMinimumHeight(34);

    transportGrid->addWidget(m_btnStart,  0, 0);
    transportGrid->addWidget(m_btnStop,   0, 1);
    transportGrid->addWidget(m_btnRecord, 1, 0, 1, 2);

    leftVBox->addWidget(grpTransport);

    // -- Options group --
    auto* grpOptions = new QGroupBox("Options", leftPanel);
    auto* optVBox    = new QVBoxLayout(grpOptions);
    m_cbxAlgo    = new QCheckBox("Enable Algorithm (DeepFilter)", grpOptions);
    m_cbxMonitor = new QCheckBox("Enable Monitoring (playback)", grpOptions);
    m_cbxAlgo->setChecked(false);
    m_cbxMonitor->setChecked(false);
    optVBox->addWidget(m_cbxAlgo);
    optVBox->addWidget(m_cbxMonitor);
    leftVBox->addWidget(grpOptions);

    // -- Status group --
    auto* grpStatus = new QGroupBox("Status", leftPanel);
    auto* statusVBox = new QVBoxLayout(grpStatus);
    m_lblStatus  = new QLabel("Idle", grpStatus);
    m_lblStatus->setObjectName("statusLabel");
    m_lblRecTime = new QLabel("", grpStatus);
    m_lblRecTime->setObjectName("recTimeLabel");
    statusVBox->addWidget(m_lblStatus);
    statusVBox->addWidget(m_lblRecTime);
    leftVBox->addWidget(grpStatus);

    leftVBox->addStretch();

    // -- Level meters --
    auto* grpLevel = new QGroupBox("Levels", leftPanel);
    auto* meterHBox = new QHBoxLayout(grpLevel);
    meterHBox->setSpacing(12);

    m_meterIn  = new LevelMeter(grpLevel);
    m_meterIn->setLabel("IN");
    m_meterIn->setMinimumWidth(32);
    m_meterIn->setMinimumHeight(160);

    m_meterOut = new LevelMeter(grpLevel);
    m_meterOut->setLabel("OUT");
    m_meterOut->setMinimumWidth(32);
    m_meterOut->setMinimumHeight(160);

    meterHBox->addStretch();
    meterHBox->addWidget(m_meterIn);
    meterHBox->addWidget(m_meterOut);
    meterHBox->addStretch();
    leftVBox->addWidget(grpLevel, 1);

    hSplit->addWidget(leftPanel);

    // ── RIGHT: spectrum display ────────────────────
    auto* rightPanel = new QWidget;
    auto* rightVBox  = new QVBoxLayout(rightPanel);
    rightVBox->setContentsMargins(0, 0, 0, 0);

    auto* grpSpectrum = new QGroupBox("Frequency Spectrum", rightPanel);
    auto* specVBox    = new QVBoxLayout(grpSpectrum);
    m_spectrum = new SpectrumWidget(grpSpectrum);
    m_spectrum->setSampleRate(48000);
    m_spectrum->setDbRange(-80.0f, 0.0f);
    specVBox->addWidget(m_spectrum);
    rightVBox->addWidget(grpSpectrum, 1);

    hSplit->addWidget(rightPanel);
    hSplit->setStretchFactor(0, 0);
    hSplit->setStretchFactor(1, 1);
    hSplit->setSizes({220, 640});

    // ── Bottom status bar ──────────────────────────
    statusBar()->showMessage("Ready  |  48 kHz  16-bit  Mono");

    // ── Signal connections ─────────────────────────
    connect(m_btnStart,  &QPushButton::clicked, this, &AudioCondition::onStartClicked);
    connect(m_btnStop,   &QPushButton::clicked, this, &AudioCondition::onStopClicked);
    connect(m_btnRecord, &QPushButton::clicked, this, &AudioCondition::onRecordClicked);
    connect(m_cbxAlgo,    &QCheckBox::toggled,  this, &AudioCondition::onAlgorithmToggled);
    connect(m_cbxMonitor, &QCheckBox::toggled,  this, &AudioCondition::onMonitoringToggled);
}

// ─────────────────────────────────────────────
// Dark theme stylesheet
// ─────────────────────────────────────────────
void AudioCondition::applyDarkTheme()
{
    qApp->setStyle("Fusion");

    setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #1c1e26;
            color: #d0d2e0;
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 13px;
        }
        QGroupBox {
            border: 1px solid #353748;
            border-radius: 6px;
            margin-top: 14px;
            padding: 6px 4px 4px 4px;
            color: #8890b0;
            font-size: 11px;
            letter-spacing: 0.5px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            top: 2px;
        }
        QPushButton {
            background-color: #2a2d3e;
            border: 1px solid #404360;
            border-radius: 5px;
            padding: 6px 14px;
            color: #c0c4d8;
        }
        QPushButton:hover {
            background-color: #353850;
            border-color: #5a5e80;
            color: #e0e4f4;
        }
        QPushButton:pressed {
            background-color: #202230;
        }
        QPushButton#btnStart {
            background-color: #1e4a2a;
            border-color: #2e7a40;
            color: #80e090;
        }
        QPushButton#btnStart:hover {
            background-color: #256035;
        }
        QPushButton#btnStop {
            background-color: #4a1e1e;
            border-color: #7a2e2e;
            color: #e08080;
        }
        QPushButton#btnStop:hover {
            background-color: #602525;
        }
        QPushButton#btnRecord {
            background-color: #3a1a2e;
            border-color: #7a3060;
            color: #e080b0;
        }
        QPushButton#btnRecord:hover {
            background-color: #4e2040;
        }
        QPushButton#btnRecord[recording="true"] {
            background-color: #7a0040;
            border-color: #ff2080;
            color: #ffffff;
        }
        QCheckBox {
            spacing: 6px;
            color: #b0b4c8;
        }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border: 1px solid #505468;
            border-radius: 3px;
            background: #252738;
        }
        QCheckBox::indicator:checked {
            background: #3060c0;
            border-color: #5080e0;
        }
        QLabel#titleLabel {
            font-size: 16px;
            font-weight: bold;
            color: #9098c8;
            padding: 4px 0;
            letter-spacing: 0.5px;
        }
        QLabel#statusLabel {
            color: #70d080;
            font-size: 12px;
        }
        QLabel#recTimeLabel {
            color: #e08080;
            font-size: 12px;
            font-weight: bold;
        }
        QStatusBar {
            background: #16181f;
            color: #606480;
            font-size: 11px;
        }
        QSplitter::handle {
            background: #2a2d3e;
        }
        QSplitter::handle:hover {
            background: #404360;
        }
    )");
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────
void AudioCondition::setRunning(bool running)
{
    m_isRunning = running;
    m_btnStart->setEnabled(!running);
    m_btnStop->setEnabled(running);
    m_btnRecord->setEnabled(running);
    m_lblStatus->setText(running ? "Running" : "Idle");
    if (!running) {
        m_meterIn->setLevel(-96.0f);
        m_meterOut->setLevel(-96.0f);
    }
}

// ─────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────
void AudioCondition::onStartClicked()
{
    if (m_isRunning) return;
    m_audioSource->start(m_audioDevice);
    setRunning(true);
    statusBar()->showMessage("Listening  |  48 kHz  16-bit  Mono");
}

void AudioCondition::onStopClicked()
{
    if (!m_isRunning) return;

    // Stop recording first if active
    if (m_audioDevice->isRecording()) {
        m_audioDevice->stopRecording();
        m_recTimer->stop();
        m_lblRecTime->clear();
        m_btnRecord->setText("⏺  Record");
        m_btnRecord->setProperty("recording", false);
        m_btnRecord->style()->unpolish(m_btnRecord);
        m_btnRecord->style()->polish(m_btnRecord);
    }

    m_audioSource->stop();
    setRunning(false);
    statusBar()->showMessage("Stopped  |  48 kHz  16-bit  Mono");
}

void AudioCondition::onRecordClicked()
{
    if (!m_isRunning) return;

    if (m_audioDevice->isRecording()) {
        // Stop recording
        m_audioDevice->stopRecording();
        m_recTimer->stop();
        m_lblRecTime->clear();
        m_btnRecord->setText("⏺  Record");
        m_btnRecord->setProperty("recording", false);
        statusBar()->showMessage("Recording saved  |  48 kHz  16-bit  Mono");
    } else {
        // Start recording
        QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".wav";
        QString path = QFileDialog::getSaveFileName(this,
                            "Save Recording",
                            dir + "/" + defaultName,
                            "WAV Files (*.wav)");
        if (path.isEmpty()) return;

        if (!m_audioDevice->startRecording(path)) {
            QMessageBox::warning(this, "Error", "Failed to open file for recording:\n" + path);
            return;
        }
        m_recElapsed.start();
        m_recTimer->start();
        m_btnRecord->setText("⏹  Stop Rec");
        m_btnRecord->setProperty("recording", true);
        statusBar()->showMessage("Recording…  |  " + path);
    }

    m_btnRecord->style()->unpolish(m_btnRecord);
    m_btnRecord->style()->polish(m_btnRecord);
}

void AudioCondition::onAlgorithmToggled(bool checked)
{
    if (m_audioDevice) m_audioDevice->setDF(checked);
}

void AudioCondition::onMonitoringToggled(bool checked)
{
    if (m_audioDevice) m_audioDevice->setReturn(checked);
}

void AudioCondition::onInputLevelChanged(float dBFS)
{
    m_meterIn->setLevel(dBFS);
}

void AudioCondition::onOutputLevelChanged(float dBFS)
{
    m_meterOut->setLevel(dBFS);
}

void AudioCondition::onSpectrumUpdated(const QVector<float>& magnitudes)
{
    m_spectrum->updateSpectrum(magnitudes);
}

void AudioCondition::updateRecordingTime()
{
    if (!m_audioDevice->isRecording()) return;
    qint64 secs = m_recElapsed.elapsed() / 1000;
    int    mm   = static_cast<int>(secs / 60);
    int    ss   = static_cast<int>(secs % 60);
    m_lblRecTime->setText(QString("REC  %1:%2")
                          .arg(mm, 2, 10, QChar('0'))
                          .arg(ss, 2, 10, QChar('0')));
}
