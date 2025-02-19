#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QProcess>
#include <QProgressDialog>

#include "PSHelperMainWindow.h"

#include "PSandPhotoScanner.h"
#include "PSProjectDataModel.h"
#include "PSSessionData.h"
#include "PSProjectFileData.h"
//#include "ProgramPreferencesDialog.h"
#include "RawImageExposureDialog.h"
#include "GeneralSettingsDialog.h"
#include "InteractivePhotoScanDialog.h"
#include "ScriptedPhotoScanDialog.h"
#include "ScriptedPhase1Dialog.h"

#include "ui_AboutDialog.h"

#include "PSProjectInfoDialog.h"

#include "RawImageExposer.h"
#include "GLModelWidget.h"
#include "QueueableProcess.h"
#include "CancelableModalProgressDialog.h"
#include "QuickPreviewDialog.h"
#include "CreateNewSessionDialog.h"
#include "CaptureSessionDialog.h"
#include "PhotoScanPhase1Dialog.h"

const QString PSHelperMainWindow::WINDOWS_PATH = "C:\\Windows;C:\\Program Files\\ImageMagick;C:\\Program Files\\GraphicsMagick;C:\\Program Files\\exiftool;C:\\Program Files\\dcraw";
const QString PSHelperMainWindow::MAC_UNIX_PATH = "/usr/bin:/usr/local/bin:/opt/local/bin";

PSHelperMainWindow::PSHelperMainWindow(QWidget* parent) : QMainWindow(parent) {
    mContextMenu = new QMenu(this);
    mContextMenu->addAction("View Model", this, &PSHelperMainWindow::viewModel);
    mContextMenu->addAction("Edit General Settings", this, &PSHelperMainWindow::editGeneralSettings);
    mContextMenu->addSeparator();
    mContextMenu->addAction("Expose Raw Images", this, &PSHelperMainWindow::runExposeImagesAction);
    mContextMenu->addAction("Quick Preview", this, &PSHelperMainWindow::runQuickPreviewAction);
    mContextMenu->addAction("Interactive PhotoScan Console", this, &PSHelperMainWindow::runInteractiveConsoleAction);
    mContextMenu->addAction("Run PhotoScan Phase 1", this, &PSHelperMainWindow::runPhotoScanPhase1Action);
    mContextMenu->addAction("Run PhotoScan Phase 2", this, &PSHelperMainWindow::runPhotoScanPhase2Action);
    mContextMenu->addSeparator();
    mContextMenu->addAction("Queue For Expose Raw Images", this, &PSHelperMainWindow::queueExposeImagesAction);
    mContextMenu->addAction("Queue For PhotoScan Phase 1", this, &PSHelperMainWindow::queuePhotoScanPhase1Action);
    mContextMenu->addAction("Queue For PhotoScan Phase 2", this, &PSHelperMainWindow::queuePhotoScanPhase2Action);
    mContextMenu->addSeparator();
    mContextMenu->addAction("Create New Session", this, &PSHelperMainWindow::createNewSession);
    mContextMenu->addAction("Scan For New Sessions", this, &PSHelperMainWindow::scanForNewSessions);

    mGUI = new Ui_PSHelperMainWindow();
    mGUI->setupUi(this);
    setWindowTitle("PhotoScan Helper");

    mModelViewer = nullptr;

    mRawExposer = nullptr;
    mRawExposureProgressDialog = new CancelableModalProgressDialog<QFileInfo>("Exposing Raw Images", this);
//    mProcessQueue = new LinkedList<QueueableProcess<? extends Object>>();

    readSettings();

    mModelViewer = nullptr; // new GLModelViewer((PSSessionData)nullptr, nullptr); // Help
    // mModelViewer->show(); // Help
}

PSHelperMainWindow::~PSHelperMainWindow() {
    // TODO: Cleanup memory
}

void PSHelperMainWindow::setModelData(PSandPhotoScanner* pScanner) {
    mCollectionDir = pScanner->getRootPath();
    mDataModel = new PSProjectDataModel(pScanner->getPSProjectData(), this);
    //mDataInfoStore = pScanner->getInfoStore();

    updateProjectDataModel();
}

