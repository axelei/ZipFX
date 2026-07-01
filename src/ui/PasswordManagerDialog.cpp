#include "PasswordManagerDialog.h"
#include "KeychainHelper.h"

#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QPainter>

PasswordManagerDialog::PasswordManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    class PasswordDelegate : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        void paint(QPainter* painter, const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
        {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);
            opt.text = QString(opt.text.length(), QChar(0x2022));
            QStyledItemDelegate::paint(painter, opt, index);
        }
        QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt,
                              const QModelIndex& idx) const override
        {
            auto* ed = qobject_cast<QLineEdit*>(
                QStyledItemDelegate::createEditor(parent, opt, idx));
            if (ed) ed->setEchoMode(QLineEdit::Password);
            return ed;
        }
    };

    setWindowTitle(tr("Password Manager"));
    setMinimumSize(520, 320);
    auto* lay = new QVBoxLayout(this);

    lay->addWidget(new QLabel(
        tr("Saved passwords are applied automatically when opening archives."), this));
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Archive filename"), tr("Password")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    table->setItemDelegateForColumn(1, new PasswordDelegate(table));
    lay->addWidget(table, 1);

    // Load existing passwords from OS keychain
    QSettings s;
    QStringList archives = s.value("passwordManager/archives").toStringList();
    for (const auto& a : archives)
    {
        QString pwd = KeychainHelper::load(a);
        if (pwd.isEmpty()) continue;
        int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(a));
        table->setItem(row, 1, new QTableWidgetItem(pwd));
    }

    auto* btnRow = new QHBoxLayout();
    auto* addBtn  = new QPushButton(tr("Add..."), this);
    auto* delBtn  = new QPushButton(tr("Delete Selected"), this);
    auto* saveBtn = new QPushButton(tr("Save && Close"), this);
    saveBtn->setDefault(true);

    connect(addBtn, &QPushButton::clicked, this, [this, table]() {
        QDialog addDlg(this);
        addDlg.setWindowTitle(tr("Add Password"));
        addDlg.setMinimumWidth(360);
        auto* addLay = new QFormLayout(&addDlg);
        addLay->setContentsMargins(12, 12, 12, 12);
        addLay->setSpacing(8);

        auto* archiveEdit = new QLineEdit(&addDlg);
        archiveEdit->setPlaceholderText(tr("archive.zip"));
        auto* pwdEdit     = new QLineEdit(&addDlg);
        pwdEdit->setEchoMode(QLineEdit::Password);
        auto* confirmEdit = new QLineEdit(&addDlg);
        confirmEdit->setEchoMode(QLineEdit::Password);
        auto* matchLabel  = new QLabel(&addDlg);
        matchLabel->setStyleSheet(QStringLiteral("color: red; font-size: 10px;"));
        matchLabel->hide();

        addLay->addRow(tr("Archive:"),          archiveEdit);
        addLay->addRow(tr("Password:"),         pwdEdit);
        addLay->addRow(tr("Confirm password:"), confirmEdit);
        addLay->addRow(QString(), matchLabel);

        auto* addBtns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &addDlg);
        addLay->addRow(addBtns);

        connect(addBtns, &QDialogButtonBox::accepted, &addDlg, [&]() {
            if (pwdEdit->text() != confirmEdit->text()) {
                matchLabel->setText(tr("Passwords do not match."));
                matchLabel->show();
                return;
            }
            if (pwdEdit->text().isEmpty()) {
                matchLabel->setText(tr("Password cannot be empty."));
                matchLabel->show();
                return;
            }
            addDlg.accept();
        });
        connect(addBtns, &QDialogButtonBox::rejected, &addDlg, &QDialog::reject);

        if (addDlg.exec() == QDialog::Accepted) {
            int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(archiveEdit->text().isEmpty()
                ? tr("archive.zip") : archiveEdit->text()));
            table->setItem(row, 1, new QTableWidgetItem(pwdEdit->text()));
        }
    });

    connect(delBtn, &QPushButton::clicked, this, [table]() {
        QSet<int> toRemove;
        for (auto* item : table->selectedItems()) toRemove.insert(item->row());
        QList<int> rows(toRemove.begin(), toRemove.end());
        std::sort(rows.rbegin(), rows.rend());
        for (int r : rows) table->removeRow(r);
    });

    connect(saveBtn, &QPushButton::clicked, this, [this, table]() {
        QSettings s2;
        QStringList oldArchives = s2.value("passwordManager/archives").toStringList();
        QStringList newArchives;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            QString arc = table->item(row, 0)->text();
            QString pwd = table->item(row, 1)->text();
            if (arc.isEmpty() || pwd.isEmpty()) continue;
            KeychainHelper::save(arc, pwd);
            newArchives.append(arc);
        }
        // Remove keychain entries for deleted rows
        for (const auto& old : oldArchives)
            if (!newArchives.contains(old))
                KeychainHelper::remove(old);
        s2.setValue("passwordManager/archives", newArchives);
        accept();
    });

    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    lay->addLayout(btnRow);
}
