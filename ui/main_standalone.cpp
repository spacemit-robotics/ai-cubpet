#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QTimer>
#include <QRandomGenerator>
#include <QDir>
#include <QDebug>
#include <QSocketNotifier>
#include <iostream>
#include <unistd.h>

#include "toy_display_widget.hpp"

/**
 * @brief Standalone version of AI Toy display (no DDS required)
 *
 * This version can run independently for testing and development.
 * Use serial/stdin input to simulate DDS events:
 *   - space: Short touch
 *   - l: Long touch
 *   - t: Throw
 *   - a: Awake mode
 *   - s: Sleep mode
 *   - 1-9: Custom GIF (01_*.gif to 09_*.gif)
 *   - 0: Play default GIF (01_daily.gif)
 *   - q: Quit
 */
class ToyMainWindowStandalone : public QMainWindow
{
    Q_OBJECT

public:
    explicit ToyMainWindowStandalone(QWidget *parent = nullptr)
        : QMainWindow(parent)
        , displayWidget_(new ToyDisplayWidget(this))
        , awakeModeTimer_(new QTimer(this))
        , sleepModeTimer_(new QTimer(this))
        , gifAnimTimer_(new QTimer(this))
        , currentMode_(NORMAL)
        , stdinNotifier_(new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this))
    {
        setWindowTitle("AI Toy Display (Standalone - No DDS)");
        setGeometry(100, 100, 932, 466);
        setCentralWidget(displayWidget_);

        // Connect stdin notifier to handle serial/stdin input
        connect(stdinNotifier_, &QSocketNotifier::activated, this, &ToyMainWindowStandalone::onStdinReady);

        loadResources();
        setupTimers();

        // Play default media after initialization
        QTimer::singleShot(0, this, &ToyMainWindowStandalone::playDefaultMedia);

        std::cout << "====================================" << std::endl;
        std::cout << "AI Toy Standalone Mode (No DDS)" << std::endl;
        std::cout << "====================================" << std::endl;
        std::cout << "Serial/Stdin Controls:" << std::endl;
        std::cout << "  space  - Short touch" << std::endl;
        std::cout << "  l      - Long touch" << std::endl;
        std::cout << "  t      - Throw toy" << std::endl;
        std::cout << "  a      - Awake mode" << std::endl;
        std::cout << "  s      - Sleep mode" << std::endl;
        std::cout << "  1-9    - Play GIF (01_*.gif to 09_*.gif)" << std::endl;
        std::cout << "  0      - Play default GIF (01_daily.gif)" << std::endl;
        std::cout << "  q      - Quit" << std::endl;
        std::cout << "====================================" << std::endl;
    }

    ~ToyMainWindowStandalone() = default;

private slots:
    void onStdinReady()
    {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            handleInput(c);
        }
    }

    void handleInput(char c)
    {
        switch (c) {
            case ' ':
                std::cout << "Input: Short touch" << std::endl;
                onShortTouch(1.0f);
                break;
            case 'l':
            case 'L':
                std::cout << "Input: Long touch" << std::endl;
                onLongTouch(3.0f);
                break;
            case 't':
            case 'T':
                std::cout << "Input: Throw" << std::endl;
                onThrow();
                break;
            case 'a':
            case 'A':
                std::cout << "Input: Awake mode" << std::endl;
                onAwakeMode();
                break;
            case 's':
            case 'S':
                std::cout << "Input: Sleep mode" << std::endl;
                onSleepMode();
                break;
            case '0':
                std::cout << "Input: Play default GIF" << std::endl;
                playDefaultMedia();
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                {
                    int gifIndex = c - '0';
                    std::cout << "Input: Play GIF " << gifIndex << std::endl;
                    playGifByIndex(gifIndex);
                }
                break;
            case 'q':
            case 'Q':
                std::cout << "Input: Quit" << std::endl;
                QApplication::quit();
                break;
            case '\n':
            case '\r':
                // Ignore newline/carriage return
                break;
            default:
                // Ignore other characters
                break;
        }
    }

    void onShortTouch(float value)
    {
        stopAllTimers();

        // Random gif 01-06 (new elephant resources)
        int gifIndex = QRandomGenerator::global()->bounded(1, 7);
        playGifByIndex(gifIndex);
    }

    void onLongTouch(float value)
    {
        stopAllTimers();
        displayWidget_->displayGif(gifBasePath_ + "02_expect.gif");
    }

    void onThrow()
    {
        stopAllTimers();
        displayWidget_->displayGif(gifBasePath_ + "03_diz.gif");
    }

    void onAwakeMode()
    {
        stopAllTimers();
        currentMode_ = AWAKE;

        std::cout << "Awake mode activated" << std::endl;

        // Start awake mode timers
        awakeModeTimer_->start(QRandomGenerator::global()->bounded(30000, 60001));
        gifAnimTimer_->start(QRandomGenerator::global()->bounded(10000, 16001));
    }

    void onSleepMode()
    {
        stopAllTimers();
        currentMode_ = SLEEP;

        std::cout << "Sleep mode activated" << std::endl;

        // Show close eye animation - GIF will loop naturally
        displayWidget_->displayGif(gifBasePath_ + "10_close_eye.gif");
    }

    void onAwakeModeAudioTimeout()
    {
        if (currentMode_ == AWAKE) {
            std::cout << "Awake mode: audio timeout (would play audio in full system)" << std::endl;
            // Schedule next timeout
            awakeModeTimer_->start(QRandomGenerator::global()->bounded(30000, 60001));
        }
    }

    void onAwakeModeGifTimeout()
    {
        if (currentMode_ == AWAKE) {
            // Random gif 01-06 for idle animation.
            int gifIndex = QRandomGenerator::global()->bounded(1, 7);
            QStringList gifFiles = filesForIndex(gifIndex);
            if (!gifFiles.isEmpty()) {
                displayWidget_->displayGif(gifBasePath_ + gifFiles.first());
                std::cout << "Awake mode: playing random GIF " << gifIndex << std::endl;
            }

            // Schedule next gif
            gifAnimTimer_->start(QRandomGenerator::global()->bounded(10000, 16001));
        }
    }

    void onSleepModeTimeout()
    {
        if (currentMode_ == SLEEP) {
            displayWidget_->displayGif(gifBasePath_ + "10_close_eye.gif");
        }
    }

