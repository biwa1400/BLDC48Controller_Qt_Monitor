#pragma once

#include <QDialog>
#include <QtGlobal>
#include <array>

class QCheckBox;
class QLabel;

// Displays the 22 LO/HI gate pairs (44 outputs total).
// The current implementation is read-only: it sends only the 0x22 state query.
// Method implementations and UI construction live in mosfetcontrolwindow.cpp.
class MosfetControlWindow final : public QDialog
{
public:
    explicit MosfetControlWindow(QWidget *parent = nullptr);

    void showState(bool manualMode, quint32 lo22, quint32 hi22);

private:
    void readState();

    QLabel *modeLabel_ = nullptr;
    std::array<QCheckBox *, 22> low_{};
    std::array<QCheckBox *, 22> high_{};
};
