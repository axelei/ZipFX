#include "ChecksumsDialog.h"
#include "../engine/ArchiveEngine.h"
#include "../engine/ArchiveEntry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QCryptographicHash>

#include <zlib.h>

ChecksumsDialog::ChecksumsDialog(ArchiveEngine* engine,
                                 const std::vector<std::string>& names,
                                 const std::vector<const ArchiveEntry*>& entries,
                                 QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_names(names)
    , m_entries(entries)
{
    setWindowTitle(tr("Checksums"));
    resize(720, 340);

    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(static_cast<int>(names.size()), 5, this);
    m_table->setHorizontalHeaderLabels({tr("File"), tr("Size"),
                                        tr("CRC32 (stored)"), tr("CRC32 (computed)"),
                                        tr("SHA-256")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    for (int i = 0; i < static_cast<int>(names.size()); ++i)
    {
        const ArchiveEntry* e = entries[i];
        m_table->setItem(i, 0, new QTableWidgetItem(
            QString::fromStdString(names[i])));
        m_table->setItem(i, 1, new QTableWidgetItem(
            QString::number(static_cast<qint64>(e->size))));
        m_table->setItem(i, 2, new QTableWidgetItem(
            e->crc ? QString::asprintf("%08X", e->crc) : tr("N/A")));
        m_table->setItem(i, 3, new QTableWidgetItem(tr("Computing...")));
        m_table->setItem(i, 4, new QTableWidgetItem(tr("Computing...")));
    }
    layout->addWidget(m_table);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, static_cast<int>(names.size()));
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Computing checksums..."), this);
    layout->addWidget(m_statusLabel);

    auto* btnRow = new QHBoxLayout();
    m_copyBtn = new QPushButton(tr("Copy to Clipboard"), this);
    m_copyBtn->setEnabled(false);
    m_closeBtn = new QPushButton(tr("Cancel"), this);
    btnRow->addWidget(m_copyBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_closeBtn);
    layout->addLayout(btnRow);

    connect(m_copyBtn,  &QPushButton::clicked, this, &ChecksumsDialog::copyToClipboard);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_cancelled = true;
        accept();
    });

    QTimer::singleShot(0, this, &ChecksumsDialog::compute);
}

void ChecksumsDialog::compute()
{
    for (int i = 0; i < static_cast<int>(m_names.size()); ++i)
    {
        if (m_cancelled) break;

        m_statusLabel->setText(tr("Computing %1 of %2: %3")
            .arg(i + 1)
            .arg(static_cast<int>(m_names.size()))
            .arg(QString::fromStdString(m_names[i])));
        m_progress->setValue(i);
        QApplication::processEvents(QEventLoop::AllEvents);

        if (m_cancelled) break;

        auto data = m_engine->ReadFile(m_names[i]);

        uLong crc = crc32(0, nullptr, 0);
        if (!data.empty())
            crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));

        QCryptographicHash sha256hash(QCryptographicHash::Sha256);
        if (!data.empty())
            sha256hash.addData(reinterpret_cast<const char*>(data.data()),
                               static_cast<qsizetype>(data.size()));

        m_table->item(i, 3)->setText(QString::asprintf("%08X", static_cast<uint32_t>(crc)));
        m_table->item(i, 4)->setText(sha256hash.result().toHex().toUpper());

        m_progress->setValue(i + 1);
        QApplication::processEvents(QEventLoop::AllEvents);
    }

    if (!m_cancelled)
    {
        m_statusLabel->setText(tr("Done."));
        m_closeBtn->setText(tr("Close"));
        m_copyBtn->setEnabled(true);
    }
}

void ChecksumsDialog::copyToClipboard()
{
    QStringList lines;
    lines << QString("%-60s  %-8s  %-10s  %-10s  %s")
                 .arg(tr("File"), tr("Size"), tr("CRC32(s)"), tr("CRC32(c)"), tr("SHA-256"));
    for (int i = 0; i < m_table->rowCount(); ++i)
    {
        lines << QString("%1\t%2\t%3\t%4\t%5")
                     .arg(m_table->item(i, 0)->text())
                     .arg(m_table->item(i, 1)->text())
                     .arg(m_table->item(i, 2)->text())
                     .arg(m_table->item(i, 3)->text())
                     .arg(m_table->item(i, 4)->text());
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}
