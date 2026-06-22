#include <QApplication>
#include <QMainWindow>
#include <QTreeView>
#include <QStandardItemModel>
#include <QFileDialog>
#include <QMenuBar>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QTemporaryDir>
#include <QProgressDialog>
#include <QMessageBox>

#include <zip.h>
#include <string>
#include <vector>

struct Entry {
    std::string path;
    zip_uint64_t size;
};

class ZipModel : public QStandardItemModel {
public:
    explicit ZipModel(QObject* parent = nullptr) : QStandardItemModel(parent) {}
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        auto f = QStandardItemModel::flags(index);
        return f | Qt::ItemIsDragEnabled;
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Qt Drag POC");
        resize(600, 400);

        m_tree = new QTreeView(this);
        m_model = new ZipModel(this);
        m_model->setHorizontalHeaderLabels({"Name", "Size"});
        m_tree->setModel(m_model);
        m_tree->setDragEnabled(true);
        m_tree->setDragDropMode(QAbstractItemView::DragOnly);
        m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        setCentralWidget(m_tree);

        auto fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("&Open...", this, &MainWindow::openZip);

        // Need this for drag to work properly
        connect(m_tree, &QTreeView::pressed, this, &MainWindow::startDrag);
    }

private slots:
    void openZip() {
        QString path = QFileDialog::getOpenFileName(this, "Open ZIP", "",
            "ZIP (*.zip);;All (*.*)");
        if (path.isEmpty()) return;

        int err = 0;
        m_zip = zip_open(path.toUtf8().constData(), ZIP_RDONLY, &err);
        if (!m_zip) {
            QMessageBox::warning(this, "Error", "Cannot open ZIP file");
            return;
        }

        m_entries.clear();
        m_model->removeRows(0, m_model->rowCount());

        zip_int64_t num = zip_get_num_entries(m_zip, 0);
        for (zip_int64_t i = 0; i < num; ++i) {
            const char* name = zip_get_name(m_zip, i, 0);
            if (!name) continue;

            struct zip_stat st;
            zip_stat_init(&st);
            zip_stat_index(m_zip, i, 0, &st);

            Entry e;
            e.path = name;
            e.size = st.size;
            m_entries.push_back(e);

            auto nameItem = new QStandardItem(QString::fromUtf8(name));
            nameItem->setFlags(nameItem->flags() | Qt::ItemIsDragEnabled);
            auto sizeItem = new QStandardItem(
                st.size > 0 ? QString::number(st.size) : "");
            sizeItem->setFlags(sizeItem->flags() | Qt::ItemIsDragEnabled);
            m_model->appendRow({nameItem, sizeItem});
        }
        setWindowTitle(QString("Qt Drag POC — %1 (%2 files)")
            .arg(path).arg(num));
    }

    void startDrag(const QModelIndex& index) {
        if (!m_zip || !index.isValid()) return;

        auto sel = m_tree->selectionModel()->selectedRows(0);
        if (sel.isEmpty()) return;

        // Extract selected entries to temp dir
        QTemporaryDir tmpDir;
        if (!tmpDir.isValid()) return;
        QString tmpRoot = tmpDir.path() + "/";

        // Count files
        struct Extraction {
            QString srcName;
            QString dstPath;
        };
        std::vector<Extraction> extractions;

        for (const auto& idx : sel) {
            QString name = m_model->itemFromIndex(idx)->text();
            if (name.endsWith('/')) continue; // skip directories

            QString dest = tmpRoot + name;
            QString parent = QFileInfo(dest).path();
            QDir().mkpath(parent);

            extractions.push_back({name, dest});
        }

        if (extractions.empty()) return;

        // Show progress
        QProgressDialog prog("Extracting...", "Cancel", 0,
            (int)extractions.size(), this);
        prog.setWindowModality(Qt::WindowModal);

        for (int i = 0; i < (int)extractions.size(); ++i) {
            if (prog.wasCanceled()) break;
            prog.setValue(i);
            prog.setLabelText("Extracting: " + extractions[i].srcName);
            QApplication::processEvents();

            // Extract via libzip
            std::string name = extractions[i].srcName.toStdString();
            std::string dest = extractions[i].dstPath.toStdString();

            zip_int64_t idx = zip_name_locate(m_zip, name.c_str(), 0);
            if (idx < 0) continue;

            zip_file_t* zf = zip_fopen_index(m_zip, idx, 0);
            if (!zf) continue;

            struct zip_stat st;
            zip_stat_init(&st);
            zip_stat_index(m_zip, idx, 0, &st);

            std::vector<char> buf((size_t)st.size);
            if (st.size > 0)
                zip_fread(zf, buf.data(), buf.size());
            zip_fclose(zf);

            FILE* out = fopen(dest.c_str(), "wb");
            if (out) {
                fwrite(buf.data(), 1, buf.size(), out);
                fclose(out);
            }
        }

        prog.setValue((int)extractions.size());

        if (prog.wasCanceled()) return;

        // Build MIME data with local file URLs
        QMimeData* mime = new QMimeData();
        QList<QUrl> urls;
        for (const auto& ex : extractions)
            urls.append(QUrl::fromLocalFile(ex.dstPath));
        mime->setUrls(urls);

        // Start drag
        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime);

        // Set a simple pixmap — the first file's icon or a generic one
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        drag->setPixmap(pix);

        Qt::DropAction result = drag->exec(Qt::CopyAction);
        Q_UNUSED(result);

        // Temp files are cleaned up by QTemporaryDir
    }

private:
    zip_t* m_zip = nullptr;
    QTreeView* m_tree;
    ZipModel* m_model;
    std::vector<Entry> m_entries;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"
