#include "livevalueswindow.h"
#include "ui_livevalueswindow.h"

#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QString>

LiveValuesWindow::LiveValuesWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LiveValuesWindow)
{
    ui->setupUi(this);

    // This must always be a real top-level window, never a child
    // widget embedded inside MainWindow.
    setWindowFlag(Qt::Window, true);

    setWindowTitle("Live Values");

    auto createValueCell =
        [this](
            QHBoxLayout *targetLayout,
            const QString &name,
            QLabel **valueLabel)
    {
        auto *cell =
            new QFrame(ui->valuesContainer);

        cell->setObjectName("valueCell");
        cell->setFrameShape(QFrame::StyledPanel);
        cell->setMinimumWidth(76);

        auto *layout =
            new QVBoxLayout(cell);

        layout->setContentsMargins(
            4, 8, 4, 8);

        layout->setSpacing(4);

        auto *nameLabel =
            new QLabel(name, cell);

        nameLabel->setAlignment(
            Qt::AlignCenter);

        QFont nameFont =
            nameLabel->font();

        nameFont.setPointSize(13);
        nameFont.setBold(true);

        nameLabel->setFont(nameFont);

        auto *numberLabel =
            new QLabel("0", cell);

        numberLabel->setAlignment(
            Qt::AlignCenter);

        QFont valueFont(
            "Consolas");

        valueFont.setPointSize(20);
        valueFont.setBold(true);

        numberLabel->setFont(valueFont);

        layout->addWidget(nameLabel);
        layout->addWidget(numberLabel, 1);

        targetLayout->addWidget(
            cell,
            1);

        *valueLabel =
            numberLabel;
    };

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        createValueCell(
            ui->valuesLayout,
            QString("I%1")
                .arg(channel + 1),
            &currentLabels[channel]);
    }

    createValueCell(
        ui->statusValuesLayout,
        "RPM",
        &rpmLabel);

    // Additional raw decoded values. Keep the existing layout unchanged.
    createValueCell(
        ui->statusValuesLayout,
        "LM10 Encoder",
        &encoderLabel);

    createValueCell(
        ui->statusValuesLayout,
        "DTC",
        &dtcLabel);

    createValueCell(
        ui->statusValuesLayout,
        "Phase Advance",
        &phaseAdvLabel);

    createValueCell(
        ui->statusValuesLayout,
        "VS Rho",
        &vsRhoLabel);

    resize(
        1900,
        190);
}

LiveValuesWindow::~LiveValuesWindow()
{
    delete ui;
}

void LiveValuesWindow::updateValues(
    const DataFrame &frame)
{
    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        if (!currentLabels[channel]) {
            continue;
        }

        currentLabels[channel]->setText(
            QString::number(
                frame.currents[channel]));
    }

    if (rpmLabel) {
        rpmLabel->setText(
            QString::number(
                frame.rpm));
    }

    if (encoderLabel) {
        encoderLabel->setText(
            QString::number(
                frame.degree));
    }

    if (dtcLabel) {
        dtcLabel->setText(
            QString::number(
                frame.dtc));
    }

    if (phaseAdvLabel) {
        phaseAdvLabel->setText(
            QString::number(
                frame.phaseAdv));
    }

    if (vsRhoLabel) {
        vsRhoLabel->setText(
            QString::number(
                frame.vsRho));
    }
}
