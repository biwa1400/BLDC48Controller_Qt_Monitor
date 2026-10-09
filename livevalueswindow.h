#ifndef LIVEVALUESWINDOW_H
#define LIVEVALUESWINDOW_H

#include <QWidget>
#include <array>

#include "dataframe.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class LiveValuesWindow;
}
QT_END_NAMESPACE

class QLabel;

class LiveValuesWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LiveValuesWindow(QWidget *parent = nullptr);
    ~LiveValuesWindow() override;

    void updateValues(const DataFrame &frame);

private:
    Ui::LiveValuesWindow *ui;

    static constexpr std::size_t CURRENT_CHANNEL_COUNT = 22;

    std::array<QLabel *, CURRENT_CHANNEL_COUNT> currentLabels{};
    QLabel *rpmLabel = nullptr;

    QLabel *encoderLabel = nullptr;
    QLabel *dtcLabel = nullptr;
    QLabel *phaseAdvLabel = nullptr;
    QLabel *vsRhoLabel = nullptr;
};

#endif // LIVEVALUESWINDOW_H
