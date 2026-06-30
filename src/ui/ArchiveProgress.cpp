#include "ArchiveProgress.h"
#include "ProgressInfo.h"
#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEntry.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

namespace ArchiveProgress {

bool extractFile(ArchiveEngine* engine, const ArchiveEntry& entry,
                  const QString& destFile, ProgressInfo& pi,
                  uint64_t baseBytes, QProgressDialog* progressDlg)
{
    struct { std::mutex m; ArchiveEngine::ExtractProgressInfo info; } ep;

    engine->setExtractProgressCb([&](const ArchiveEngine::ExtractProgressInfo& info) {
        std::lock_guard<std::mutex> lock(ep.m);
        ep.info = info;
    });

    std::atomic<bool> extractDone{false};
    std::atomic<bool> extractOk{false};
    std::string entryPath = entry.path;
    std::string destStr = destFile.toStdString();

    std::thread extractThread([&]() {
        extractOk = engine->Extract(entryPath, destStr);
        extractDone = true;
    });

    QString name = QString::fromUtf8(entry.name.c_str());

    while (!extractDone)
    {
        QApplication::processEvents();
        QThread::msleep(16);

        if (progressDlg && progressDlg->wasCanceled())
            engine->cancelExtract();

        uint64_t fileBytesNow = 0;
        {
            std::lock_guard<std::mutex> lock(ep.m);
            fileBytesNow = ep.info.bytesProcessed;
        }

        pi.bytesProcessed = baseBytes + fileBytesNow;
        if (pi.shouldUpdate())
        {
            pi.updateRate();
            int pct = pi.percent();
            QString eta = pi.etaString();
            if (progressDlg)
            {
                progressDlg->setValue(pct);
                QString label = name + QChar('\n')
                    + QString("%1%").arg(pct)
                    + (eta.isEmpty() ? QString() : QChar('\n') + eta);
                progressDlg->setLabelText(label);
            }
        }
    }

    if (extractThread.joinable())
        extractThread.join();

    engine->setExtractProgressCb(nullptr);
    return extractOk;
}

SaveResult save(ArchiveEngine* engine, QProgressDialog* progressDlg,
                 QWidget* parent)
{
    progressDlg->setLabelText(QObject::tr("Saving..."));
    progressDlg->setRange(0, 0);
    QApplication::processEvents();

    std::atomic<bool> saveDone{false};
    std::atomic<bool> saveOk{false};
    std::mutex spMutex;
    ArchiveEngine::SaveProgressInfo spInfo;
    uint64_t prevBytes = 0;
    ProgressInfo savePi;

    engine->setSaveProgressCb([&](const ArchiveEngine::SaveProgressInfo& info) {
        std::lock_guard<std::mutex> lock(spMutex);
        spInfo = info;
    });

    engine->resetSaveCancel();

    std::thread saveThread([engine, &saveDone, &saveOk]() {
        saveOk = engine->Save();
        saveDone = true;
    });

    bool userCancelled = false;

    while (!saveDone)
    {
        QApplication::processEvents();
        QThread::msleep(16);

        if (!userCancelled && progressDlg->wasCanceled())
        {
            engine->cancelSave();
            userCancelled = true;
            progressDlg->setLabelText(QObject::tr("Cancelling..."));
        }

        bool needsRangeInit = false;
        bool needsUpdate = false;
        int pct = 0;
        QString fileName;
        QString eta;

        {
            std::lock_guard<std::mutex> lock(spMutex);
            if (spInfo.totalBytes > 0)
            {
                if (savePi.totalBytes == 0)
                {
                    savePi.start(spInfo.totalBytes);
                    needsRangeInit = true;
                }
                savePi.addBytes(spInfo.bytesProcessed - prevBytes);
                prevBytes = spInfo.bytesProcessed;
                if (savePi.shouldUpdate() && !userCancelled)
                {
                    savePi.updateRate();
                    pct = savePi.percent();
                    fileName = QString::fromStdString(spInfo.fileName);
                    eta = savePi.etaString();
                    needsUpdate = true;
                }
            }
        }

        if (needsRangeInit)
        {
            progressDlg->setRange(0, 100);
            if (auto* lbl = progressDlg->findChild<QLabel*>())
                lbl->setAlignment(Qt::AlignLeft);
        }
        if (needsUpdate)
        {
            progressDlg->setValue(pct);
            QString label = fileName + QChar('\n')
                + QString("%1%").arg(pct)
                + (eta.isEmpty() ? QString() : QChar('\n') + eta);
            progressDlg->setLabelText(label);
        }
    }

    if (saveThread.joinable())
        saveThread.join();

    engine->setSaveProgressCb(nullptr);

    progressDlg->close();
    delete progressDlg;

    if (engine->isSaveCancelled())
        return SaveResult::Cancelled;

    if (!saveOk)
    {
        QMessageBox::warning(parent, QObject::tr("Error"),
            QObject::tr("Failed to save archive."));
        return SaveResult::Failed;
    }

    return SaveResult::Ok;
}

bool runSave(ArchiveEngine* engine, const QString& label,
              QWidget* parent)
{
    QDialog dlg(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    dlg.setWindowTitle(label.isEmpty() ? QObject::tr("Saving...") : label);
    dlg.setFixedSize(300, 80);
    auto* lay = new QVBoxLayout(&dlg);
    auto* lbl = new QLabel(label.isEmpty() ? QObject::tr("Saving...") : label, &dlg);
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
    auto* bar = new QProgressBar(&dlg);
    bar->setRange(0, 0);
    lay->addWidget(bar);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.show();
    QApplication::processEvents();

    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};

    engine->resetSaveCancel();

    std::thread t([engine, &done, &ok]() {
        ok = engine->Save();
        done = true;
    });

    while (!done)
        QApplication::processEvents(QEventLoop::AllEvents, 16);

    if (t.joinable()) t.join();

    if (!ok)
        QMessageBox::warning(parent, QObject::tr("Error"),
            QObject::tr("Failed to save archive."));

    return ok;
}

} // namespace ArchiveProgress
