#include "CreateArchiveDialog.h"
#include "../engine/RarEngine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

CreateArchiveDialog::CreateArchiveDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Archive"));
    resize(500, 400);

    m_formats = {
        { "ZIP",     true,  false, false },
        { "7z",      true,  true,  true  },
        { "TAR.GZ",  false, false, false },
    };
    if (RarEngine::isAvailable())
        m_formats.append({ "RAR", true, false, true });

    auto mainLayout = new QVBoxLayout(this);

    // ── Source ─────────────────────────────────────────────
    auto* srcLabel = new QLabel(tr("Source files / folders to compress:"), this);
    mainLayout->addWidget(srcLabel);

    auto srcRow = new QHBoxLayout();
    m_sourceEdit = new QLineEdit(this);
    m_sourceEdit->setReadOnly(true);
    m_sourceEdit->setPlaceholderText(tr("None selected"));
    srcRow->addWidget(m_sourceEdit, 1);

    auto* filesBtn = new QPushButton(tr("Add Files..."), this);
    connect(filesBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onAddFiles);
    srcRow->addWidget(filesBtn);

    auto* folderBtn = new QPushButton(tr("Add Folder..."), this);
    connect(folderBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onAddFolder);
    srcRow->addWidget(folderBtn);

    auto* clearBtn = new QPushButton(tr("Clear"), this);
    connect(clearBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onClearSources);
    srcRow->addWidget(clearBtn);
    mainLayout->addLayout(srcRow);

    // ── Destination ────────────────────────────────────────
    auto dstLayout = new QHBoxLayout();
    dstLayout->addWidget(new QLabel(tr("Save as:"), this));
    m_pathEdit = new QLineEdit(this);
    dstLayout->addWidget(m_pathEdit, 1);
    auto dstBtn = new QPushButton(tr("Browse..."), this);
    connect(dstBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onBrowseDest);
    dstLayout->addWidget(dstBtn);
    mainLayout->addLayout(dstLayout);

    // ── Format ─────────────────────────────────────────────
    auto fmtLayout = new QHBoxLayout();
    fmtLayout->addWidget(new QLabel(tr("Format:"), this));
    m_formatCombo = new QComboBox(this);
    for (const auto& f : m_formats)
        m_formatCombo->addItem(f.name);
    fmtLayout->addWidget(m_formatCombo, 1);
    mainLayout->addLayout(fmtLayout);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CreateArchiveDialog::onFormatChanged);

    // ── Compression ────────────────────────────────────────
    auto lvlLayout = new QHBoxLayout();
    lvlLayout->addWidget(new QLabel(tr("Compression:"), this));
    m_levelSpin = new QSpinBox(this);
    m_levelSpin->setRange(0, 9);
    m_levelSpin->setValue(6);
    lvlLayout->addWidget(m_levelSpin);
    lvlLayout->addWidget(new QLabel(tr("0 = Store  ·  9 = Maximum"), this));
    lvlLayout->addStretch();
    mainLayout->addLayout(lvlLayout);

    // ── Encryption group ──────────────────────────────────
    auto encGroup = new QGroupBox(tr("Encryption"), this);
    auto encLayout = new QFormLayout(encGroup);

    m_passwordEdit = new QLineEdit(encGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("No password"));
    encLayout->addRow(tr("Password:"), m_passwordEdit);

    m_encryptNamesCheck = new QCheckBox(tr("Encrypt file names"), encGroup);
    m_encryptNamesCheck->setToolTip(tr("Only supported by 7z format"));
    encLayout->addRow(QString(), m_encryptNamesCheck);

    mainLayout->addWidget(encGroup);

    // ── Volumes group ──────────────────────────────────────
    auto volGroup = new QGroupBox(tr("Volumes (split)"), this);
    auto volLayout = new QHBoxLayout(volGroup);
    volLayout->addWidget(new QLabel(tr("Volume size:"), volGroup));
    m_volumeSpin = new QSpinBox(volGroup);
    m_volumeSpin->setRange(0, 65535);
    m_volumeSpin->setValue(0);
    m_volumeSpin->setSuffix(tr(" MB"));
    m_volumeSpin->setSpecialValueText(tr("None"));
    volLayout->addWidget(m_volumeSpin);
    volLayout->addStretch();
    mainLayout->addWidget(volGroup);

    // ── Comment ────────────────────────────────────────────
    auto commentGroup = new QGroupBox(tr("Archive Comment"), this);
    auto commentLayout = new QVBoxLayout(commentGroup);
    m_commentEdit = new QPlainTextEdit(commentGroup);
    m_commentEdit->setMaximumHeight(60);
    m_commentEdit->setPlaceholderText(tr("Optional archive comment"));
    commentLayout->addWidget(m_commentEdit);
    mainLayout->addWidget(commentGroup);

    // ── Buttons ────────────────────────────────────────────
    auto btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto okBtn = new QPushButton(tr("Create"), this);
    auto cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updateFormatOptions();
}

