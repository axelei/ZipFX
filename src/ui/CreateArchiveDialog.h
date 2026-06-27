#ifndef ZIPFX_CREATE_ARCHIVE_DIALOG_H
#define ZIPFX_CREATE_ARCHIVE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QStringList>
#include <cstdint>

class QFrame;
class QPushButton;

struct CreateArchiveResult
{
    QString path;
    QString format;
    int compressionLevel = 6;
    QStringList sourcePaths;
    QString password;
    bool encryptFilenames = false;
    int volumeSize = 0;
    QString comment;
    int compressionMethod = -1;     // -1=auto; else BitCompressionMethod int value
    uint32_t dictionarySize = 0;    // bytes, 0=default
    uint32_t wordSize = 0;          // 0=default
    uint32_t threadsCount = 0;      // 0=auto
    bool solidMode = true;
    bool solidModeSet = false;
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
    void onInstallRar();

private:
    void updateSourceDisplay();
    void updateFormatOptions();

    QStringList  m_sourcePaths;
    QLineEdit*   m_sourceEdit = nullptr;
    QLineEdit*   m_pathEdit = nullptr;
    QComboBox*   m_formatCombo = nullptr;
    QSpinBox*    m_levelSpin = nullptr;
    QComboBox*   m_methodCombo = nullptr;
    QLineEdit*   m_passwordEdit = nullptr;
    QCheckBox*   m_encryptNamesCheck = nullptr;
    QSpinBox*    m_volumeSpin = nullptr;
    QPlainTextEdit* m_commentEdit = nullptr;
    QGroupBox*   m_advancedGroup = nullptr;
    QComboBox*   m_dictCombo = nullptr;
    QSpinBox*    m_wordSpin = nullptr;
    QSpinBox*    m_threadsSpin = nullptr;
    QCheckBox*   m_solidCheck = nullptr;

    struct FormatInfo
    {
        QString name;
        bool supportsPassword;
        bool supportsEncryptNames;
        bool supportsVolumes;
        bool supportsAdvanced;
    };
    QList<FormatInfo> m_formats;

    QFrame*       m_rarWarning = nullptr;
    QPushButton*  m_createBtn  = nullptr;
};

#endif
