#include "FindFilesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDateEdit>
#include <QCheckBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>

#include <chrono>
#include <cstdint>
#include <algorithm>

FindFilesDialog::FindFilesDialog(std::vector<ArchiveEntry> entries, QWidget* parent)
    : QDialog(parent), m_entries(std::move(entries))
{
    setWindowTitle(tr("Find Files in Archive"));
    setMinimumSize(650, 480);

    auto* mainLay = new QVBoxLayout(this);

    // ── Criteria ────────────────────────────────────────────────────────
    auto* filterBox = new QGroupBox(tr("Search Criteria"), this);
    auto* filterLay = new QFormLayout(filterBox);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("e.g. *.txt  or  report*  (blank = all files)"));
    filterLay->addRow(tr("Name pattern:"), m_nameEdit);

    // Size range
    auto* sizeRow = new QHBoxLayout();
    m_useSizeRange = new QCheckBox(tr("Size between"), this);
    m_minSizeSpin = new QSpinBox(this);
    m_minSizeSpin->setRange(0, 2000000);
    m_minSizeSpin->setSuffix(tr(" KB"));
    m_minSizeSpin->setEnabled(false);
    m_maxSizeSpin = new QSpinBox(this);
    m_maxSizeSpin->setRange(0, 2000000);
    m_maxSizeSpin->setSuffix(tr(" KB"));
    m_maxSizeSpin->setValue(100000);
    m_maxSizeSpin->setEnabled(false);
    sizeRow->addWidget(m_useSizeRange);
    sizeRow->addWidget(m_minSizeSpin);
    sizeRow->addWidget(new QLabel(tr("and"), this));
    sizeRow->addWidget(m_maxSizeSpin);
    sizeRow->addStretch();
    filterLay->addRow(sizeRow);
    connect(m_useSizeRange, &QCheckBox::toggled, m_minSizeSpin, &QSpinBox::setEnabled);
    connect(m_useSizeRange, &QCheckBox::toggled, m_maxSizeSpin, &QSpinBox::setEnabled);

    // Date range
    auto* dateRow = new QHBoxLayout();
    m_useDateRange = new QCheckBox(tr("Date between"), this);
    m_fromDate = new QDateEdit(QDate::currentDate().addMonths(-1), this);
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setEnabled(false);
    m_toDate = new QDateEdit(QDate::currentDate(), this);
    m_toDate->setCalendarPopup(true);
    m_toDate->setEnabled(false);
    dateRow->addWidget(m_useDateRange);
    dateRow->addWidget(m_fromDate);
    dateRow->addWidget(new QLabel(tr("and"), this));
    dateRow->addWidget(m_toDate);
    dateRow->addStretch();
    filterLay->addRow(dateRow);
    connect(m_useDateRange, &QCheckBox::toggled, m_fromDate, &QDateEdit::setEnabled);
    connect(m_useDateRange, &QCheckBox::toggled, m_toDate,   &QDateEdit::setEnabled);

    mainLay->addWidget(filterBox);

    // ── Results ─────────────────────────────────────────────────────────
    m_resultsTree = new QTreeWidget(this);
    m_resultsTree->setHeaderLabels({tr("Name"), tr("Path"), tr("Size"), tr("Modified")});
    m_resultsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_resultsTree->setRootIsDecorated(false);
    m_resultsTree->setAlternatingRowColors(true);
    m_resultsTree->setSortingEnabled(true);
    m_resultsTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLay->addWidget(m_resultsTree, 1);

    connect(m_resultsTree, &QTreeWidget::itemActivated,
            this, &FindFilesDialog::onItemActivated);

    // ── Buttons ─────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    auto* statusLbl = new QLabel(this);
    btnRow->addWidget(statusLbl, 1);

    auto* findBtn = new QPushButton(tr("Find"), this);
    findBtn->setDefault(true);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    connect(findBtn,  &QPushButton::clicked, this, [this, statusLbl]() {
        doSearch();
        statusLbl->setText(tr("%1 result(s)").arg(m_resultsTree->topLevelItemCount()));
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(findBtn);
    btnRow->addWidget(closeBtn);
    mainLay->addLayout(btnRow);

    connect(m_nameEdit, &QLineEdit::returnPressed, findBtn, &QPushButton::click);
}

void FindFilesDialog::doSearch()
{
    m_resultsTree->clear();

    const QString pattern     = m_nameEdit->text().trimmed();
    const bool useSizeRange   = m_useSizeRange->isChecked();
    const uint64_t minBytes   = useSizeRange
        ? static_cast<uint64_t>(m_minSizeSpin->value()) * 1024 : 0;
    const uint64_t maxBytes   = useSizeRange
        ? static_cast<uint64_t>(m_maxSizeSpin->value()) * 1024 : UINT64_MAX;
    const bool useDateRange   = m_useDateRange->isChecked();
    const QDateTime fromDt    = useDateRange
        ? QDateTime(m_fromDate->date(), QTime(0, 0)) : QDateTime();
    const QDateTime toDt      = useDateRange
        ? QDateTime(m_toDate->date(), QTime(23, 59, 59)) : QDateTime();

    static const std::chrono::system_clock::time_point kEpoch{};

    for (const auto& e : m_entries)
    {
        if (e.isDirectory) continue;

        const QString entryPath = QString::fromUtf8(e.path.c_str());
        const QString entryName = QFileInfo(entryPath).fileName();

        if (!pattern.isEmpty() && !QDir::match(pattern, entryName))
            continue;

        if (useSizeRange && (e.size < minBytes || e.size > maxBytes))
            continue;

        if (useDateRange)
        {
            if (e.modified == kEpoch) continue;
            auto tp = std::chrono::system_clock::to_time_t(e.modified);
            QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(tp));
            if (dt < fromDt || dt > toDt) continue;
        }

        auto* item = new QTreeWidgetItem(m_resultsTree);
        item->setText(0, entryName);
        item->setText(1, entryPath);

        // Size — right-align, sort numerically
        item->setText(2, QString::number(e.size));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(2, Qt::UserRole, static_cast<qulonglong>(e.size));

        if (e.modified != kEpoch)
        {
            auto tp = std::chrono::system_clock::to_time_t(e.modified);
            item->setText(3, QDateTime::fromSecsSinceEpoch(static_cast<qint64>(tp))
                                 .toString("yyyy-MM-dd hh:mm"));
        }

        item->setData(0, Qt::UserRole, entryPath);
    }
}

void FindFilesDialog::onItemActivated(QTreeWidgetItem* item, int /*column*/)
{
    if (item)
        emit entryActivated(item->data(0, Qt::UserRole).toString());
}