void PSHelperMainWindow::addModelData(PSandPhotoScanner *pScanner) {
    mCollectionDir = pScanner->getRootPath();

    QVector<PSSessionData*> lData = mDataModel->getData();
    for(int i = 0; i < pScanner->getPSProjectData().length(); i++) {
        lData.append(pScanner->getPSProjectData()[i]);
    }

    mDataModel = new PSProjectDataModel(lData, this);

    updateProjectDataModel();
}

void PSHelperMainWindow::addNewSession(PSSessionData* pSession) {
    mDataModel->appendNewSession(pSession);

    updateProjectDataModel();
}

void PSHelperMainWindow::updateProjectDataModel() {
    mDataModel->sort(PSSessionData::getSortBy());
    mGUI->PSDataTableView->setModel(mDataModel);
    mGUI->PSDataTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        mGUI->PSDataTableView, &QTableView::customContextMenuRequested,
        this, &PSHelperMainWindow::showContextMenu
    );

    QSettings settings;
    settings.beginGroup("ViewOptions");
    int extended = settings.value("ExtendedInfo", "0").toInt();
    int colors = settings.value("ColorsForStatus", "0").toInt();
    settings.endGroup();

    if(extended == 1) {
        mGUI->action_ShowExtendedInfo->setChecked(true);
        mDataModel->setExtendedColsEnabled(true);
    }

    if(colors == 1) {
        mGUI->action_ShowColorsForStatus->setChecked(true);
        mDataModel->setShowColorForStatus(true);
    }


    mGUI->DataInfoLabel->setText(
        QString::asprintf("%d projects (%d unique), %d w/o PSZ fies, %d w/o image align, %d w/o dense cloud, %d w/o model",
        mDataModel->getData().size(), mDataModel->countUniqueDirs(), mDataModel->countDirsWithoutProjects(),
        mDataModel->countDirsWithoutImageAlign(), mDataModel->countDirsWithoutDenseCloud(),
        mDataModel->countDirsWithoutModels())
    );
    mGUI->PSDataTableView->resizeColumnsToContents();
    mGUI->PSDataTableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void PSHelperMainWindow::showContextMenu(const QPoint& pos) {
    QModelIndex lIndex = mGUI->PSDataTableView->indexAt(pos);
    mLastData = mDataModel->getDataAtIndex(lIndex.row());
    mLastDataRow = lIndex.row();
    mContextMenu->popup(QCursor::pos());
}

bool PSHelperMainWindow::validateSettings() {

    // Keep looping until we get valid paths (proceeding is useless until this passes)
//    while(true) {
//        // Test to see if the paths work
//        if(ImageProcessorIM4J.locatePrograms()) {
//            return true;
//        } else {
//            // Settings are wrong so warn and prompt to update or cancel
//            int button = QMessageBox.warning(this, "IM4J Search Failed", "Could not locate the necessary utilities. "
//                                    + "Please update your im4j path settings.", QMessageBox.StandardButton.Ok,
//                                    QMessageBox.StandardButton.Cancel);
//            if(button == QMessageBox.StandardButton.Cancel.value()) {
//                return false;
//            }

//            // Show the preferences dialog for updating (
//            ProgramPreferencesDialog prefsDiag = new ProgramPreferencesDialog(nullptr, this);
//            if(prefsDiag.exec() == QDialog.DialogCode.Accepted.value()) {
//                prefsDiag.writeResults(nullptr);
//                writeSettings();
//            } else {
//                // They cancelled so return false
//                return false;
//            }
//        }
//    }
    return true;
}

