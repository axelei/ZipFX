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
#include "../ui/ProgressInfo.h"

class DragProgressDialog : public QDialog
{
public:
    explicit DragProgressDialog(int total, uint64_t totalBytes, QWidget* parent = nullptr)
        : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint)
        , m_totalBytes(totalBytes)
    {
        setWindowTitle(tr("Extracting files..."));
        setWindowModality(Qt::ApplicationModal);
        setFixedSize(440, 160);

        auto* layout = new QVBoxLayout(this);

        m_fileLabel = new QLabel(this);
        m_fileLabel->setWordWrap(true);
        layout->addWidget(m_fileLabel);

        // Per-mille range (0–1000) for byte-accurate display
        m_bar = new QProgressBar(this);
        m_bar->setRange(0, 1000);
        m_bar->setValue(0);
        layout->addWidget(m_bar);

        m_etaLabel = new QLabel(this);
        m_etaLabel->setStyleSheet("color: gray;");
        layout->addWidget(m_etaLabel);

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

        m_pi.start(totalBytes);

        show();
        QApplication::processEvents();
    }

    // Called from the extract progress callback (gated externally to ~100 ms).
    void updateProgress(int perMille, const QString& filename, const QString& eta)
    {
        m_bar->setValue(perMille);
        m_fileLabel->setText(filename);
        m_etaLabel->setText(eta);
        QApplication::processEvents();
    }

    // Call once per completed file to advance ProgressInfo bookkeeping.
    void advanceBytes(uint64_t fileBytes)
    {
        m_pi.addBytes(fileBytes);
        if (m_pi.shouldUpdate())
            m_pi.updateRate();
    }

    QString etaString() { return m_pi.etaString(); }

    void finishProgress()
    {
        m_bar->setValue(1000);
        m_etaLabel->clear();
        QApplication::processEvents();
    }

    bool wasCancelled() const { return m_cancelled; }
    AfterAction afterAction() const
    {
        return static_cast<AfterAction>(m_afterCombo->currentIndex());
    }

    ProgressInfo  m_pi;
    uint64_t      m_totalBytes = 0;

private:
    QProgressBar* m_bar       = nullptr;
    QLabel*       m_fileLabel = nullptr;
    QLabel*       m_etaLabel  = nullptr;
    QComboBox*    m_afterCombo = nullptr;
    bool          m_cancelled = false;
};

#endif
