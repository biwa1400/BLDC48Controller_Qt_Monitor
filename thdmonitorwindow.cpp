#include "thdmonitorwindow.h"

#include <QCloseEvent>
#include <QDebug>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr quint32 kThdMask = (1u << 22) - 1u;
constexpr quint16 kHarmonicMask = (1u << 10) - 1u;
QLabel *makeSummaryValue(const QString &initial, QWidget *parent)
{
    auto *label = new QLabel(initial, parent);
    QFont font = label->font();
    font.setPointSize(18);
    font.setBold(true);
    label->setFont(font);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QWidget *makeSummaryCard(const QString &caption, QLabel *value, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName("summaryCard");
    auto *layout = new QVBoxLayout(card);
    auto *title = new QLabel(caption, card);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #718096;");
    layout->addWidget(title);
    layout->addWidget(value);
    return card;
}

QString formatPercent(float value)
{
    return std::isfinite(value) && value >= 0.0f
               ? QString::number(value, 'f', 2) + "%"
               : QStringLiteral("N/A");
}
}

ThdMonitorWindow::ThdMonitorWindow(const QString &host, QWidget *parent)
    : QWidget(parent), m_host(host.trimmed()), m_socket(new QTcpSocket(this))
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle("THD Monitor — 22 Channels");
    resize(940, 760);
    setMinimumSize(660, 460);
    buildUi();

    connect(m_socket, &QTcpSocket::connected,
            this, &ThdMonitorWindow::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &ThdMonitorWindow::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ThdMonitorWindow::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &ThdMonitorWindow::onSocketError);
    clearDisplay();
    connectSocket();
}

ThdMonitorWindow::~ThdMonitorWindow()
{
    disconnectSocket();
}

void ThdMonitorWindow::closeEvent(QCloseEvent *event)
{
    disconnectSocket();
    QWidget::closeEvent(event);
}

