#include "PreviewPanel.h"
#include "engine/ArchiveEngine.h"

#include <QLabel>
#include <QStackedWidget>
#include <QTextEdit>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QFrame>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QFileInfo>

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(180);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 0, 0, 0);
    lay->setSpacing(4);

    m_info = new QLabel(tr("No selection"), this);
    m_info->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    m_stack = new QStackedWidget(this);

    auto* placeholder = new QLabel(tr("Select a file to preview"), m_stack);
    placeholder->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(placeholder);   // index 0

    m_text = new QTextEdit(m_stack);
    m_text->setReadOnly(true);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_stack->addWidget(m_text);        // index 1

    m_scene     = new QGraphicsScene(this);
    m_imageView = new QGraphicsView(m_scene, m_stack);
    m_imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_imageView->setFrameShape(QFrame::NoFrame);
    m_stack->addWidget(m_imageView);   // index 2

    lay->addWidget(m_stack, 1);
    hide();
}

void PreviewPanel::showEntry(ArchiveEngine& engine, const QString& entryPath,
                              uint64_t size, bool isDir)
{
    if (isDir)
    {
        m_info->setText(tr("Folder: %1").arg(QFileInfo(entryPath).fileName()));
        m_stack->setCurrentIndex(0);
        return;
    }

    m_info->setText(tr("%1\n%2 bytes")
        .arg(QFileInfo(entryPath).fileName()).arg(size));

    QString ext = QFileInfo(entryPath).suffix().toLower();
    std::string entryStd = entryPath.toStdString();

    if (size == 0 || !engine.SupportsViewFile())
    {
        m_stack->setCurrentIndex(0);
        return;
    }

    static const QStringList imgExts = {
        "png","jpg","jpeg","gif","bmp","svg","webp","ico","tiff","tif"};
    static constexpr size_t kPreviewLimit = 65536;

    if (imgExts.contains(ext))
    {
        auto data = engine.ReadFile(entryStd);
        if (!data.empty())
        {
            QPixmap pix;
            if (pix.loadFromData(data.data(), static_cast<uint32_t>(data.size())))
            {
                m_scene->clear();
                auto* pi = m_scene->addPixmap(pix);
                m_imageView->fitInView(pi->sceneBoundingRect(), Qt::KeepAspectRatio);
                m_stack->setCurrentIndex(2);
                return;
            }
        }
    }

    auto data = engine.ReadFilePartial(entryStd, kPreviewLimit);
    if (data.empty())
    {
        m_stack->setCurrentIndex(0);
        return;
    }

    // Detect text vs binary
    bool isText = true;
    for (size_t i = 0; i < data.size() && i < 512; ++i)
        if (data[i] == 0 || (data[i] < 9 && data[i] != 0x0A && data[i] != 0x0D))
            { isText = false; break; }

    if (isText)
    {
        m_text->setPlainText(QString::fromUtf8(
            reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size())));
    }
    else
    {
        QString hex;
        size_t maxHex = std::min(data.size(), size_t(256));
        for (size_t i = 0; i < maxHex; i += 16)
        {
            hex += QString("%1  ").arg(i, 6, 16, QChar('0'));
            for (size_t j = 0; j < 16; ++j)
            {
                if (i + j < maxHex) hex += QString("%1 ").arg(data[i+j], 2, 16, QChar('0'));
                else hex += "   ";
                if (j == 7) hex += " ";
            }
            hex += " ";
            for (size_t j = 0; j < 16 && i + j < maxHex; ++j)
            {
                char c = static_cast<char>(data[i+j]);
                hex += (c >= 32 && c < 127) ? c : '.';
            }
            hex += "\n";
        }
        m_text->setPlainText(hex);
    }
    m_stack->setCurrentIndex(1);
}

void PreviewPanel::showMessage(const QString& msg)
{
    m_info->setText(msg);
    m_stack->setCurrentIndex(0);
}
