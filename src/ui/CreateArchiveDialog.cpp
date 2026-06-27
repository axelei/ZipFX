#include "CreateArchiveDialog.h"
#include "../engine/RarEngine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>

CreateArchiveDialog::CreateArchiveDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Archive"));
    resize(500, 400);

    m_formats = {
        { "ZIP",     true,  false, false, false },
        { "7z",      true,  true,  true,  true  },
        { "TAR.GZ",  false, false, false, false },
        { "RAR",     true,  false, true,  false },
    };

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

    // ── RAR not-found warning ──────────────────────────────
    m_rarWarning = new QFrame(this);
    m_rarWarning->setFrameStyle(QFrame::StyledPanel);
    auto* rwLayout = new QHBoxLayout(m_rarWarning);
    rwLayout->setContentsMargins(8, 4, 8, 4);
    rwLayout->addWidget(new QLabel(
        tr("RAR creation requires <b>rar</b> / WinRAR — not found on this system."),
        m_rarWarning), 1);
    if (RarEngine::canAutoInstall())
    {
        auto* installBtn = new QPushButton(
            tr("Install via %1")
                .arg(QString::fromStdString(RarEngine::autoInstallDescription())
                         .section(' ', 0, 0)),
            m_rarWarning);
        rwLayout->addWidget(installBtn);
        connect(installBtn, &QPushButton::clicked,
                this, &CreateArchiveDialog::onInstallRar);
    }
    m_rarWarning->setVisible(false);
    mainLayout->addWidget(m_rarWarning);

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

    auto methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:"), this));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Auto (LZMA2)"),   -1);
    m_methodCombo->addItem("LZMA2",              5);
    m_methodCombo->addItem("LZMA",               4);
    m_methodCombo->addItem("PPMd",               6);
    m_methodCombo->addItem("BZip2",              3);
    m_methodCombo->addItem("Deflate",            1);
    m_methodCombo->addItem(tr("Copy (Store)"),   0);
    methodLayout->addWidget(m_methodCombo, 1);
    mainLayout->addLayout(methodLayout);

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

    // ── Advanced (7z only) ─────────────────────────────────
    m_advancedGroup = new QGroupBox(tr("Advanced"), this);
    auto advLayout = new QFormLayout(m_advancedGroup);

    m_dictCombo = new QComboBox(m_advancedGroup);
    const struct { const char* label; uint32_t bytes; } kDictSizes[] = {
        { "Auto",    0 },
        { "64 KB",   64*1024 },
        { "256 KB",  256*1024 },
        { "1 MB",    1*1024*1024 },
        { "4 MB",    4*1024*1024 },
        { "8 MB",    8*1024*1024 },
        { "16 MB",   16*1024*1024 },
        { "32 MB",   32*1024*1024 },
        { "64 MB",   64*1024*1024 },
        { "128 MB",  128u*1024*1024 },
        { "256 MB",  256u*1024*1024 },
        { "512 MB",  512u*1024*1024 },
        { "1 GB",    1024u*1024*1024 },
    };
    for (const auto& d : kDictSizes)
        m_dictCombo->addItem(QString::fromLatin1(d.label), static_cast<uint>(d.bytes));
    advLayout->addRow(tr("Dictionary:"), m_dictCombo);

    m_wordSpin = new QSpinBox(m_advancedGroup);
    m_wordSpin->setRange(0, 273);
    m_wordSpin->setValue(0);
    m_wordSpin->setSpecialValueText(tr("Auto"));
    advLayout->addRow(tr("Word size:"), m_wordSpin);

    m_threadsSpin = new QSpinBox(m_advancedGroup);
    m_threadsSpin->setRange(0, 64);
    m_threadsSpin->setValue(0);
    m_threadsSpin->setSpecialValueText(tr("Auto"));
    advLayout->addRow(tr("Threads:"), m_threadsSpin);

    m_solidCheck = new QCheckBox(tr("Solid archive"), m_advancedGroup);
    m_solidCheck->setChecked(true);
    advLayout->addRow(QString(), m_solidCheck);

    mainLayout->addWidget(m_advancedGroup);

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
    m_createBtn = new QPushButton(tr("Create"), this);
    auto cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(m_createBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_createBtn, &QPushButton::clicked, this, &CreateArchiveDialog::onAccept);
    connect(cancelBtn,   &QPushButton::clicked, this, &QDialog::reject);

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

    bool adv = fmt.supportsAdvanced;
    if (m_methodCombo)     m_methodCombo->setEnabled(adv);
    if (m_advancedGroup)   m_advancedGroup->setEnabled(adv);

    bool rarBlocked = (fmt.name == "RAR" && !RarEngine::isAvailable());
    if (m_rarWarning) m_rarWarning->setVisible(rarBlocked);
    if (m_createBtn)  m_createBtn->setEnabled(!rarBlocked);
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
    int idx = m_formatCombo->currentIndex();
    bool adv = (idx >= 0 && idx < m_formats.size()) && m_formats[idx].supportsAdvanced;
    return {
        m_pathEdit->text(),
        m_formats[idx].name,
        m_levelSpin->value(),
        m_sourcePaths,
        m_passwordEdit->text(),
        m_encryptNamesCheck->isChecked(),
        m_volumeSpin->value(),
        m_commentEdit->toPlainText(),
        adv ? m_methodCombo->currentData().toInt() : -1,
        adv ? static_cast<uint32_t>(m_dictCombo->currentData().toUInt()) : 0u,
        adv ? static_cast<uint32_t>(m_wordSpin->value()) : 0u,
        adv ? static_cast<uint32_t>(m_threadsSpin->value()) : 0u,
        adv && m_solidCheck->isChecked(),
        adv  // solidModeSet
    };
}

