#ifndef ZIPFX_CREATE_ARCHIVE_DIALOG_H
#define ZIPFX_CREATE_ARCHIVE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>

struct CreateArchiveResult
{
    QString path;
    QString format;
    int compressionLevel;
};

class CreateArchiveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CreateArchiveDialog(QWidget* parent = nullptr);
    CreateArchiveResult result() const;

private slots:
    void onBrowse();
    void onAccept();

private:
    QLineEdit* m_pathEdit = nullptr;
    QComboBox* m_formatCombo = nullptr;
    QSpinBox*  m_levelSpin = nullptr;
};

#endif