void ThdMonitorWindow::buildUi()
{
    setStyleSheet(
        "QWidget#thdRoot { background: #f7f9fc; }"
        "QFrame#summaryCard { background: white; border: 1px solid #dce3eb;"
        " border-radius: 9px; }"
        "QFrame#channelCard { background: white; border: 1px solid #e0e6ee;"
        " border-radius: 7px; }"
        "QToolButton { border: none; background: transparent; font-weight: 600;"
        " text-align: left; padding: 5px; }"
        "QToolButton:hover { background: #edf4fc; }"
        "QProgressBar { background: #edf1f6; border: none; border-radius: 3px;"
        " height: 12px; text-align: center; }"
        "QProgressBar::chunk { background: #3995c6; border-radius: 3px; }"
        );
    setObjectName("thdRoot");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto *top = new QHBoxLayout();
    auto *heading = new QVBoxLayout();
    auto *title = new QLabel("THD Monitor", this);
    QFont titleFont = title->font();
    titleFont.setPointSize(19);
    titleFont.setBold(true);
    title->setFont(titleFont);
    heading->addWidget(title);
    auto *subtitle = new QLabel("22-Channel Harmonic Analysis", this);
    subtitle->setStyleSheet("color: #718096;");
    heading->addWidget(subtitle);
    top->addLayout(heading);
    top->addStretch();
    m_connectionLabel = new QLabel("Disconnected", this);
    m_connectionLabel->setStyleSheet("color: #a13b3b; font-weight: 600;");
    top->addWidget(m_connectionLabel);
    root->addLayout(top);

    auto *summary = new QHBoxLayout();
    m_validChannelsLabel = makeSummaryValue("0 / 22", this);
    m_averageThdLabel = makeSummaryValue("N/A", this);
    m_maximumThdLabel = makeSummaryValue("N/A", this);
    summary->addWidget(makeSummaryCard("Valid Channels", m_validChannelsLabel, this));
    summary->addWidget(makeSummaryCard("Average THD", m_averageThdLabel, this));
    summary->addWidget(makeSummaryCard("Maximum THD", m_maximumThdLabel, this));
    root->addLayout(summary);

    auto *head = new QHBoxLayout();
    auto *channelHead = new QLabel("CHANNEL", this);
    auto *frequencyHead = new QLabel("FREQUENCY", this);
    auto *thdHead = new QLabel("THD", this);
    for (auto *label : {channelHead, frequencyHead, thdHead})
        label->setStyleSheet("color: #718096; font-size: 11px; font-weight: 600;");
    head->addWidget(channelHead, 37);
    head->addWidget(frequencyHead, 30);
    head->addWidget(thdHead, 33);
    root->addLayout(head);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *contents = new QWidget(scroll);
    auto *list = new QVBoxLayout(contents);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(6);

    for (int ch = 0; ch < ChannelCount; ++ch) {
        auto *card = new QFrame(contents);
        card->setObjectName("channelCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 7, 12, 7);
        cardLayout->setSpacing(5);
        auto *line = new QHBoxLayout();
        auto *toggle = new QToolButton(card);
        toggle->setText(QString("I%1").arg(ch + 1));
        toggle->setArrowType(Qt::RightArrow);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toggle->setCheckable(true);
        auto *frequency = new QLabel("N/A", card);
        auto *thd = new QLabel("N/A", card);
        frequency->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        thd->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        line->addWidget(toggle, 37);
        line->addWidget(frequency, 30);
        line->addWidget(thd, 33);
        cardLayout->addLayout(line);

        auto *panel = new QWidget(card);
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(24, 4, 8, 10);
        panelLayout->setSpacing(5);
        panel->hide();
        connect(toggle, &QToolButton::toggled, panel, &QWidget::setVisible);
        connect(toggle, &QToolButton::toggled, this, [this, toggle, ch](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            if (expanded)
                updateHarmonicBars(ch);
        });
        cardLayout->addWidget(panel);
        list->addWidget(card);
        m_channelWidgets[ch] = {toggle, frequency, thd, panel, panelLayout};
    }
    list->addStretch();
    scroll->setWidget(contents);
    root->addWidget(scroll, 1);
}

void ThdMonitorWindow::connectSocket()
{
    if (m_host.isEmpty()) {
        m_connectionLabel->setText("Host not configured");
        return;
    }
    m_receiveBuffer.clear();
    m_connectionLabel->setText("Connecting...");
    m_connectionLabel->setStyleSheet("color: #9a6700; font-weight: 600;");
    m_socket->connectToHost(m_host, 10001);
}

void ThdMonitorWindow::disconnectSocket()
{
    m_socket->abort();
    m_receiveBuffer.clear();
}

void ThdMonitorWindow::onConnected()
{
    m_connectionLabel->setText("Connected");
    m_connectionLabel->setStyleSheet("color: #27804b; font-weight: 600;");
}

void ThdMonitorWindow::onDisconnected()
{
    m_receiveBuffer.clear();
    clearDisplay();
    m_connectionLabel->setText("Disconnected");
    m_connectionLabel->setStyleSheet("color: #a13b3b; font-weight: 600;");
}

void ThdMonitorWindow::onSocketError(QAbstractSocket::SocketError error)
{
    qWarning() << "[THD] Socket error:" << error << m_socket->errorString();
    m_receiveBuffer.clear();
    clearDisplay();
    m_connectionLabel->setText("Connection error");
    m_connectionLabel->setToolTip(m_socket->errorString());
    m_connectionLabel->setStyleSheet("color: #a13b3b; font-weight: 600;");
}

void ThdMonitorWindow::onReadyRead()
{
    const QByteArray chunk = m_socket->readAll();
    m_receiveBuffer.append(chunk);
    processBuffer();
}

void ThdMonitorWindow::processBuffer()
{
    static const QByteArray magic("THD2", 4);
    while (m_receiveBuffer.size() >= 4) {
        if (m_receiveBuffer.left(4) != magic) {
            const qsizetype next = m_receiveBuffer.indexOf(magic, 1);
            if (next >= 0) {
                qWarning() << "[THD] Protocol resync, discarded" << next << "bytes";
                m_receiveBuffer.remove(0, next);
            } else {
                const qsizetype discarded = m_receiveBuffer.size() - 3;
                qWarning() << "[THD] THD2 magic not found; discarded" << discarded << "bytes";
                m_receiveBuffer.remove(0, discarded);
                return;
            }
        }
        if (m_receiveBuffer.size() < PacketSize)
            return;
        decodePacket(m_receiveBuffer.left(PacketSize));
        m_receiveBuffer.remove(0, PacketSize);
    }
}

quint16 ThdMonitorWindow::readU16(const char *data)
{
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 ThdMonitorWindow::readU32(const char *data)
{
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    return quint32(p[0]) | (quint32(p[1]) << 8) |
           (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

float ThdMonitorWindow::readFloat(const char *data)
{
    const quint32 bits = readU32(data);
    float value;
    static_assert(sizeof(value) == sizeof(bits), "Expected IEEE-754 float32");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void ThdMonitorWindow::decodePacket(const QByteArray &packet)
{
    if (packet.size() != PacketSize || packet.left(4) != "THD2")
        return;
    const char *p = packet.constData();
    m_sequence = readU32(p + 4);
    m_validMask = readU32(p + 1064) & kThdMask;
    m_averageThd = readFloat(p + 1112);
    m_maximumThd = readFloat(p + 1116);
    for (int ch = 0; ch < ChannelCount; ++ch) {
        auto &data = m_channels[ch];
        data.frequencyHz = readFloat(p + 8 + ch * 4);
        data.thdPercent = readFloat(p + 96 + ch * 4);
        data.harmonicValidMask = readU16(p + 1068 + ch * 2) & kHarmonicMask;
        data.thdValid = (m_validMask & (quint32(1) << ch)) != 0;
        for (int h = 0; h < HarmonicCount; ++h)
            data.harmonics[h] = readFloat(p + 184 + (ch * HarmonicCount + h) * 4);
    }
    updateDisplay();
}

void ThdMonitorWindow::updateDisplay()
{
    int valid = 0;
    for (int ch = 0; ch < ChannelCount; ++ch) {
        const auto &data = m_channels[ch];
        auto &widgets = m_channelWidgets[ch];
        if (data.thdValid && std::isfinite(data.thdPercent) && data.thdPercent >= 0.0f)
            ++valid;
        widgets.frequencyLabel->setText(
            std::isfinite(data.frequencyHz) && data.frequencyHz > 0.0f
                ? QString::number(data.frequencyHz, 'f', 1) + " Hz"
                : QStringLiteral("N/A"));
        widgets.thdLabel->setText(data.thdValid ? formatPercent(data.thdPercent)
                                                : QStringLiteral("N/A"));
        if (widgets.harmonicsPanel->isVisible())
            updateHarmonicBars(ch);
    }
    m_validChannelsLabel->setText(QString("%1 / %2").arg(valid).arg(ChannelCount));
    m_averageThdLabel->setText(valid ? formatPercent(m_averageThd) : "N/A");
    m_maximumThdLabel->setText(valid ? formatPercent(m_maximumThd) : "N/A");
}

void ThdMonitorWindow::updateHarmonicBars(int channel)
{
    auto &widgets = m_channelWidgets[channel];
    auto *layout = widgets.harmonicsLayout;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        delete item;
    }
    const auto &data = m_channels[channel];
    float maxAmplitude = 0.0f;
    for (int h = 0; h < HarmonicCount; ++h) {
        if ((data.harmonicValidMask & (quint16(1) << h)) &&
            std::isfinite(data.harmonics[h]) && data.harmonics[h] >= 0.0f)
            maxAmplitude = std::max(maxAmplitude, data.harmonics[h]);
    }
    if (data.harmonicValidMask == 0 || maxAmplitude <= 0.0f) {
        auto *label = new QLabel("No valid harmonic measurements", widgets.harmonicsPanel);
        label->setStyleSheet("color: #718096;");
        layout->addWidget(label);
        return;
    }
    auto *caption = new QLabel("Harmonic amplitudes · relative to peak (%)", widgets.harmonicsPanel);
    caption->setStyleSheet("color: #718096; font-size: 11px;");
    layout->addWidget(caption);
    for (int h = 0; h < HarmonicCount; ++h) {
        if (!(data.harmonicValidMask & (quint16(1) << h)))
            continue;
        const float amplitude = data.harmonics[h];
        if (!std::isfinite(amplitude) || amplitude < 0.0f)
            continue;
        const double relative = 100.0 * double(amplitude) / double(maxAmplitude);
        auto *row = new QWidget(widgets.harmonicsPanel);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        auto *name = new QLabel(QString("H%1").arg(h + 1), row);
        name->setFixedWidth(30);
        auto *bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        bar->setValue(qBound(0, int(std::round(relative * 10.0)), 1000));
        bar->setTextVisible(false);
        bar->setToolTip(QString("H%1 | Amplitude: %2 | Peak-relative: %3%")
                            .arg(h + 1).arg(amplitude, 0, 'g', 6).arg(relative, 0, 'f', 2));
        auto *value = new QLabel(QString::number(relative, 'f', 1) + "%", row);
        value->setFixedWidth(56);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        line->addWidget(name);
        line->addWidget(bar, 1);
        line->addWidget(value);
        layout->addWidget(row);
    }
}

void ThdMonitorWindow::clearDisplay()
{
    m_sequence = 0;
    m_validMask = 0;
    m_averageThd = -1.0f;
    m_maximumThd = -1.0f;
    for (auto &channel : m_channels)
        channel = ChannelData{};
    updateDisplay();
}
