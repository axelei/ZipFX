#ifndef ZIPFX_DRAG_PROGRESS_DIALOG_H
#define ZIPFX_DRAG_PROGRESS_DIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "../ui/PowerManager.h"

class DragProgressDialog : public QDialog
{
public:
    explicit DragProgressDialog(int total, QWidget* parent = nullptr)
        : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint)
    {
        setWindowTitle(tr("Extracting files..."));
        setFixedSize(400, 160);

        auto* layout = new QVBoxLayout(this);

        m_bar = new QProgressBar(this);
        m_bar->setRange(0, total);
        layout->addWidget(m_bar);

        m_fileLabel = new QLabel(this);
        layout->addWidget(m_fileLabel);

        auto* afterRow = new QHBoxLayout();
        afterRow->addWidget(new QLabel(tr("After:"), this));
        m_afterCombo = new QComboBox(this);
        m_afterCombo->addItems(GetAfterActionLabels());
        afterRow->addWidget(m_afterCombo, 1);
        layout->addLayout(afterRow);

        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        auto* cancelBtn = new QPushButton(tr("Cancel"), this);
        connect(cancelBtn, &QPushButton::clicked, this, [this]() {
            m_cancelled = true;
        });
        btnLayout->addWidget(cancelBtn);
        layout->addLayout(btnLayout);

        show();
        QApplication::processEvents();
    }

    void updateProgress(int value, const QString& currentFile)
    {
        m_bar->setValue(value);
        m_fileLabel->setText(currentFile);
        QApplication::processEvents();
    }

    bool wasCancelled() const { return m_cancelled; }
    AfterAction afterAction() const
    {
        return static_cast<AfterAction>(m_afterCombo->currentIndex());
    }

private:
    QProgressBar* m_bar = nullptr;
    QLabel*       m_fileLabel = nullptr;
    QComboBox*    m_afterCombo = nullptr;
    bool          m_cancelled = false;
};

#endif
