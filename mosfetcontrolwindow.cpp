#include "mosfetcontrolwindow.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

namespace {
constexpr auto kHost = "192.168.7.2";
constexpr quint16 kPort = 8889;
constexpr int kPollMs = 100;          // 10 Hz only in Manual Override
constexpr int kTimeoutMs = 1500;
constexpr int kReconnectMs = 1000;
constexpr quint32 kMask22 = 0x003FFFFFu;

struct Session {
    QTcpSocket *socket = nullptr;
    QTimer *poll = nullptr;
    QTimer *timeout = nullptr;
    QCheckBox *modeSwitch = nullptr;
    QByteArray reply;
    QByteArray outgoing;
    bool waiting = false;
    int requestType = 0; // 0=state, 1=mode, 2=channel
    int pendingChannel = -1;
    bool requestedMode = false;
    bool connectedState = false;
    bool manual = false;
    quint32 low = 0;
    quint32 high = 0;
    qint64 lastReconnectMs = 0;
};
}

MosfetControlWindow::MosfetControlWindow(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("MOSFET Output Control (44 channels)"));
    resize(580, 700);
    auto *root = new QVBoxLayout(this);
    auto *notice = new QLabel(QStringLiteral(
                                  "Manual mode directly drives MOSFET gates without deadtime or interlock. "
                                  "Disconnect the power stage before testing."), this);
    notice->setWordWrap(true);
    root->addWidget(notice);

    auto *modeRow = new QHBoxLayout;
    modeLabel_ = new QLabel(QStringLiteral("Mode: Disconnected"), this);
    modeRow->addWidget(modeLabel_);
    modeRow->addStretch();
    auto *modeSwitch = new QCheckBox(QStringLiteral("Manual"), this);
    modeSwitch->setObjectName(QStringLiteral("mosfetModeSwitch"));
    modeSwitch->setEnabled(false);
    modeSwitch->setCursor(Qt::PointingHandCursor);
    modeSwitch->setStyleSheet(QStringLiteral(
        "QCheckBox { spacing: 9px; }"
        "QCheckBox::indicator { width: 40px; height: 21px; border-radius: 10px; "
        "background: #888888; border: 1px solid #777777; }"
        "QCheckBox::indicator:checked { background: #2879cf; border-color: #2879cf; }"
        "QCheckBox::indicator:disabled { background: #bcbcbc; border-color: #aaaaaa; }"));
    modeRow->addWidget(modeSwitch);
    root->addLayout(modeRow);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *grid = new QGridLayout(content);
    grid->addWidget(new QLabel(QStringLiteral("Pair"), content), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("LO"), content), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("HI"), content), 0, 2);
    for (int i = 0; i < 22; ++i) {
        grid->addWidget(new QLabel(QStringLiteral("Channel %1").arg(i), content), i + 1, 0);
        low_[i] = new QCheckBox(content);
        high_[i] = new QCheckBox(content);
        low_[i]->setEnabled(false);
        high_[i]->setEnabled(false);
        grid->addWidget(low_[i], i + 1, 1);
        grid->addWidget(high_[i], i + 1, 2);
    }
    scroll->setWidget(content);
    root->addWidget(scroll);

    // One persistent asynchronous connection. ARM supports multiple commands per TCP session.
    // No blocking waitFor... calls: UI remains responsive during disconnects and timeouts.
    auto s = std::make_shared<Session>();
    s->socket = new QTcpSocket(this);
    s->poll = new QTimer(this);
    s->timeout = new QTimer(this);
    s->modeSwitch = modeSwitch;
    s->poll->setInterval(kPollMs);
    s->timeout->setSingleShot(true);

    auto disconnected = [this, s]() {
        s->waiting = false;
        s->requestType = 0;
        s->reply.clear();
        s->connectedState = false;
        modeLabel_->setText(QStringLiteral("Mode: Disconnected (reconnecting...)"));
        s->modeSwitch->setEnabled(false);
        for (int i = 0; i < 22; ++i) {
            low_[i]->setEnabled(false);
            high_[i]->setEnabled(false);
        }
        // Leave last-known bits visible; they are not claimed to be live while disconnected.
    };

    auto sendCommand = [s](const QByteArray &command, int requestType, bool requestedMode, int channel = -1) {
        if (s->waiting || s->socket->state() != QAbstractSocket::ConnectedState)
            return false;
        s->waiting = true;
        s->requestType = requestType;
        s->pendingChannel = channel;
        s->requestedMode = requestedMode;
        s->reply.clear();
        s->outgoing = command;
        if (s->socket->write(s->outgoing) != s->outgoing.size()) {
            s->waiting = false;
            s->socket->abort();
            return false;
        }
        s->timeout->start(kTimeoutMs);
        return true;
    };

    auto query = [s, sendCommand]() {
        if (!s->waiting && s->socket->state() == QAbstractSocket::ConnectedState)
            sendCommand(QByteArray(1, char(0x22)), 0, false);
    };

    connect(s->socket, &QTcpSocket::connected, this, [s, query]() {
        s->lastReconnectMs = 0;
        query();
    });
    connect(s->socket, &QTcpSocket::disconnected, this, disconnected);
    connect(s->socket, &QTcpSocket::errorOccurred, this,
            [s, disconnected](QAbstractSocket::SocketError) {
                s->timeout->stop();
                disconnected();
            });
    connect(s->timeout, &QTimer::timeout, this, [s, disconnected]() {
        disconnected();
        s->socket->abort();
    });

    connect(s->socket, &QTcpSocket::readyRead, this, [this, s, query, disconnected]() {
        if (!s->waiting) {
            s->socket->readAll();
            return;
        }
        s->reply += s->socket->readAll();
        const int needed = s->requestType != 0 ? 1 : 10;
        if (s->reply.size() < needed)
            return;
        s->timeout->stop();
        s->waiting = false;
        const QByteArray data = s->reply.left(needed);
        s->reply.clear();
        const auto *b = reinterpret_cast<const unsigned char *>(data.constData());
        if (b[0] != 0) {
            if (s->requestType != 0)
                QMessageBox::warning(this, QStringLiteral("MOSFET Control"),
                                     QStringLiteral("ARM rejected command (status 0x%1).")
                                         .arg(unsigned(b[0]), 2, 16, QLatin1Char('0')));
            else
                modeLabel_->setText(QStringLiteral("Mode: ARM state query failed"));
            s->requestType = 0;
            query(); // Restore authoritative FPGA state after a failed write.
            return;
        }
        if (s->requestType != 0) {
            s->requestType = 0;
            query(); // One readback after a write, including switch to Normal.
            return;
        }
        quint32 lo = 0, hi = 0;
        for (int i = 0; i < 4; ++i) {
            lo |= quint32(b[2 + i]) << (8 * i);
            hi |= quint32(b[6 + i]) << (8 * i);
        }
        s->manual = b[1] != 0;
        s->low = lo & kMask22;
        s->high = hi & kMask22;
        s->connectedState = true;
        showState(s->manual, s->low, s->high);
    });

    connect(s->poll, &QTimer::timeout, this, [this, s, query]() {
        if (!isVisible()) {
            if (s->socket->state() != QAbstractSocket::UnconnectedState)
                s->socket->abort();
            return;
        }
        if (s->socket->state() == QAbstractSocket::UnconnectedState) {
            // Poll timer doubles as reconnect timer; no concurrent connect attempts.
            ++s->lastReconnectMs;
            if (s->lastReconnectMs >= kReconnectMs / kPollMs) {
                s->lastReconnectMs = 0;
                s->socket->connectToHost(QString::fromLatin1(kHost), kPort);
            }
        } else if (s->manual) {
            // Normal PWM: no periodic state queries. Manual: 10 Hz.
            query();
        }
    });
    s->poll->start();
    QTimer::singleShot(0, this, [this, s]() {
        if (isVisible())
            s->socket->connectToHost(QString::fromLatin1(kHost), kPort);
    });

    connect(modeSwitch, &QCheckBox::toggled, this,
            [this, s, sendCommand](bool enable) {
                if (!s->connectedState || s->waiting) {
                    QSignalBlocker block(s->modeSwitch);
                    s->modeSwitch->setChecked(s->manual);
                    return;
                }
                if (enable == s->manual)
                    return;
                if (enable) {
                    if (s->low != 0 || s->high != 0) {
                        QMessageBox::warning(this, QStringLiteral("MOSFET Mode"),
                                             QStringLiteral("Manual output registers contain HIGH bits. "
                                                            "Cannot enable manual mode."));
                        QSignalBlocker block(s->modeSwitch);
                        s->modeSwitch->setChecked(s->manual);
                        return;
                    }
                    if (QMessageBox::warning(this, QStringLiteral("Enable Manual Override"),
                                             QStringLiteral("Manual override bypasses deadtime and interlock. "
                                                            "Confirm the MOSFET power stage is disconnected."),
                                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
                        QSignalBlocker block(s->modeSwitch);
                        s->modeSwitch->setChecked(s->manual);
                        return;
                    }
                }
                s->modeSwitch->setEnabled(false);
                QByteArray cmd;
                cmd.append(char(0x20));
                cmd.append(char(enable ? 1 : 0));
                if (!sendCommand(cmd, 1, enable)) {
                    QSignalBlocker block(s->modeSwitch);
                    s->modeSwitch->setChecked(s->manual);
                    s->modeSwitch->setEnabled(s->connectedState);
                }
            });

    // Each checkbox writes exactly one FPGA manual-output bit (0x21).
    // Never write when disconnected, in Normal PWM mode, or while another
    // request is pending. The next 0x22 response is the source of truth.
    for (int i = 0; i < 22; ++i) {
        auto connectChannel = [this, s, sendCommand](QCheckBox *check, int channel) {
            connect(check, &QCheckBox::toggled, this,
                    [this, s, sendCommand, channel](bool level) {
                        if (!s->connectedState || !s->manual || s->waiting) {
                            showState(s->manual, s->low, s->high);
                            return;
                        }
                        QByteArray cmd;
                        cmd.append(char(0x21));
                        cmd.append(char(channel));
                        cmd.append(char(level ? 1 : 0));
                        if (!sendCommand(cmd, 2, false, channel))
                            showState(s->manual, s->low, s->high);
                    });
        };
        connectChannel(low_[i], i);
        connectChannel(high_[i], i + 22);
    }

}

void MosfetControlWindow::showState(bool manualMode, quint32 lo22, quint32 hi22)
{
    modeLabel_->setText(manualMode ? QStringLiteral("Mode: Manual Override")
                                   : QStringLiteral("Mode: Normal PWM"));
    if (auto *toggle = findChild<QCheckBox *>(QStringLiteral("mosfetModeSwitch"))) {
        const QSignalBlocker block(toggle);
        toggle->setChecked(manualMode);
        toggle->setEnabled(true);
    }
    for (int i = 0; i < 22; ++i) {
        const QSignalBlocker blockLow(low_[i]);
        const QSignalBlocker blockHigh(high_[i]);
        low_[i]->setChecked((lo22 & (quint32(1) << i)) != 0);
        high_[i]->setChecked((hi22 & (quint32(1) << i)) != 0);
        low_[i]->setEnabled(manualMode);
        high_[i]->setEnabled(manualMode);
    }
}

void MosfetControlWindow::readState()
{
    // Retained for compatibility with the existing header.
    // State refresh is automatic on connection and after writes;
    // periodic 10 Hz polling is active only in Manual Override.
}