void PSHelperMainWindow::writeSettings() {
    QSettings settings;

    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("pos", pos());
    settings.endGroup();

//    String searchPath = ImageProcessorIM4J.getSearchPath();
//    String useGM = (ImageProcessorIM4J.getUseGraphicsMagick()?"true":"false");
//    String exifOverride = (ImageProcessorIM4J.getExiftoolOverrideBin()==nullptr?"":
//                            ImageProcessorIM4J.getExiftoolOverrideBin());
//    String dcrawOverride = (ImageProcessorIM4J.getDcrawOverrideBin()==nullptr?"":
//                            ImageProcessorIM4J.getDcrawOverrideBin());
//    String IMOverride = (ImageProcessorIM4J.getImageMagickOverrideBin()==nullptr?"":
//                            ImageProcessorIM4J.getImageMagickOverrideBin());

    settings.beginGroup("Paths");
//    settings.setValue("im4j Search Path", searchPath);
//    settings.setValue("im4j exiftool Override Path", exifOverride);
//    settings.setValue("im4j dcraw Override Path", dcrawOverride);
//    settings.setValue("im4j ImageMagick Override Path", IMOverride);
//    settings.setValue("im4j Use GraphicsMagick", useGM);

    settings.endGroup();
}

void PSHelperMainWindow::readSettings() {
    QSettings settings;

    // Remember window position and such
    settings.beginGroup("MainWindow");
    resize(settings.value("size", QSize(800, 600)).toSize());
    move(settings.value("pos", QPoint(100, 100)).toPoint());
    settings.endGroup();

    // Remember the search path for use by im4Java
    QString defaultPath = "";
    #ifdef Q_OS_WIN32
    defaultPath = WINDOWS_PATH;
    #elif defined(Q_OS_MAC)
    defaultPath = MAC_UNIX_PATH;
    #endif

//    settings.beginGroup("Paths");
//    String searchPath = settings.value("im4j Search Path", defaultPath).toString();
//    String exifOverridePath = settings.value("im4j exiftool Override Path", "").toString();
//    String dcrawOverridePath = settings.value("im4j dcraw Override Path", "").toString();
//    String IMOverridePath = settings.value("im4j ImageMagick Override Path", "").toString();
//    String useGM = settings.value("im4j Use GraphicsMagick", "false").toString();
//    settings.endGroup();

//    useGM = useGM.toLowerCase();
//    ImageProcessorIM4J.setSearchPath(searchPath);
//    ImageProcessorIM4J.setExiftoolOverrideBin(exifOverridePath);
//    ImageProcessorIM4J.setDcrawOverrideBin(dcrawOverridePath);
//    ImageProcessorIM4J.setImageMagickOverrideBin(IMOverridePath);
//    ImageProcessorIM4J.setUseGraphicsMagick(useGM.equals("true"));
}

void PSHelperMainWindow::closeEvent(QCloseEvent* event) {
    writeSettings();
    event->accept();
}

void PSHelperMainWindow::on_PSDataTableView_doubleClicked(const QModelIndex &pIndex) {
    PSSessionData* lProjData = mDataModel->getDataAtIndex(pIndex.row());
    if(lProjData == nullptr) return;
    PSProjectInfoDialog* lProjDialog = new PSProjectInfoDialog(lProjData, this);
    lProjDialog->exec();
    delete lProjDialog;
}

void PSHelperMainWindow::on_action_WriteToCSVFile_triggered(bool pChecked) {
    (void)pChecked;
    QString filename = QFileDialog::getSaveFileName(this, "Specify a CSV file to save data.", "");
    if(filename.isNull() && !filename.isEmpty()) {
        if(!mDataModel->outputToCSVFile(filename)) {
            QMessageBox::warning(this, "CSV Saving Failed", "An error occured while saving to a CSV file.");
        }
    }
}

void PSHelperMainWindow::on_action_ShowExtendedInfo_triggered(bool pChecked) {
    mDataModel->setExtendedColsEnabled(pChecked);
    mGUI->PSDataTableView->resizeColumnsToContents();

    QSettings settings;
    settings.beginGroup("ViewOptions");
    settings.setValue("ExtendedInfo", (pChecked?1:0));
    settings.endGroup();
}

