#include "toy_display_widget.hpp"

#include <QFileInfo>
#include <QDebug>

ToyDisplayWidget::ToyDisplayWidget(QWidget* parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setScaledContents(true);
    setStyleSheet("background: black;");
}

ToyDisplayWidget::~ToyDisplayWidget()
{
    if (movie_) {
        movie_->stop();
    }
}

void ToyDisplayWidget::displayGif(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        qWarning() << "GIF file not found:" << path;
        return;
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (currentGifPath_ == absolutePath && movie_ && movie_->state() == QMovie::Running) {
        qDebug() << "GIF already displaying, skip reload:" << absolutePath;
        return;
    }

    if (movie_) {
        movie_->stop();
        movie_->deleteLater();
        movie_ = nullptr;
    }

    movie_ = new QMovie(absolutePath, QByteArray(), this);
    movie_->setCacheMode(QMovie::CacheAll);
    updateMovieSize();

    if (!movie_->isValid()) {
        qWarning() << "Invalid GIF:" << absolutePath;
        movie_->deleteLater();
        movie_ = nullptr;
        currentGifPath_.clear();
        return;
    }

    currentGifPath_ = absolutePath;
    setMovie(movie_);
    movie_->start();
    qDebug() << "Displaying GIF:" << absolutePath
            << "frames=" << movie_->frameCount();
}

void ToyDisplayWidget::clearDisplay()
{
    currentGifPath_.clear();
    if (movie_) {
        movie_->stop();
        movie_->deleteLater();
        movie_ = nullptr;
    }
    clear();
}

void ToyDisplayWidget::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateMovieSize();
}

void ToyDisplayWidget::updateMovieSize()
{
    if (movie_ && !size().isEmpty()) {
        movie_->setScaledSize(size());
    }
}
