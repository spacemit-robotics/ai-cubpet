#include "cubpet_ui_dds_subscriber.hpp"
#include "toy_display_widget.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QAudioDeviceInfo>
#include <QRandomGenerator>
#include <QSoundEffect>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <iostream>
#include <unistd.h>

class ToyMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ToyMainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , displayWidget_(new ToyDisplayWidget(this))
        , ddsWorker_(new CubpetUiDdsSubscriber())
        , subscriberThread_(new QThread(this))
        , awakeModeTimer_(new QTimer(this))
        , sleepModeTimer_(new QTimer(this))
        , gifAnimTimer_(new QTimer(this))
        , stdinNotifier_(new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this))
        , audioEffect_(new QSoundEffect(selectAudioOutputDevice(), this))
    {
        setWindowTitle("AI Cubpet UI");
        setGeometry(100, 100, 932, 466);
        setCentralWidget(displayWidget_);
        audioEffect_->setVolume(1.0);

        loadResources();
        setupDDS();
        setupTimers();

        connect(stdinNotifier_, &QSocketNotifier::activated, this, &ToyMainWindow::onStdinReady);
        connect(audioEffect_, &QSoundEffect::statusChanged, this, &ToyMainWindow::onAudioStatusChanged);
        connect(audioEffect_, &QSoundEffect::playingChanged, this, &ToyMainWindow::onAudioPlayingChanged);

        QTimer::singleShot(0, this, &ToyMainWindow::playDefaultMedia);
        std::cout << "AI Cubpet UI ready. Press q to quit." << std::endl;
    }

    ~ToyMainWindow() override
    {
        if (ddsWorker_) {
            ddsWorker_->stop();
        }
        if (subscriberThread_->isRunning()) {
            subscriberThread_->quit();
            subscriberThread_->wait(3000);
        }
    }

private slots:
    void onShortTouch(float)
    {
        stopAllTimers();
        playGifByIndex(QRandomGenerator::global()->bounded(1, 7));
    }

    void onLongTouch(float)
    {
        stopAllTimers();
        displayWidget_->displayGif(resolveMediaPath("02_expect.gif", gifBasePath_));
    }

    void onThrow()
    {
        stopAllTimers();
        displayWidget_->displayGif(resolveMediaPath("03_diz.gif", gifBasePath_));
    }

    void onAwakeMode()
    {
        stopAllTimers();
        currentMode_ = AWAKE;
        awakeModeTimer_->start(QRandomGenerator::global()->bounded(30000, 60001));
        gifAnimTimer_->start(QRandomGenerator::global()->bounded(10000, 16001));
    }

    void onSleepMode()
    {
        stopAllTimers();
        currentMode_ = SLEEP;
        displayWidget_->displayGif(resolveMediaPath("10_close_eye.gif", gifBasePath_));
    }

    void onCustomMedia(const QString& audio_file, const QString& gif_file)
    {
        stopAllTimers();

        const QString gif_path = resolveMediaPath(gif_file, gifBasePath_);
        const QString audio_path = resolveMediaPath(audio_file, audioBasePath_);

        std::cout << "Playing custom media: audio=" << audio_path.toStdString()
                  << " gif=" << gif_path.toStdString() << std::endl;

        if (!gif_path.isEmpty()) {
            displayWidget_->displayGif(gif_path);
        }
        playAudio(audio_path);
    }

    void onAwakeModeAudioTimeout()
    {
        if (currentMode_ == AWAKE) {
            playAudio(resolveMediaPath("007_daily_response.wav", audioBasePath_));
            awakeModeTimer_->start(QRandomGenerator::global()->bounded(30000, 60001));
        }
    }

    void onAwakeModeGifTimeout()
    {
        if (currentMode_ == AWAKE) {
            playGifByIndex(QRandomGenerator::global()->bounded(1, 7));
            gifAnimTimer_->start(QRandomGenerator::global()->bounded(10000, 16001));
        }
    }

    void onSleepModeTimeout()
    {
        if (currentMode_ == SLEEP) {
            displayWidget_->displayGif(resolveMediaPath("10_close_eye.gif", gifBasePath_));
        }
    }

    void onStdinReady()
    {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) != 1) {
            return;
        }

        if (c == 'q' || c == 'Q') {
            QApplication::quit();
            return;
        }
        if (c >= '0' && c <= '9') {
            const int index = c == '0' ? 1 : (c - '0');
            playGifByIndex(index);
        }
    }

    void onAudioStatusChanged()
    {
        switch (audioEffect_->status()) {
        case QSoundEffect::Ready:
            qDebug() << "Audio ready:" << audioEffect_->source();
            if (pendingAudioPlay_) {
                pendingAudioPlay_ = false;
                audioEffect_->play();
                qDebug() << "Playing audio:" << pendingAudioSource_.toLocalFile();
            }
            break;
        case QSoundEffect::Loading:
            qDebug() << "Audio loading:" << audioEffect_->source();
            break;
        case QSoundEffect::Error:
            pendingAudioPlay_ = false;
            qWarning() << "Audio playback error:" << audioEffect_->source();
            break;
        case QSoundEffect::Null:
        default:
            break;
        }
    }

    void onAudioPlayingChanged()
    {
        qDebug() << "Audio playing state:" << audioEffect_->isPlaying();
    }

