#include "CreateArchiveDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

CreateArchiveDialog::CreateArchiveDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Archive"));
    resize(450, 200);

    auto mainLayout = new QVBoxLayout(this);

    auto pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel(tr("Save as:"), this));
    m_pathEdit = new QLineEdit(this);
    pathLayout->addWidget(m_pathEdit, 1);
    auto browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onBrowse);
    pathLayout->addWidget(browseBtn);
    mainLayout->addLayout(pathLayout);

    auto fmtLayout = new QHBoxLayout();
    fmtLayout->addWidget(new QLabel(tr("Format:"), this));
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItems({"ZIP", "7z", "TAR.GZ"});
    fmtLayout->addWidget(m_formatCombo, 1);
    mainLayout->addLayout(fmtLayout);

    auto lvlLayout = new QHBoxLayout();
    lvlLayout->addWidget(new QLabel(tr("Compression:"), this));
    m_levelSpin = new QSpinBox(this);
    m_levelSpin->setRange(0, 9);
    m_levelSpin->setValue(6);
    lvlLayout->addWidget(m_levelSpin);
    lvlLayout->addStretch();
    mainLayout->addLayout(lvlLayout);

    mainLayout->addWidget(new QLabel(
        tr("0 = Store (fast, large)\n9 = Maximum (slow, small)"), this));

    auto btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto okBtn = new QPushButton(tr("Create"), this);
    auto cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void CreateArchiveDialog::onBrowse()
{
    QString ext = "." + m_formatCombo->currentText().toLower();
    QString path = QFileDialog::getSaveFileName(this, tr("Save Archive"),
        "", tr("%1 (*%2)").arg(m_formatCombo->currentText(), ext));
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}

void CreateArchiveDialog::onAccept()
{
    if (m_pathEdit->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("Please choose a path."));
        return;
    }
    accept();
}

CreateArchiveResult CreateArchiveDialog::result() const
{
    return { m_pathEdit->text(), m_formatCombo->currentText(), m_levelSpin->value() };
}
