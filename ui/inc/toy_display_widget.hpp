#ifndef TOY_DISPLAY_WIDGET_HPP
#define TOY_DISPLAY_WIDGET_HPP

#include <QLabel>
#include <QMovie>
#include <QResizeEvent>
#include <QString>

/**
 * @brief Widget for displaying GIF animations with Qt's native movie decoder.
 */
class ToyDisplayWidget : public QLabel
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param parent Parent widget
     */
    explicit ToyDisplayWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor - cleans up temporary QML file
     */
    ~ToyDisplayWidget();

    /**
     * @brief Display a GIF animation on both eyes
     * @param path Absolute path to the GIF file
     */
    void displayGif(const QString& path);

    /**
     * @brief Clear the current display
     */
    void clearDisplay();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateMovieSize();

    QMovie* movie_ = nullptr;
    QString currentGifPath_;
};

#endif // TOY_DISPLAY_WIDGET_HPP
