#include "BatchOpsDialog.h"
#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QTextEdit>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QMessageBox>
#include <QApplication>
#include <QFileInfo>

#include <atomic>

BatchOpsDialog::BatchOpsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Batch Operations"));
    setMinimumSize(560, 420);
    auto* lay = new QVBoxLayout(this);

    auto* form = new QFormLayout();

    auto* srcEdit = new QLineEdit(this);
    auto* srcBtn  = new QPushButton(tr("Browse..."), this);
    auto* srcRow  = new QHBoxLayout();
    srcRow->addWidget(srcEdit, 1); srcRow->addWidget(srcBtn);
    form->addRow(tr("Source folder:"), srcRow);
    connect(srcBtn, &QPushButton::clicked, this, [this, srcEdit]() {
        QString d = QFileDialog::getExistingDirectory(this, tr("Select Source Folder"));
        if (!d.isEmpty()) srcEdit->setText(d);
    });

    auto* recurseCheck = new QCheckBox(tr("Include subfolders"), this);
    recurseCheck->setChecked(true);
    form->addRow(recurseCheck);

    auto* opCombo = new QComboBox(this);
    opCombo->addItem(tr("Test integrity of all archives"),  "test");
    opCombo->addItem(tr("Extract all archives"),            "extract");
    form->addRow(tr("Operation:"), opCombo);

    auto* outEdit = new QLineEdit(this);
    auto* outBtn  = new QPushButton(tr("Browse..."), this);
    auto* outRow  = new QHBoxLayout();
    outRow->addWidget(outEdit, 1); outRow->addWidget(outBtn);
    auto* outLabel = new QLabel(tr("Output folder:"), this);
    form->addRow(outLabel, outRow);
    connect(outBtn, &QPushButton::clicked, this, [this, outEdit]() {
        QString d = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"));
        if (!d.isEmpty()) outEdit->setText(d);
    });

    auto setOutVisible = [&]() {
        bool ex = (opCombo->currentData() == "extract");
        outLabel->setVisible(ex); outEdit->setVisible(ex); outBtn->setVisible(ex);
    };
    setOutVisible();
    connect(opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [&]() { setOutVisible(); });

    lay->addLayout(form);

    auto* log = new QTextEdit(this);
    log->setReadOnly(true);
    lay->addWidget(log, 1);

    auto* btnRow = new QHBoxLayout();
    auto* startBtn  = new QPushButton(tr("Start"), this);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* closeBtn  = new QPushButton(tr("Close"), this);
    startBtn->setDefault(true);
    cancelBtn->setEnabled(false);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addStretch();
    btnRow->addWidget(startBtn);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    std::atomic<bool> batchCancelled{false};
    connect(cancelBtn, &QPushButton::clicked, this, [&]() { batchCancelled = true; });

    connect(startBtn, &QPushButton::clicked, this, [=, this, &batchCancelled,
            srcEdit, opCombo, outEdit, log, recurseCheck,
            startBtn, cancelBtn, closeBtn]() {
        const QString srcFolder = srcEdit->text().trimmed();
        if (srcFolder.isEmpty() || !QDir(srcFolder).exists())
        {
            QMessageBox::warning(this, tr("Error"), tr("Please select a valid source folder."));
            return;
        }
        const QString op        = opCombo->currentData().toString();
        const QString outFolder = outEdit->text().trimmed();
        if (op == "extract" && outFolder.isEmpty())
        {
            QMessageBox::warning(this, tr("Error"), tr("Please select an output folder."));
            return;
        }

        startBtn->setEnabled(false);
        batchCancelled = false;
        cancelBtn->setEnabled(true);
        log->clear();

        QDirIterator::IteratorFlags flags = recurseCheck->isChecked()
            ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

        QStringList archivePaths;
        QDirIterator it(srcFolder, QDir::Files, flags);
        while (it.hasNext())
        {
            it.next();
            auto eng = ArchiveEngineFactory::CreateForFile(it.filePath().toStdString());
            if (eng) archivePaths << it.filePath();
        }

        log->append(tr("Found %1 archive(s).").arg(archivePaths.size()));
        QApplication::processEvents();

        int ok = 0, fail = 0;
        for (const auto& ap : archivePaths)
        {
            QApplication::processEvents();
            if (batchCancelled) { log->append(tr("\nCancelled by user.")); break; }
            log->append(QString("\n[%1]").arg(ap));

            auto eng = ArchiveEngineFactory::CreateForFile(ap.toStdString());
            if (!eng || !eng->Open(ap.toStdString()))
            {
                log->append(tr("  ERROR: could not open."));
                fail++;
                continue;
            }

            if (op == "test")
            {
                bool res = eng->TestIntegrity(nullptr, nullptr);
                if (res) { log->append(tr("  OK: integrity check passed.")); ok++; }
                else     { log->append(tr("  FAILED: integrity check failed.")); fail++; }
            }
            else
            {
                QFileInfo fi(ap);
                QString dest = outFolder + "/" + fi.completeBaseName();
                QDir().mkpath(dest);
                if (eng->ExtractAll(dest.toStdString()))
                {
                    log->append(tr("  OK: extracted to %1").arg(dest)); ok++;
                }
                else
                {
                    log->append(tr("  FAILED: extraction failed.")); fail++;
                }
            }
            QApplication::processEvents();
        }

        log->append(tr("\nDone. %1 succeeded, %2 failed.").arg(ok).arg(fail));
        cancelBtn->setEnabled(false);
        startBtn->setEnabled(true);
    });
}
