#ifndef ZIPFX_PREVIEWPANEL_H
#define ZIPFX_PREVIEWPANEL_H

#include <QWidget>
#include <cstdint>

class QLabel;
class QStackedWidget;
class QTextEdit;
class QGraphicsView;
class QGraphicsScene;
class ArchiveEngine;

class PreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

    void showEntry(ArchiveEngine& engine, const QString& entryPath,
                    uint64_t size, bool isDir);
    void showMessage(const QString& msg);

private:
    QLabel*          m_info        = nullptr;
    QStackedWidget*  m_stack       = nullptr;
    QTextEdit*       m_text        = nullptr;
    QGraphicsView*   m_imageView   = nullptr;
    QGraphicsScene*  m_scene       = nullptr;
};

#endif