void PSHelperMainWindow::on_action_ShowColorsForStatus_triggered(bool pChecked) {
    mDataModel->setShowColorForStatus(pChecked);
    mGUI->PSDataTableView->resizeColumnsToContents();

    QSettings settings;
    settings.beginGroup("ViewOptions");
    settings.setValue("ColorsForStatus", (pChecked?1:0));
    settings.endGroup();
}

void PSHelperMainWindow::on_PreferencesAction_triggered(bool pChecked) {
    Q_UNUSED(pChecked);
//    ProgramPreferencesDialog* prefsDiag = new ProgramPreferencesDialog(mDataInfoStore, this);
//    if(prefsDiag->exec() == QDialog::Accepted) {
//        prefsDiag->writeResults(mDataInfoStore);
//        writeSettings();
//    }
}

void PSHelperMainWindow::on_AboutAction_triggered(bool pChecked) {
    (void)pChecked;
    QDialog* aboutDialog = new QDialog(this);
    Ui_AboutDialog* aboutUI = new Ui_AboutDialog();

    aboutUI->setupUi(aboutDialog);
    QPixmap* iconPix = new QPixmap(":/PSHelper/icon.png");
    if(iconPix != nullptr && !iconPix->isNull()) {
        int w = aboutUI->iconLabel->width();
        int h = aboutUI->iconLabel->height();
        aboutUI->iconLabel->setPixmap(
            iconPix->scaled(w,h,Qt::KeepAspectRatio)
        );
    }

    aboutDialog->exec();
    delete aboutDialog;
}

void PSHelperMainWindow::on_QuitAction_triggered(bool pChecked) {
    (void)pChecked;
    this->close();
}

void PSHelperMainWindow::on_ClearQueueButton_clicked() {
    int lResult = QMessageBox::question(this, "Clear the queue?",
            "Remove all queued processes?", QMessageBox::Cancel, QMessageBox::Yes);
    if(lResult == QMessageBox::Yes) {
        mGUI->QueueListWidget->clear();
        mProcessQueue.clear();
    }
}

void PSHelperMainWindow::on_RunQueueButton_clicked() {
    if(mGUI->QueueListWidget->count() < 1) {
        QMessageBox::warning(this, "Nothing to do", "Please add something to the queue first.");
    } else {
        int lResult = QMessageBox::question(this, "Begin processing?",
                "Run all queued actions?\n\nNote: This may take a long time.",
                QMessageBox::Cancel, QMessageBox::Yes);
        if(lResult == QMessageBox::Yes) {
//            ProcessQueueProgressDialog lQueueDialog = new ProcessQueueProgressDialog(mProcessQueue, this);
//            lQueueDialog.stageComplete.connect(this, "dequeue()");
//            lQueueDialog.show();
//            lQueueDialog.startProcessQueue();
        }
    }
}

void PSHelperMainWindow::viewModel() {
    if(mModelViewer == nullptr) {
        // Parent is left NULL so this doesn't live INSIDE the main window
        mModelViewer = new GLModelWidget(static_cast<QWidget*>(nullptr));
    }
    PSProjectFileData* lPSProject = new PSProjectFileData(mLastData->getPSProjectFile());
    mModelViewer->loadNewModel(lPSProject->getModelData());
    mModelViewer->show();
}

void PSHelperMainWindow::editGeneralSettings() {
    GeneralSettingsDialog* lDialog = new GeneralSettingsDialog(mLastData, this);
    if(lDialog->exec() == QDialog::Accepted) {
        qDebug("Writing general settings for session %s", mLastData->getName().toLocal8Bit().data());
        mLastData->writeGeneralSettings();
    }
}