// ── RAR auto-install ───────────────────────────────────────────────────────

void CreateArchiveDialog::onInstallRar()
{
    auto args = RarEngine::autoInstallArgs();
    if (args.empty()) return;

    QString prog = QString::fromStdString(args[0]);
    QStringList pargs;
    for (size_t i = 1; i < args.size(); ++i)
        pargs << QString::fromStdString(args[i]);

    // Progress dialog
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Install RAR"));
    dlg.setMinimumWidth(500);
    auto* vb = new QVBoxLayout(&dlg);
    vb->addWidget(new QLabel(
        tr("Running: <b>%1</b>")
            .arg(QString::fromStdString(RarEngine::autoInstallDescription())),
        &dlg));

    auto* log = new QPlainTextEdit(&dlg);
    log->setReadOnly(true);
    log->setMinimumHeight(160);
    vb->addWidget(log);

    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    closeBtn->setEnabled(false);
    auto* hb = new QHBoxLayout();
    hb->addStretch();
    hb->addWidget(closeBtn);
    vb->addLayout(hb);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto* proc = new QProcess(&dlg);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, [&]() {
        log->appendPlainText(QString::fromLocal8Bit(proc->readAll()));
    });

    bool installed = false;
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [&](int code, QProcess::ExitStatus) {
                installed = (code == 0);
                closeBtn->setEnabled(true);
                log->appendPlainText(installed
                    ? tr("\nInstalled successfully.")
                    : tr("\nFailed (exit %1). You can install manually:\n  %2")
                          .arg(code)
                          .arg(QString::fromStdString(RarEngine::autoInstallDescription())));
            });

    proc->start(prog, pargs);
    if (!proc->waitForStarted(5000))
    {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not launch the package manager.\n"
               "Install manually: %1")
                .arg(QString::fromStdString(RarEngine::autoInstallDescription())));
        return;
    }

    dlg.exec();

    if (installed)
    {
        RarEngine::resetFindCache();
        if (RarEngine::isAvailable())
            updateFormatOptions(); // hides warning, re-enables Create
    }
}
