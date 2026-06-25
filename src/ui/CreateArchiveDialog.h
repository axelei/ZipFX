#ifndef ZIPFX_CREATE_ARCHIVE_DIALOG_H
#define ZIPFX_CREATE_ARCHIVE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QStringList>

struct CreateArchiveResult
{
    QString path;
    QString format;
    int compressionLevel;
    QStringList sourcePaths;
    QString password;
    bool encryptFilenames = false;
    int volumeSize = 0;
    QString comment;
};

class CreateArchiveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CreateArchiveDialog(QWidget* parent = nullptr);
    CreateArchiveResult result() const;

private slots:
    void onBrowseDest();
    void onAddFiles();
    void onAddFolder();
    void onClearSources();
    void onFormatChanged(int index);
    void onAccept();

private:
    void updateSourceDisplay();
    void updateFormatOptions();

    QStringList  m_sourcePaths;
    QLineEdit*   m_sourceEdit = nullptr;
    QLineEdit*   m_pathEdit = nullptr;
    QComboBox*   m_formatCombo = nullptr;
    QSpinBox*    m_levelSpin = nullptr;
    QLineEdit*   m_passwordEdit = nullptr;
    QCheckBox*   m_encryptNamesCheck = nullptr;
    QSpinBox*    m_volumeSpin = nullptr;
    QPlainTextEdit* m_commentEdit = nullptr;

    struct FormatInfo
    {
        QString name;
        bool supportsPassword;
        bool supportsEncryptNames;
        bool supportsVolumes;
    };
    QList<FormatInfo> m_formats;
};

#endif