void PSHelperMainWindow::runExposeImagesAction() {
    if(mLastData == nullptr) return;

    RawImageExposureDialog* lExposureDialog = new RawImageExposureDialog(this);
    lExposureDialog->setProjectData(mLastData);
    int result = lExposureDialog->exec();

    if(result == QDialog::Accepted) {
        try {
            mRawExposer = new RawImageExposer(*mLastData, *lExposureDialog->getExposureSettings(),
                                              lExposureDialog->getDestinationPath());
        } catch (std::exception e) {
            qWarning() << e.what();
        }

        mRawExposureProgressDialog->setFuture(mRawExposer->runProcess());
        mRawExposureProgressDialog->exec();

        if(!mRawExposureProgressDialog->wasCanceled()) {
            try {
                mLastData->getProcessedFileList(true);
                mLastData->autoSetStatus();
                mLastData->writeGeneralSettings();
                mGUI->PSDataTableView->update(mDataModel->index(mLastDataRow, (int)PSSessionData::F_IMAGE_COUNT_REAL));
            } catch (std::exception e) {
                qWarning() << "Error: exception while rebuilding PS Project Data.";
                qWarning() << e.what();
            }
        }
    }
}

void PSHelperMainWindow::runQuickPreviewAction()
{
    QuickPreviewDialog lQPDialog(this);
    int lResult = lQPDialog.exec();
    if (lResult == QDialog::Accepted) {
        ScriptedPhotoScanDialog lSPSDialog(mLastData, lQPDialog.getMaskDir(), lQPDialog.getTextureSize(), lQPDialog.getTolerance(), this);
        lSPSDialog.exec();
    }
}

void PSHelperMainWindow::runInteractiveConsoleAction() {
    InteractivePhotoScanDialog lInteractiveDialog(this);
    lInteractiveDialog.exec();
}

void PSHelperMainWindow::runPhotoScanPhase1Action() {
    PhotoScanPhase1Dialog lPhase1Dialog(this);
    int lResult = lPhase1Dialog.exec();
    if(lResult == QDialog::Accepted) {
        ScriptedPhase1Dialog lSP1Dialog(mLastData,
                                        lPhase1Dialog.getProjectName(),
                                        lPhase1Dialog.getAccuracy(),
                                        lPhase1Dialog.getGenericPreselection(),
                                        lPhase1Dialog.getReferencePreselection(),
                                        lPhase1Dialog.getKeyPointLimit(),
                                        lPhase1Dialog.getTiePointLimit(),
                                        lPhase1Dialog.getApplyMasks(),
                                        "10",
                                        lPhase1Dialog.getAdaptiveCameraModel(),
                                        lPhase1Dialog.getBuildDenseCloud(),
                                        lPhase1Dialog.getQuality(),
                                        lPhase1Dialog.getDepthFiltering(),
                                        lPhase1Dialog.getPointColors(),
                                        this);
        lSP1Dialog.exec();
    }
}

void PSHelperMainWindow::dequeue() {
    if(mGUI->QueueListWidget->count() > 0) {
        mGUI->QueueListWidget->removeItemWidget(mGUI->QueueListWidget->item(0));
    }
}

void PSHelperMainWindow::queueExposeImagesAction() {
    if(mLastData == nullptr) return;
    RawImageExposureDialog* lExposureDialog = new RawImageExposureDialog(this);
    lExposureDialog->setEnqueueMode(true);
    lExposureDialog->setProjectData(mLastData);
    int result = lExposureDialog->exec();

    if(result == QDialog::Accepted) {
//        try {
//            mRawExposer = new RawImageExposer(mLastData, lExposureDialog->getExposureSettings(),
//                    lExposureDialog->getDestinationPath());
//            mProcessQueue.add(mRawExposer);
//            QListWidgetItem lNewItem = new QListWidgetItem(mRawExposer->describeProcess(), mGUI->QueueListWidget);
//        } catch (std::exception& e) {}
    }
}