private:
    enum Mode { NORMAL, AWAKE, SLEEP };

    static QAudioDeviceInfo selectAudioOutputDevice()
    {
        const QList<QAudioDeviceInfo> devices =
            QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
        const QString configured =
            QString::fromLocal8Bit(qgetenv("AI_CUBPET_AUDIO_OUTPUT")).trimmed();

        for (const auto& device : devices) {
            qDebug() << "Audio output device:" << device.deviceName();
        }

        auto findDevice = [&](const QString& needle) -> QAudioDeviceInfo {
            if (needle.isEmpty()) {
                return QAudioDeviceInfo();
            }
            for (const auto& device : devices) {
                if (device.deviceName().contains(needle, Qt::CaseInsensitive)) {
                    return device;
                }
            }
            return QAudioDeviceInfo();
        };

        QAudioDeviceInfo selected = findDevice(configured);
        if (selected.isNull()) {
            selected = findDevice("SPV Composite Device");
        }
        if (selected.isNull()) {
            selected = QAudioDeviceInfo::defaultOutputDevice();
        }

        qDebug() << "Selected audio output device:" << selected.deviceName();
        return selected;
    }

    static QString envOrDefault(const char* first_env,
                                const char* second_env,
                                const QString& default_value)
    {
        QByteArray value = qgetenv(first_env);
        if (!value.isEmpty()) {
            return QString::fromLocal8Bit(value);
        }

        value = qgetenv(second_env);
        if (!value.isEmpty()) {
            return QString::fromLocal8Bit(value);
        }

        return default_value;
    }

    static QString withTrailingSlash(QString path)
    {
        if (path.isEmpty()) {
            return path;
        }
        path = QDir::cleanPath(path);
        if (!path.endsWith('/')) {
            path += '/';
        }
        return path;
    }

    static QString defaultMediaBasePath(const QString& subdir)
    {
        return withTrailingSlash(QCoreApplication::applicationDirPath()
                                 + "/../share/ai-cubpet/" + subdir);
    }

    static QString resolveMediaPath(const QString& file, const QString& base_path)
    {
        if (file.isEmpty()) {
            return QString();
        }

        const QFileInfo info(file);
        if (info.isAbsolute()) {
            return info.absoluteFilePath();
        }

        return QFileInfo(base_path + file).absoluteFilePath();
    }

    void loadResources()
    {
        gifBasePath_ = withTrailingSlash(envOrDefault("GIF_PATH",
                                                      "AI_CUBPET_GIF_DIR",
                                                      defaultMediaBasePath("gif")));
        audioBasePath_ = withTrailingSlash(envOrDefault("AUDIO_PATH",
                                                        "AI_CUBPET_AUDIO_DIR",
                                                        defaultMediaBasePath("audio")));

        if (!QDir(gifBasePath_).exists()) {
            qWarning() << "GIF directory not found:" << gifBasePath_;
        } else {
            qDebug() << "GIF directory found:" << gifBasePath_;
        }

        if (!QDir(audioBasePath_).exists()) {
            qWarning() << "Audio directory not found:" << audioBasePath_;
        } else {
            qDebug() << "Audio directory found:" << audioBasePath_;
        }
    }

    void setupDDS()
    {
        ddsWorker_->moveToThread(subscriberThread_);
        connect(subscriberThread_, &QThread::finished, ddsWorker_, &QObject::deleteLater);
        connect(subscriberThread_, &QThread::started, ddsWorker_, &CubpetUiDdsSubscriber::run);

        connect(ddsWorker_, &CubpetUiDdsSubscriber::shortTouchReceived, this, &ToyMainWindow::onShortTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::longTouchReceived, this, &ToyMainWindow::onLongTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::throwReceived, this, &ToyMainWindow::onThrow);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::awakeModeReceived, this, &ToyMainWindow::onAwakeMode);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::sleepModeReceived, this, &ToyMainWindow::onSleepMode);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::customMediaReceived, this, &ToyMainWindow::onCustomMedia);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::leftFootShortTouchReceived, this, &ToyMainWindow::onShortTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::leftFootLongTouchReceived, this, &ToyMainWindow::onLongTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::rightFootShortTouchReceived, this, &ToyMainWindow::onShortTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::rightFootLongTouchReceived, this, &ToyMainWindow::onLongTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::noseShortTouchReceived, this, &ToyMainWindow::onShortTouch);
        connect(ddsWorker_, &CubpetUiDdsSubscriber::noseLongTouchReceived, this, &ToyMainWindow::onLongTouch);

        subscriberThread_->start();
    }

    void setupTimers()
    {
        connect(awakeModeTimer_, &QTimer::timeout, this, &ToyMainWindow::onAwakeModeAudioTimeout);
        connect(gifAnimTimer_, &QTimer::timeout, this, &ToyMainWindow::onAwakeModeGifTimeout);
        connect(sleepModeTimer_, &QTimer::timeout, this, &ToyMainWindow::onSleepModeTimeout);
    }

    void stopAllTimers()
    {
        awakeModeTimer_->stop();
        gifAnimTimer_->stop();
        sleepModeTimer_->stop();
        currentMode_ = NORMAL;
    }

    void playDefaultMedia()
    {
        displayWidget_->displayGif(resolveMediaPath("01_daily.gif", gifBasePath_));
    }

    QStringList filesForIndex(int index) const
    {
        const QString prefix = QString("%1_").arg(index, 2, 10, QChar('0'));
        return QDir(gifBasePath_).entryList(
            QStringList() << QString("%1*.gif").arg(prefix),
            QDir::Files,
            QDir::Name);
    }

    void playGifByIndex(int index)
    {
        const QStringList gif_files = filesForIndex(index);
        if (gif_files.isEmpty()) {
            qWarning() << "No GIF found for index" << index;
            return;
        }
        displayWidget_->displayGif(resolveMediaPath(gif_files.first(), gifBasePath_));
    }

    void playAudio(const QString& audio_path)
    {
        if (audio_path.isEmpty()) {
            return;
        }
        if (!QFileInfo::exists(audio_path)) {
            qWarning() << "Audio file not found:" << audio_path;
            return;
        }

        pendingAudioSource_ = QUrl::fromLocalFile(audio_path);
        pendingAudioPlay_ = true;
        audioEffect_->stop();
        if (audioEffect_->source() != pendingAudioSource_) {
            audioEffect_->setSource(pendingAudioSource_);
        }
        if (audioEffect_->status() == QSoundEffect::Ready) {
            onAudioStatusChanged();
        } else {
            qDebug() << "Queued audio:" << audio_path
                     << "status=" << audioEffect_->status();
        }
    }

    ToyDisplayWidget* displayWidget_;
    CubpetUiDdsSubscriber* ddsWorker_;
    QThread* subscriberThread_;
    QTimer* awakeModeTimer_;
    QTimer* sleepModeTimer_;
    QTimer* gifAnimTimer_;
    QSocketNotifier* stdinNotifier_;
    QSoundEffect* audioEffect_;
    Mode currentMode_ = NORMAL;
    QString audioBasePath_;
    QString gifBasePath_;
    QUrl pendingAudioSource_;
    bool pendingAudioPlay_ = false;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    ToyMainWindow window;
    window.show();

    return app.exec();
}

#include "main_dds.moc"
