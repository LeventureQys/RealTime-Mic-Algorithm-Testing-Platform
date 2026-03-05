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
    setWindowTitle("实时麦克风算法测试平台");
    resize(960, 620);

    // 居中显示
    if (auto* scr = QApplication::primaryScreen()) {
        QRect sg = scr->availableGeometry();
        move(sg.center() - rect().center());
    }

    // ── 根布局 ────────────────────────────────────
    auto* central  = new QWidget(this);
    setCentralWidget(central);
    auto* rootVBox = new QVBoxLayout(central);
    rootVBox->setContentsMargins(12, 10, 12, 6);
    rootVBox->setSpacing(8);

    // ── 标题栏 ────────────────────────────────────
    auto* titleLbl = new QLabel("  实时麦克风算法测试平台", central);
    titleLbl->setObjectName("titleLabel");
    rootVBox->addWidget(titleLbl);

    // ── 主区域：左侧控制面板 + 右侧可视化 ─────────
    auto* hSplit = new QSplitter(Qt::Horizontal, central);
    hSplit->setHandleWidth(5);
    rootVBox->addWidget(hSplit, 1);

    // ╔══════════════════════════════╗
    // ║        LEFT PANEL            ║
    // ╚══════════════════════════════╝
    auto* leftPanel = new QWidget;
    leftPanel->setMaximumWidth(230);
    auto* leftVBox  = new QVBoxLayout(leftPanel);
    leftVBox->setContentsMargins(0, 0, 4, 0);
    leftVBox->setSpacing(8);

    // ── 传输控制 ──────────────────────────────────
    auto* grpTransport  = new QGroupBox("传输控制", leftPanel);
    auto* transportGrid = new QGridLayout(grpTransport);
    transportGrid->setSpacing(6);
    transportGrid->setContentsMargins(8, 14, 8, 8);

    m_btnStart  = new QPushButton("▶  开始监听", grpTransport);
    m_btnStop   = new QPushButton("■  停止",     grpTransport);
    m_btnRecord = new QPushButton("⏺  开始录音", grpTransport);
    m_btnStart->setObjectName("btnStart");
    m_btnStop->setObjectName("btnStop");
    m_btnRecord->setObjectName("btnRecord");
    m_btnStart->setMinimumHeight(36);
    m_btnStop->setMinimumHeight(36);
    m_btnRecord->setMinimumHeight(36);
    m_btnStart->setToolTip("开启麦克风采集");
    m_btnStop->setToolTip("停止采集与录音");
    m_btnRecord->setToolTip("录制处理后的音频到 WAV 文件");

    transportGrid->addWidget(m_btnStart,  0, 0);
    transportGrid->addWidget(m_btnStop,   0, 1);
    transportGrid->addWidget(m_btnRecord, 1, 0, 1, 2);
    leftVBox->addWidget(grpTransport);

    // ── 功能选项 ──────────────────────────────────
    auto* grpOptions = new QGroupBox("功能选项", leftPanel);
    auto* optVBox    = new QVBoxLayout(grpOptions);
    optVBox->setContentsMargins(8, 14, 8, 8);
    optVBox->setSpacing(8);

    m_cbxAlgo    = new QCheckBox("启用降噪算法（DeepFilter）", grpOptions);
    m_cbxMonitor = new QCheckBox("启用实时监听（耳返）",       grpOptions);
    m_cbxAlgo->setChecked(false);
    m_cbxMonitor->setChecked(false);
    m_cbxAlgo->setToolTip("开启后对采集到的音频执行 DeepFilter 降噪处理");
    m_cbxMonitor->setToolTip("将处理后的音频实时播放到扬声器/耳机");
    optVBox->addWidget(m_cbxAlgo);
    optVBox->addWidget(m_cbxMonitor);
    leftVBox->addWidget(grpOptions);

    // ── 运行状态 ──────────────────────────────────
    auto* grpStatus  = new QGroupBox("运行状态", leftPanel);
    auto* statusGrid = new QGridLayout(grpStatus);
    statusGrid->setContentsMargins(8, 14, 8, 8);
    statusGrid->setSpacing(4);

    auto* lblStatusKey  = new QLabel("状态：",   grpStatus);
    auto* lblRecTimeKey = new QLabel("录音时长：", grpStatus);
    lblStatusKey->setObjectName("keyLabel");
    lblRecTimeKey->setObjectName("keyLabel");

    m_lblStatus  = new QLabel("空闲", grpStatus);
    m_lblStatus->setObjectName("statusLabel");
    m_lblRecTime = new QLabel("--:--", grpStatus);
    m_lblRecTime->setObjectName("recTimeLabel");

    statusGrid->addWidget(lblStatusKey,   0, 0);
    statusGrid->addWidget(m_lblStatus,    0, 1);
    statusGrid->addWidget(lblRecTimeKey,  1, 0);
    statusGrid->addWidget(m_lblRecTime,   1, 1);
    leftVBox->addWidget(grpStatus);

    leftVBox->addStretch();

    // ── 电平表 ────────────────────────────────────
    auto* grpLevel  = new QGroupBox("电平监测", leftPanel);
    auto* meterHBox = new QHBoxLayout(grpLevel);
    meterHBox->setContentsMargins(12, 16, 12, 8);
    meterHBox->setSpacing(16);

    // 左侧：标签列
    auto* meterLabels = new QVBoxLayout;
    meterLabels->addStretch();
    for (const char* db : {"  0", " -6", "-12", "-24", "-48"}) {
        auto* l = new QLabel(db, grpLevel);
        l->setObjectName("dbLabel");
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        meterLabels->addWidget(l);
        meterLabels->addStretch();
    }

    m_meterIn  = new LevelMeter(grpLevel);
    m_meterIn->setLabel("输入");
    m_meterIn->setFixedWidth(36);
    m_meterIn->setMinimumHeight(180);

    m_meterOut = new LevelMeter(grpLevel);
    m_meterOut->setLabel("输出");
    m_meterOut->setFixedWidth(36);
    m_meterOut->setMinimumHeight(180);

    meterHBox->addLayout(meterLabels);
    meterHBox->addWidget(m_meterIn);
    meterHBox->addWidget(m_meterOut);
    leftVBox->addWidget(grpLevel, 1);

    hSplit->addWidget(leftPanel);

    // ╔══════════════════════════════╗
    // ║        RIGHT PANEL           ║
    // ╚══════════════════════════════╝
    auto* rightPanel = new QWidget;
    auto* rightVBox  = new QVBoxLayout(rightPanel);
    rightVBox->setContentsMargins(4, 0, 0, 0);
    rightVBox->setSpacing(0);

    auto* grpSpectrum = new QGroupBox("幅频谱分析（dBFS，对数频率轴）", rightPanel);
    auto* specVBox    = new QVBoxLayout(grpSpectrum);
    specVBox->setContentsMargins(6, 14, 6, 6);

    m_spectrum = new SpectrumWidget(grpSpectrum);
    m_spectrum->setSampleRate(48000);
    m_spectrum->setDbRange(-80.0f, 0.0f);
    specVBox->addWidget(m_spectrum);
    rightVBox->addWidget(grpSpectrum, 1);

    hSplit->addWidget(rightPanel);
    hSplit->setStretchFactor(0, 0);
    hSplit->setStretchFactor(1, 1);
    hSplit->setSizes({230, 730});

    // ── 底部状态栏 ────────────────────────────────
    statusBar()->showMessage("就绪  |  48 kHz · 16-bit · 单声道");

    // ── 信号连接 ──────────────────────────────────
    connect(m_btnStart,   &QPushButton::clicked, this, &AudioCondition::onStartClicked);
    connect(m_btnStop,    &QPushButton::clicked, this, &AudioCondition::onStopClicked);
    connect(m_btnRecord,  &QPushButton::clicked, this, &AudioCondition::onRecordClicked);
    connect(m_cbxAlgo,    &QCheckBox::toggled,   this, &AudioCondition::onAlgorithmToggled);
    connect(m_cbxMonitor, &QCheckBox::toggled,   this, &AudioCondition::onMonitoringToggled);
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
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 13px;
        }
        QGroupBox {
            border: 1px solid #353748;
            border-radius: 6px;
            margin-top: 16px;
            padding: 6px 4px 4px 4px;
            color: #7880a8;
            font-size: 11px;
            letter-spacing: 0.5px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
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
        QPushButton:pressed  { background-color: #202230; }
        QPushButton:disabled { color: #505468; border-color: #303248; }

        QPushButton#btnStart {
            background-color: #1e4a2a;
            border-color: #2e7a40;
            color: #80e090;
        }
        QPushButton#btnStart:hover  { background-color: #256035; }
        QPushButton#btnStart:disabled { background-color: #1a2e1e; color: #406050; }

        QPushButton#btnStop {
            background-color: #4a1e1e;
            border-color: #7a2e2e;
            color: #e08080;
        }
        QPushButton#btnStop:hover    { background-color: #602525; }
        QPushButton#btnStop:disabled { background-color: #2e1a1a; color: #604040; }

        QPushButton#btnRecord {
            background-color: #3a1a2e;
            border-color: #7a3060;
            color: #e080b0;
        }
        QPushButton#btnRecord:hover { background-color: #4e2040; }
        QPushButton#btnRecord[recording="true"] {
            background-color: #7a0040;
            border-color: #ff2080;
            color: #ffffff;
        }

        QCheckBox { spacing: 6px; color: #b0b4c8; }
        QCheckBox::indicator {
            width: 15px; height: 15px;
            border: 1px solid #505468;
            border-radius: 3px;
            background: #252738;
        }
        QCheckBox::indicator:checked {
            background: #3060c0;
            border-color: #5080e0;
        }

        QLabel#titleLabel {
            font-size: 17px;
            font-weight: bold;
            color: #9098c8;
            padding: 4px 0 2px 0;
        }
        QLabel#statusLabel  { color: #70d080; font-size: 12px; }
        QLabel#recTimeLabel { color: #e06080; font-size: 12px; font-weight: bold; }
        QLabel#keyLabel     { color: #707090; font-size: 11px; }
        QLabel#dbLabel      { color: #505468; font-size: 9px;  font-family: "Consolas", monospace; }

        QStatusBar { background: #16181f; color: #606480; font-size: 11px; }

        QSplitter::handle       { background: #252838; }
        QSplitter::handle:hover { background: #404360; }
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
    m_lblStatus->setText(running ? "运行中" : "空闲");
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
    statusBar()->showMessage("监听中  |  48 kHz · 16-bit · 单声道");
}

void AudioCondition::onStopClicked()
{
    if (!m_isRunning) return;

    // Stop recording first if active
    if (m_audioDevice->isRecording()) {
        m_audioDevice->stopRecording();
        m_recTimer->stop();
        m_lblRecTime->clear();
        m_btnRecord->setText("⏺  开始录音");
        m_btnRecord->setProperty("recording", false);
        m_btnRecord->style()->unpolish(m_btnRecord);
        m_btnRecord->style()->polish(m_btnRecord);
    }

    m_audioSource->stop();
    setRunning(false);
    statusBar()->showMessage("已停止  |  48 kHz · 16-bit · 单声道");
}

void AudioCondition::onRecordClicked()
{
    if (!m_isRunning) return;

    if (m_audioDevice->isRecording()) {
        // Stop recording
        m_audioDevice->stopRecording();
        m_recTimer->stop();
        m_lblRecTime->setText("--:--");
        m_btnRecord->setText("⏺  开始录音");
        m_btnRecord->setProperty("recording", false);
        statusBar()->showMessage("录音已保存  |  48 kHz · 16-bit · 单声道");
    } else {
        // Start recording
        QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".wav";
        QString path = QFileDialog::getSaveFileName(this,
                            "保存录音文件",
                            dir + "/" + defaultName,
                            "WAV 音频文件 (*.wav)");
        if (path.isEmpty()) return;

        if (!m_audioDevice->startRecording(path)) {
            QMessageBox::warning(this, "错误", "无法创建录音文件：\n" + path);
            return;
        }
        m_recElapsed.start();
        m_recTimer->start();
        m_btnRecord->setText("⏹  停止录音");
        m_btnRecord->setProperty("recording", true);
        statusBar()->showMessage("录音中…  |  " + path);
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
    m_lblRecTime->setText(QString("● %1:%2")
                          .arg(mm, 2, 10, QChar('0'))
                          .arg(ss, 2, 10, QChar('0')));
}