void PSHelperMainWindow::createNewSession() {
    CreateNewSessionDialog lNewSessionDialog(mCollectionDir, mDataModel, this);
    int lRet = lNewSessionDialog.exec();
    QDir lCollectionDir(mCollectionDir);
    PSSessionData* lNewSession;

    switch (lRet) {
    case QDialog::Accepted:
        lCollectionDir.mkdir(lNewSessionDialog.getSessionFolderName());
        lNewSession = new PSSessionData(QDir(mCollectionDir + QDir::separator() + lNewSessionDialog.getSessionFolderName()));
        lNewSession->convertToPSSession(lNewSessionDialog.getRawFolderPath(), lNewSessionDialog.getProcessedFolderPath(), lNewSessionDialog.getMasksFolderPath());
        addNewSession(lNewSession);
        break;
    case QDialog::Rejected:
        return;
        break;
    default:
        break;
    }

    CaptureSessionDialog lCaptureSessionDialog(lNewSession, this);
    lRet = lCaptureSessionDialog.exec();

    switch (lRet) {
    case QDialog::Accepted:
        lNewSession->updateOutOfSyncSession();
        break;
    case QDialog::Rejected:
        break;
    default:
        break;
    }
}

void PSHelperMainWindow::scanForNewSessions() {
    // Clear NeedsApproval vector
    PSSessionData::clearNeedsApproval();

    // Prepare a progress dialog
    QProgressDialog* myProgressDiag = new QProgressDialog("Scanning Files/Folders", "Cancel", 0, 0);
    myProgressDiag->setWindowModality(Qt::WindowModal);
    myProgressDiag->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);

    // Scan the given directory for PSZ files and images
    PSandPhotoScanner* lScanner = nullptr;
    try {
        // Use a future watcher to signal the progress dialog to close
        QFutureWatcher<PSSessionData*>* lWatcher = new QFutureWatcher<PSSessionData*>();
        QObject::connect(lWatcher, &QFutureWatcher<PSSessionData*>::progressRangeChanged,
                myProgressDiag, &QProgressDialog::setRange);
        QObject::connect(lWatcher, &QFutureWatcher<PSSessionData*>::progressValueChanged,
                myProgressDiag, &QProgressDialog::setValue);
        QObject::connect(lWatcher, &QFutureWatcher<PSSessionData*>::finished,
                myProgressDiag, &QProgressDialog::reset);
        QObject::connect(myProgressDiag, &QProgressDialog::canceled,
                lWatcher, &QFutureWatcher<PSSessionData*>::cancel);

        // Do the scanning in a separate thread with a future signal
        lScanner = new PSandPhotoScanner(mCollectionDir, 0);
        lWatcher->setFuture(lScanner->startScanParallel());

        // Run dialog in a locally blocking event loop
        myProgressDiag->exec();
        if(lWatcher->isCanceled()) {
            exit(1);
        }

        lWatcher->waitForFinished();
        lScanner->finishScanParallelForUninitSessions();

        QFutureWatcher<void>* lWatcher2 = new QFutureWatcher<void>();
        QObject::connect(lWatcher2, &QFutureWatcher<void>::progressRangeChanged,
                myProgressDiag, &QProgressDialog::setRange);
        QObject::connect(lWatcher2, &QFutureWatcher<void>::progressValueChanged,
                myProgressDiag, &QProgressDialog::setValue);
        QObject::connect(lWatcher2, &QFutureWatcher<void>::finished,
                myProgressDiag, &QProgressDialog::reset);
        QObject::connect(lWatcher2, &QFutureWatcher<void>::canceled,
                myProgressDiag, &QProgressDialog::cancel);

        lWatcher2->setFuture(lScanner->startSyncAndInitParallel());

        myProgressDiag->exec();
        if(lWatcher2->isCanceled()) {
            exit(1);
        }

        lWatcher2->waitForFinished();
        lScanner->finishSyncAndInitParallel();

        delete lWatcher;
        delete lWatcher2;
        delete myProgressDiag;
    } catch(const std::exception& e) {

        QMessageBox::critical(nullptr, "Error Scanning Files", "Error: failed to scan '" + mCollectionDir + "'.");

        // Something went wrong scanning the given file/folder
        qWarning("Error: failed to scan '%s'.", mCollectionDir.toLocal8Bit().data());
        qWarning("Exception: %s", e.what());
        exit(1);
    }

    // Setup the main GUI window
    addModelData(lScanner);
}
