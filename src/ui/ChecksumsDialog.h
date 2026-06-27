#pragma once

#include <QDialog>
#include <QTimer>
#include <string>
#include <vector>

class QTableWidget;
class QProgressBar;
class QLabel;
class QPushButton;
class ArchiveEngine;
struct ArchiveEntry;

class ChecksumsDialog : public QDialog
{
    Q_OBJECT
public:
    ChecksumsDialog(ArchiveEngine* engine,
                    const std::vector<std::string>& names,
                    const std::vector<const ArchiveEntry*>& entries,
                    QWidget* parent = nullptr);

private slots:
    void compute();
    void copyToClipboard();

private:
    ArchiveEngine* m_engine;
    std::vector<std::string> m_names;
    std::vector<const ArchiveEntry*> m_entries;
    bool m_cancelled = false;

    QTableWidget*  m_table;
    QProgressBar*  m_progress;
    QLabel*        m_statusLabel;
    QPushButton*   m_copyBtn;
    QPushButton*   m_closeBtn;
};
