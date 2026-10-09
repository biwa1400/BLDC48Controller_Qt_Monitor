
#ifndef LUTMANAGERDIALOG_H
#define LUTMANAGERDIALOG_H

#include <QDialog>
#include <QString>

#include "lutcsvparser.h"

class QLabel;
class QPushButton;
class QLineSeries;
class QChartView;
class QValueAxis;

class LutManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LutManagerDialog(QWidget *parent = nullptr);
    ~LutManagerDialog() override = default;

private:
    void setupUi();
    void openCsv();
    void confirmLut();
    void updatePreview();

    QLabel *csvFileLabel = nullptr;
    QLabel *entryCountLabel = nullptr;
    QLabel *valueRangeLabel = nullptr;
    QLabel *statusLabel = nullptr;

    QPushButton *openCsvButton = nullptr;
    QPushButton *okButton = nullptr;
    QPushButton *cancelButton = nullptr;

    QChartView *chartView = nullptr;
    QLineSeries *lutSeries = nullptr;
    QValueAxis *axisX = nullptr;
    QValueAxis *axisY = nullptr;

    QString currentCsvPath;
    LutCsvParser::LutData lutData{};
    bool dataValid = false;
};

#endif // LUTMANAGERDIALOG_H
