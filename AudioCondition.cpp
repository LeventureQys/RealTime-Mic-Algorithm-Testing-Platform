#include "AudioCondition.h"

AudioCondition::AudioCondition(QWidget *parent)
	: QMainWindow(parent)
	, ui(new Ui::AudioConditionClass())
{
    ui->setupUi(this);
    // 获取默认音频输入设备
    QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();

    // 设置音频格式
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);  // 使用单声道，方便处理
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    // 检查设备是否支持所设置的格式
    if (!info.isFormatSupported(format)) {
        qWarning() << "Default format not supported, trying to use the nearest.";
        format = info.nearestFormat(format);
    }

    // 创建音频输入对象
    audioInput = new QAudioInput(info, format, this);

    // 创建自定义的QIODevice来处理音频数据
    audioDevice = new AudioDevice_(this);
    audioDevice->open(QIODevice::ReadWrite);

    // 初始化 DeepFilterNet
}

AudioCondition::~AudioCondition()
{
	delete ui;
}

void AudioCondition::on_btn_stop_clicked()
{
    // 停止录音
    audioInput->stop();
}

void AudioCondition::on_btn_start_clicked()
{
    // 开始录音
    audioInput->start(audioDevice);
}

void AudioCondition::on_cbx_algorithm_clicked(bool blnchecked)
{
    audioDevice->setDF(blnchecked);
}

void AudioCondition::on_cbx_monitoring_clicked(bool blnchecked)
{
    audioDevice->setReturn(blnchecked);
}
