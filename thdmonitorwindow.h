#ifndef THDMONITORWINDOW_H
#define THDMONITORWINDOW_H

#include <QWidget>
#include <QTcpSocket>
#include <QByteArray>
#include <QLabel>
#include <QToolButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QVector>
#include <QString>
#include <QtGlobal>

#include <array>

class ThdMonitorWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ThdMonitorWindow(const QString &host,
                              QWidget *parent = nullptr);
    ~ThdMonitorWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    static constexpr int ChannelCount = 22;
    static constexpr int HarmonicCount = 10;
    static constexpr int PacketSize = 1120;

    struct ChannelData {
        float frequencyHz = 0.0f;
        float thdPercent = -1.0f;
        std::array<float, HarmonicCount> harmonics{};
        quint16 harmonicValidMask = 0;
        bool thdValid = false;
    };

    struct ChannelWidgets {
        QToolButton *expandButton = nullptr;
        QLabel *frequencyLabel = nullptr;
        QLabel *thdLabel = nullptr;
        QWidget *harmonicsPanel = nullptr;
        QVBoxLayout *harmonicsLayout = nullptr;
    };

    void buildUi();
    void connectSocket();
    void disconnectSocket();
    void processBuffer();
    void decodePacket(const QByteArray &packet);
    void updateDisplay();
    void updateHarmonicBars(int channel);
    void clearDisplay();

    static quint16 readU16(const char *data);
    static quint32 readU32(const char *data);
    static float readFloat(const char *data);

    QString m_host;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_receiveBuffer;

    QLabel *m_connectionLabel = nullptr;
    QLabel *m_validChannelsLabel = nullptr;
    QLabel *m_averageThdLabel = nullptr;
    QLabel *m_maximumThdLabel = nullptr;

    std::array<ChannelData, ChannelCount> m_channels;
    std::array<ChannelWidgets, ChannelCount> m_channelWidgets;

    quint32 m_sequence = 0;
    quint32 m_validMask = 0;
    float m_averageThd = -1.0f;
    float m_maximumThd = -1.0f;
};

#endif // THDMONITORWINDOW_H