private:
    QString defaultGifBasePath() const
    {
        QString path = QCoreApplication::applicationDirPath() + "/../share/ai-cubpet/gif/";
        return QDir(path).cleanPath(path) + "/";
    }

    void loadResources()
    {
        audioBasePath_.clear();
        gifBasePath_ = qEnvironmentVariable("GIF_PATH", defaultGifBasePath());
        if (!gifBasePath_.endsWith('/')) {
            gifBasePath_ += '/';
        }

        // Check if resource directories exist
        if (!QDir(gifBasePath_).exists()) {
            qWarning() << "GIF directory not found:" << gifBasePath_;
            qWarning() << "Trying local directory...";

            // Try relative paths for development
            gifBasePath_ = "./gif/";
            if (!QDir(gifBasePath_).exists()) {
                qWarning() << "Local GIF directory also not found:" << gifBasePath_;
                qWarning() << "Please set GIF_PATH environment variable or ensure files exist at:"
                        << defaultGifBasePath();
            }
        } else {
            qDebug() << "GIF directory found:" << gifBasePath_;
        }
    }

    void setupTimers()
    {
        connect(awakeModeTimer_, &QTimer::timeout, this, &ToyMainWindowStandalone::onAwakeModeAudioTimeout);
        connect(gifAnimTimer_, &QTimer::timeout, this, &ToyMainWindowStandalone::onAwakeModeGifTimeout);
        connect(sleepModeTimer_, &QTimer::timeout, this, &ToyMainWindowStandalone::onSleepModeTimeout);
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
        const QString defaultGif = gifBasePath_ + "01_daily.gif";
        std::cout << "Playing default media" << std::endl;
        displayWidget_->displayGif(defaultGif);
    }

    QStringList filesForIndex(int index) const
    {
        const QString prefix = QString("%1_").arg(index, 2, 10, QChar('0'));
        return QDir(gifBasePath_).entryList(
            QStringList() << QString("%1*.gif").arg(prefix)
                        << QString("%1*.png").arg(prefix),
            QDir::Files,
            QDir::Name);
    }

    void playGifByIndex(int index)
    {
        QStringList gifFiles = filesForIndex(index);
        if (!gifFiles.isEmpty()) {
            std::cout << "Playing GIF: " << gifFiles.first().toStdString() << std::endl;
            displayWidget_->displayGif(gifBasePath_ + gifFiles.first());
        } else {
            qWarning() << "No GIF found for index" << index;
        }
    }

    ToyDisplayWidget* displayWidget_;

    QTimer* awakeModeTimer_;
    QTimer* sleepModeTimer_;
    QTimer* gifAnimTimer_;

    enum Mode { NORMAL, AWAKE, SLEEP };
    Mode currentMode_;

    QString audioBasePath_;
    QString gifBasePath_;

    QSocketNotifier* stdinNotifier_;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    ToyMainWindowStandalone window;
    window.show();

    return app.exec();
}

#include "main_standalone.moc"