// ── Source handling ────────────────────────────────────────
void CreateArchiveDialog::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Select files to compress"));
    if (files.isEmpty()) return;

    for (const auto& f : files)
        if (!m_sourcePaths.contains(f))
            m_sourcePaths.append(f);

    updateSourceDisplay();
}

void CreateArchiveDialog::onAddFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder to compress"));
    if (dir.isEmpty()) return;

    if (!m_sourcePaths.contains(dir))
        m_sourcePaths.append(dir);

    updateSourceDisplay();
}

void CreateArchiveDialog::onClearSources()
{
    m_sourcePaths.clear();
    updateSourceDisplay();
}

void CreateArchiveDialog::updateSourceDisplay()
{
    if (m_sourcePaths.isEmpty())
    {
        m_sourceEdit->setText(QString());
        m_sourceEdit->setPlaceholderText(tr("None selected"));
        return;
    }

    int files = 0, dirs = 0;
    for (const auto& p : m_sourcePaths)
    {
        QFileInfo fi(p);
        if (fi.isDir()) dirs++; else files++;
    }

    QStringList parts;
    if (files > 0) parts << tr("%1 file(s)").arg(files);
    if (dirs > 0)  parts << tr("%1 folder(s)").arg(dirs);
    m_sourceEdit->setText(parts.join(tr(", ")));
}

// ── Destination ────────────────────────────────────────────
void CreateArchiveDialog::onBrowseDest()
{
    int idx = m_formatCombo->currentIndex();
    QString ext = "." + m_formats[idx].name.toLower();
    QString path = QFileDialog::getSaveFileName(this, tr("Save Archive"),
        "", tr("%1 (*%2)").arg(m_formats[idx].name, ext));
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}

// ── Format switching ───────────────────────────────────────
void CreateArchiveDialog::onFormatChanged(int)
{
    updateFormatOptions();
}

void CreateArchiveDialog::updateFormatOptions()
{
    int idx = m_formatCombo->currentIndex();
    if (idx < 0 || idx >= m_formats.size()) return;

    const auto& fmt = m_formats[idx];
    bool enc = fmt.supportsPassword;
    m_passwordEdit->setEnabled(enc);
    m_encryptNamesCheck->setEnabled(enc && fmt.supportsEncryptNames);
    m_volumeSpin->setEnabled(fmt.supportsVolumes);
}

// ── Accept ─────────────────────────────────────────────────
void CreateArchiveDialog::onAccept()
{
    if (m_pathEdit->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("Please choose a destination path."));
        return;
    }
    accept();
}

CreateArchiveResult CreateArchiveDialog::result() const
{
    return {
        m_pathEdit->text(),
        m_formats[m_formatCombo->currentIndex()].name,
        m_levelSpin->value(),
        m_sourcePaths,
        m_passwordEdit->text(),
        m_encryptNamesCheck->isChecked(),
        m_volumeSpin->value(),
        m_commentEdit->toPlainText()
    };
}
