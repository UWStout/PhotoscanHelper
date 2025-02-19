#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
using namespace std;

#include "PSSessionData.h"

#include <QSettings>
#include <QDebug>
#include <QDirIterator>

#include "PSProjectFileData.h"
#include "PSChunkData.h"
#include "PSModelData.h"
#include "DirLister.h"

// Helper to get a clean last-modified date-time
inline qint64 lastModified(const QFileInfo& file) {
    if (!file.exists()) { return 0; }
    return file.lastModified().toSecsSinceEpoch();
}

DEFINE_ENUM(Field, FIELDS_ENUM, PSSessionData)

// The number of fields shown for base and extended modes
const uchar PSSessionData::BASE_LENGTH = 6;
const uchar PSSessionData::EXTENDED_LENGTH = 11;

// Values to control how containers of PSSessionData objects are sorted
PSSessionData::Field PSSessionData::mSortBy = PSSessionData::F_PROJECT_ID;
void PSSessionData::setSortBy(PSSessionData::Field pNewSortBy) {
    mSortBy = pNewSortBy;
}

PSSessionData::Field PSSessionData::getSortBy() {
    return mSortBy;
}

// The next ID available that is guaranteed to be unique
uint64_t PSSessionData::mNextID = 1;

// File name filters
// Note: raw file extensions from https://en.wikipedia.org/wiki/Raw_image_format
const QStringList gPSProjectFileExtensions = { "*.psz", "*.psx" };
const QStringList gPSImageFileExtensions = {
    "*.jpg", "*.jpeg",
    "*.tif", "*.tiff",
    "*.pgm", "*.ppm",
    "*.png",
    "*.bmp",
    "*.exr"
};
const QStringList gPSMaskFileExtensions = {
    "*_mask.jpg", "*_mask.jpeg",
    "*_mask.tif", "*_mask.tiff",
    "*_mask.pgm", "*_mask.ppm",
    "*_mask.png",
    "*_mask.bmp",
    "*_mask.exr"
};
const QStringList gRawFileExtensions = {
    "*.3fr",
    "*.ari","*.arw",
    "*.bay",
    "*.crw","*.cr2","*.cr3",
    "*.cap",
    "*.data","*.dcs","*.dcr","*.dng",
    "*.drf",
    "*.eip","*.erf",
    "*.fff",
    "*.gpr",
    "*.iiq",
    "*.k25","*.kdc",
    "*.mdc","*.mef","*.mos","*.mrw",
    "*.nef","*.nrw",
    "*.obm","*.orf",
    "*.pef","*.ptx","*.pxn",
    "*.r3d","*.raf","*.raw","*.rwl","*.rw2","*.rwz",
    "*.sr2","*.srf","*.srw",
    "*.x3f"
};

QVector<PSSessionData*> PSSessionData::sNeedsApproval = QVector<PSSessionData*>();

// Images sub-folder names
const QString PSSessionData::sRawFolderName = "Raw";
const QString PSSessionData::sProcessedFolderName = "Processed";
const QString PSSessionData::sMasksFolderName = "Masks";

PSSessionData::PSSessionData(QDir pPSProjectFolder)
    : mSettings(pPSProjectFolder.absolutePath() + QDir::separator() + "psh_meta.ini"),
      mExposure(ExposureSettings::DEFAULT_EXPOSURE) {
    // Fill everything with default values
    mSessionFolder = pPSProjectFolder;
    mStatus = PSS_UNKNOWN;
    mRawFileCount = mProcessedFileCount = mMaskFileCount = -1;
    mBlockWritingOfSettings = false;

    mIsInitialized = false;
    mIsSynchronized = false;
    mExplicitlyIgnored = false;

    mName = "";
    mNotes = QStringList();

    // Update project state and synchronize
    examineProject();
}

PSSessionData::~PSSessionData() {}

bool PSSessionData::iniFileExists() {
    return QFileInfo(mSettings).exists();
}

void PSSessionData::examineProject() {
    // Preliminary examination of directory
    examineDirectory(mSessionFolder);

    // Is this an external session folder that needs to be fully examined (no INI file yet)
    if (!iniFileExists()) {
        mIsInitialized = false;
        sNeedsApproval.append(this);
    } else {
        // Read from INI file
        readGeneralSettings();
        readExposureSettings();

        // NOTE: image files lists will not be created until the first time
        //       their getters are called (to shorten loading time).
    }
}

void PSSessionData::convertToPSSession() {
    // Build folder names
    QDir lRawFolder = QDir(mSessionFolder.absolutePath() + QDir::separator() + sRawFolderName);
    QDir lProcessedFolder = QDir(mSessionFolder.absolutePath() + QDir::separator() + sProcessedFolderName);
    QDir lMasksFolder = QDir(mSessionFolder.absolutePath() + QDir::separator() + sMasksFolderName);

    convertToPSSession(lRawFolder, lProcessedFolder, lMasksFolder);
}

void PSSessionData::convertToPSSession(const QDir& pRawFolder, const QDir &pProcessedFolder, const QDir &pMasksFolder) {
    qDebug() << "converting to session";

    // Build folder names
    mRawFolder = pRawFolder;
    mProcessedFolder = pProcessedFolder;
    mMasksFolder = pMasksFolder;

    // Ensure the image folders exist and files are copied into them
    initImageDir(mMasksFolder, gPSMaskFileExtensions);
    initImageDir(mRawFolder, gRawFileExtensions);
    initImageDir(mProcessedFolder, gPSImageFileExtensions);

    // Try to guess some info from the folder name
    extractInfoFromFolderName(mSessionFolder.dirName());

    // Parse the PhotoScan XML file
    parseProjectXMLAndCache();

    // Ensure the image files lists are initialized and image counts are accurate
    mBlockWritingOfSettings = true;
    getRawFileList(); getProcessedFileList(); getMaskFileList();
    mBlockWritingOfSettings = false;

    // Update status and create initial INI file
    mIsInitialized = true;
    autoSetStatus();
    writeGeneralSettings();
}

void PSSessionData::setExplicitlyIgnored(bool pIgnore) {
    mExplicitlyIgnored = pIgnore;
    QSettings lSettings(mSettings, QSettings::IniFormat);
    lSettings.beginGroup("General");
    lSettings.setValue("ExplicitlyIgnored", mExplicitlyIgnored);
    lSettings.endGroup();
}

bool PSSessionData::getExplicitlyIgnored() {
    return mExplicitlyIgnored;
}

void PSSessionData::extractInfoFromFolderName(QString pFolderName) {
    // The original format: "[ID_as_integer] rest of folder name"
    QStringList parts = pFolderName.split(" ");
    if(parts.length() > 1) {
        // Get the ID
        setID(parts[0].toULongLong());

        // Build description
        parts.removeFirst();
        mName = parts.join(' ');
    } else {
        qWarning() << "Folder name not in correct format";
    }
}

int PSSessionData::compareTo(const PSSessionData* o) const {
    switch(mSortBy) {
        default:
        case F_PROJECT_FOLDER:
            return mSessionFolder.dirName().compare(o->mSessionFolder.dirName());

        case F_PROJECT_ID: return (mID - o->mID);
        case F_PROJECT_NAME: return getName().compare(o->getName());
        case F_PHOTO_DATE:
        {
            if(mDateTimeCaptured.isNull()) return -1;
            if(o->mDateTimeCaptured.isNull()) return 1;
            return o->mDateTimeCaptured.daysTo(mDateTimeCaptured);
        }
        case F_IMAGE_COUNT_REAL: return mProcessedFileList.length() - o->mProcessedFileList.length();

        case F_PROJECT_STATUS: return (int)(mStatus) - (int)(o->mStatus);
        case F_IMAGE_ALIGN_LEVEL: return describeImageAlignPhase().compare(o->describeImageAlignPhase());
        case F_DENSE_CLOUD_LEVEL: return (int)(getDenseCloudPhaseStatus()) - (int)(o->getDenseCloudPhaseStatus());
        case F_MODEL_GEN_LEVEL: return (int)(getModelFaceCount()) - (int)(o->getModelFaceCount());
        case F_TEXTURE_GEN_LEVEL: return describeTextureGenPhase().compare(o->describeTextureGenPhase());
    }
}

bool compareFileInfo(const QFileInfo& A, const QFileInfo& B) {
    return (A.fileName().compare(B.fileName()) < 0);
}

void PSSessionData::parseProjectXMLAndCache() {
    // Make sure there's something to parse first
    if(!hasProject()) { return; }

    // Parse the actual project (this is where the XML reading happens)
    PSProjectFileData* lPSProject = new PSProjectFileData(mPSProjectFile);

    // Extract the critical information from the PSProjectFileData structure
    // and cache that data locally in this object
    mChunkCount = static_cast<int>(lPSProject->getChunkCount());
    mActiveChunkIndex = static_cast<int>(lPSProject->getActiveChunkIndex());

    PSChunkData* lActiveChunk = lPSProject->getActiveChunk();

    mChunkImages = static_cast<int>(lActiveChunk->getImageCount());
    mChunkCameras = static_cast<int>(lActiveChunk->getCameraCount());

    mAlignmentLevelString = lActiveChunk->getImageAlignment_LevelString();
    mAlignmentFeatureLimit = static_cast<int>(lActiveChunk->getImageAlignment_featureLimit());
    mAlignmentTieLimit = static_cast<int>(lActiveChunk->getImageAlignment_tiePointLimit());

    mDenseCloudLevelString = lActiveChunk->getDenseCloud_levelString();
    mDenseCloudImagesUsed = lActiveChunk->getDenseCloud_imagesUsed();

    PSModelData* lModel = lActiveChunk->getModelData();
    if (lModel != nullptr) {
        mHasMesh = true;
        mMeshFaces = lModel->getFaceCount();
        mMeshVerts = lModel->getVertexCount();
    } else {
        mHasMesh = false;
        mMeshFaces = mMeshVerts = 0;
    }

    mTextureCount = lActiveChunk->getTextureGeneration_count();
    if (mTextureCount > 0) {
        mTextureWidth = lActiveChunk->getTextureGeneration_width();
        mTextureHeight = lActiveChunk->getTextureGeneration_height();
    } else {
        mTextureWidth = mTextureHeight = 0;
    }

    // Delete temporary data
    delete lPSProject;
}

bool PSSessionData::examineDirectory(QDir pDirToExamine) {
    // Sanity check
    if(!pDirToExamine.exists()) {
        return false;
    }

    // Clear old data
    mPSProjectFile = QFileInfo();
    mDateTimeCaptured = QDateTime();

    // Find the project file
    QFileInfoList lProjectFiles = pDirToExamine.entryInfoList(
                gPSProjectFileExtensions, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    if(!lProjectFiles.isEmpty()) {
        mPSProjectFile = lProjectFiles[0];
        if(lProjectFiles.length() > 1) {
            qDebug("Session Warning: More then one project file. Will use the first one found only! (%s)",
                   mPSProjectFile.fileName().toLocal8Bit().data());
        }
    }

    // Return success
    return true;
}

inline void PSSessionData::initImageDir(const QDir &pDir, const QStringList& pFilter) {
    // Check to see if directory exists
    if(!pDir.exists()) {
        // If not, try to create that directory
        QString lNewDir = pDir.absolutePath();
        if (!mSessionFolder.mkpath(lNewDir) || !pDir.exists()) {
            qDebug("Failed to create directory %s", lNewDir.toLocal8Bit().data());
        }
    }

    // Iterate through the all files that match the given filter in the session root dir
    QDirIterator lIt(mSessionFolder.absolutePath(), pFilter, QDir::Files);
    while (lIt.hasNext() && !lIt.next().isNull()) {
        if(lIt.fileName().isEmpty()) {
            continue;
        }

        // Move matched files into the given folder
        QString newName = pDir.absolutePath() + QDir::separator() + lIt.fileName();
        QFile::rename(lIt.filePath(), newName);
    }
}

void PSSessionData::autoSetStatus(bool pOverwriteCustom) {
    // Avoid overwriting a custom status unless explicity requested
    if (mStatus > PSS_TEXTURE_GEN_DONE && !pOverwriteCustom) {
        return;
    }

    // Start out unknown (which can represent some unusual combination of factors)
    mStatus = PSS_UNKNOWN;

    // Pick one of the PhotoScan phases
    if(describeImageAlignPhase() == "N/A") {
        mStatus = PSS_RAW_PROCESSING_DONE;
    } else if(describeDenseCloudPhase() == "N/A") {
        mStatus = PSS_ALIGNMENT_DONE;
    } else if(describeModelGenPhase() == "N/A") {
        mStatus = PSS_POINT_CLOUD_DONE;
    } else if(describeTextureGenPhase() == "N/A") {
        mStatus = PSS_MODEL_GEN_DONE;
    } else {
        mStatus = PSS_TEXTURE_GEN_DONE;
    }

    // Zero processed images + some raw images = unprocessed
    if(getProcessedImageCount() == 0 && getRawImageCount() != 0) {
        // If we have no project file then this makes sense
        if (mStatus == PSS_UNKNOWN) { mStatus = PSS_UNPROCESSSED; }

        // If we do have a project file (e.g. status got set to something above)
        // then something is weird (processed images are expected but missing)
        // so set the status back to unknown.
        else { mStatus = PSS_UNKNOWN; }
    }
}

void PSSessionData::setCustomStatus(int statusIndex) {
    int ordinal = PSS_TEXTURE_GEN_DONE + statusIndex;
    if(ordinal > PSS_TEXTURE_GEN_DONE && ordinal <= PSS_FINAL_APPROVAL) {
        mStatus = (Status)ordinal;
    } else {
        autoSetStatus();
    }
}

void PSSessionData::setID(uint64_t pID) {
    mID = pID;
    mNextID = pID + 1;
    if(mID >= mNextID) {
        mNextID = mID + 1;
    }
}

uint64_t PSSessionData::getID() const { return mID; }
uint64_t PSSessionData::getNextID() { return mNextID; }
void PSSessionData::addNotes(QString pNotes) { mNotes.append(pNotes); }
void PSSessionData::setName(QString pName) { mName = pName; }
void PSSessionData::setDescription(QString pDescription) { mDescription = pDescription; }
void PSSessionData::setDateTimeCaptured(QDateTime pDateTimeCaptured) {
    mDateTimeCaptured = pDateTimeCaptured;
}

QFileInfo PSSessionData::getPSProjectFile() const {
    return mPSProjectFile;
}

QDir PSSessionData::getSessionFolder() const {
    return mSessionFolder;
}

int PSSessionData::getRawImageCount() const { return mRawFileCount; }
int PSSessionData::getProcessedImageCount() const { return mProcessedFileCount; }
int PSSessionData::getMaskImageCount() const { return mMaskFileCount; }

const QFileInfoList& PSSessionData::getRawFileList(bool pForceRecheck) {
    if(pForceRecheck || (mRawFileList.length() != mRawFileCount)) {
        mRawFileList = mRawFolder.entryInfoList(gRawFileExtensions,
                           QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        if(mRawFileList.length() != mRawFileCount) {
            mRawFileCount = mRawFileList.length();
            writeGeneralSettings();
        }
    }
    return mRawFileList;
}

const QFileInfoList& PSSessionData::getProcessedFileList(bool pForceRecheck) {
    if(pForceRecheck || (mProcessedFileList.length() != mProcessedFileCount)) {
        mProcessedFileList = mProcessedFolder.entryInfoList(gPSImageFileExtensions,
                                 QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        if (mProcessedFileList.length() != mProcessedFileCount) {
            mProcessedFileCount = mProcessedFileList.length();
            writeGeneralSettings();
        }
    }
    return mProcessedFileList;
}

const QFileInfoList& PSSessionData::getMaskFileList(bool pForceRecheck) {
    if(pForceRecheck || (mMaskFileList.length() != mMaskFileCount)) {
        mMaskFileList = mMasksFolder.entryInfoList(gPSMaskFileExtensions,
                                 QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        if(mMaskFileList.length() != mMaskFileCount) {
            mMaskFileCount = mMaskFileList.length();
            writeGeneralSettings();
        }
    }
    return mMaskFileList;
}

QDateTime PSSessionData::getDateTimeCaptured() const { return mDateTimeCaptured; }
QStringList PSSessionData::getNotes() const { return mNotes; }
QString PSSessionData::getName() const { return mName; }
QString PSSessionData::getDescription() const { return mDescription; }
PSSessionData::Status PSSessionData::getStatus() const { return mStatus; }

void PSSessionData::initSettingsFile() {
}

void PSSessionData::writeGeneralSettings() {
    if (mBlockWritingOfSettings) { return; }

    QSettings lSettings(mSettings, QSettings::IniFormat);
    lSettings.beginGroup("General");
    if (lSettings.status() != QSettings::NoError) {
        qDebug("Error with session ini file.");
    }

    lSettings.setValue("ID", mID);

    // Write these only if they are not empty
    if(!mName.isEmpty()) { lSettings.setValue("Name", mName); }
    if(!mDescription.isEmpty()) { lSettings.setValue("Description", mDescription); }
    if(!mNotes.isEmpty()) {
        lSettings.beginWriteArray("Notes");
        for (int i = 0; i < mNotes.size(); i++) {
            lSettings.setArrayIndex(i);
            lSettings.setValue("note", mNotes[i]);
        }
        lSettings.endArray();
    }

    if(!mDateTimeCaptured.isNull()) {
        lSettings.setValue("DateTime", mDateTimeCaptured.toString());
    }

    // Only write custom status states
    lSettings.setValue("Status", static_cast<int>(mStatus));

    lSettings.setValue("ExplicitlyIgnored", mExplicitlyIgnored);
    lSettings.setValue("IsInitialized", mIsInitialized);
    lSettings.endGroup();

    // Store image counts so we can read them without scaning the image directories
    lSettings.beginGroup("Images");
    lSettings.setValue("RawImageCount", mRawFileCount);
    lSettings.setValue("ProcessedImageCount", mProcessedFileCount);
    lSettings.setValue("MaskImageCount", mMaskFileCount);
    lSettings.setValue("RawFolder", mSessionFolder.relativeFilePath(mRawFolder.absolutePath()));
    lSettings.setValue("ProcessedFolder", mSessionFolder.relativeFilePath(mProcessedFolder.absolutePath()));
    lSettings.setValue("MasksFolder", mSessionFolder.relativeFilePath(mMasksFolder.absolutePath()));
    lSettings.endGroup();

    if (hasProject()) {
        lSettings.beginGroup("ChunkData");
        lSettings.setValue("ChunkCount", mChunkCount);
        lSettings.setValue("ActiveChunkIndex", mActiveChunkIndex);
        lSettings.setValue("ChunkImages", mChunkImages);
        lSettings.setValue("ChunkCameras", mChunkCameras);
        lSettings.setValue("AlignmentLevelString", mAlignmentLevelString);
        lSettings.setValue("AlignmentFeatureLimit", mAlignmentFeatureLimit);
        lSettings.setValue("AlignmentTieLimit", mAlignmentTieLimit);
        lSettings.setValue("DenseCloudLevelString", mDenseCloudLevelString);
        lSettings.setValue("DenseCloudImagesUsed", mDenseCloudImagesUsed);
        lSettings.setValue("HasMesh", mHasMesh);
        lSettings.setValue("MeshFaces", mMeshFaces);
        lSettings.setValue("MeshVerts", mMeshVerts);
        lSettings.setValue("TextureCount", mTextureCount);
        lSettings.setValue("TextureWidth", mTextureWidth);
        lSettings.setValue("TextureHeight", mTextureHeight);
        lSettings.endGroup();
    } else {
        mChunkCount = mActiveChunkIndex = 0;
    }

    lSettings.beginGroup("Synchronization");
    if (hasProject()) {
        lSettings.setValue("ProjectFileName", mPSProjectFile.fileName());
        lSettings.setValue("ProjectFileTimestamp", lastModified(mPSProjectFile));
    } else {
        lSettings.remove("ProjectFileName");
        lSettings.remove("ProjectFileTimestamp");
    }

    // Write last modified for each image folder
    lSettings.setValue("RawTimestamp", lastModified(QFileInfo(mRawFolder, QString())));
    lSettings.setValue("ProcessedTimestamp", lastModified(QFileInfo(mProcessedFolder, QString())));
    lSettings.setValue("MasksTimestamp", lastModified(QFileInfo(mMasksFolder, QString())));
    lSettings.endGroup();

    // Update state
    mIsSynchronized = true;
}

void PSSessionData::readGeneralSettings() {
    QSettings lSettings(mSettings, QSettings::IniFormat);
    lSettings.beginGroup("General");

    // Is this an ignored folder
    mExplicitlyIgnored = lSettings.value("ExplicitlyIgnored", false).toBool();
    if (mExplicitlyIgnored) {
        lSettings.endGroup();
        return;
    }

    mIsInitialized = lSettings.value("IsInitialized", true).toBool();

    // Retrieve the general settings
    mID = lSettings.value("ID", mID).toULongLong();
    mName = lSettings.value("Name", mName).toString();
    mDescription = lSettings.value("Description", mDescription).toString();

    QStringList settingsNotes;
    int notesSize = lSettings.beginReadArray("Notes");
    for (int i = 0; i < notesSize; ++i) {
        lSettings.setArrayIndex(i);
        settingsNotes << lSettings.value("note").toString();
    }
    lSettings.endArray();

    mDateTimeCaptured = QDateTime::fromString(lSettings.value("DateTime").toString());

    // Only read these settings if they are not empty
    if(!settingsNotes.isEmpty()) { mNotes = settingsNotes; }

    // Read most recent status
    mStatus = static_cast<Status>(lSettings.value("Status", "0").toInt());
    lSettings.endGroup();

    // Read the image counts
    lSettings.beginGroup("Images");
    mRawFileCount = lSettings.value("RawImageCount", -1).toInt();
    mProcessedFileCount = lSettings.value("ProcessedImageCount", -1).toInt();
    mMaskFileCount = lSettings.value("MaskImageCount", -1).toInt();
    QString lRelRawFolderName = lSettings.value("RawFolder", sRawFolderName).toString();
    QString lRelProcessedFolderName = lSettings.value("ProcessedFolder", sProcessedFolderName).toString();
    QString lRelMasksFolderName = lSettings.value("MasksFolder", sMasksFolderName).toString();

    mRawFolder.setPath(mSessionFolder.absolutePath() + QDir::separator() + lRelRawFolderName);
    mProcessedFolder.setPath(mSessionFolder.absolutePath() + QDir::separator() + lRelProcessedFolderName);
    mMasksFolder.setPath(mSessionFolder.absolutePath() + QDir::separator() + lRelMasksFolderName);

    lSettings.endGroup();

    // Read the chunk data
    lSettings.beginGroup("ChunkData");
    mChunkCount = lSettings.value("ChunkCount", 0).toInt(); // May need to test the fallback value
    mActiveChunkIndex = lSettings.value("ActiveChunkIndex", 0).toInt(); // May need to test the fallback value
    mChunkImages = lSettings.value("ChunkImages", -1).toInt();
    mChunkCameras = lSettings.value("ChunkCameras", -1).toInt();
    mAlignmentLevelString = lSettings.value("AlignmentLevelString", "N/A").toString();
    mAlignmentFeatureLimit = lSettings.value("AlignmentFeatureLimit", 0).toInt();
    mAlignmentTieLimit = lSettings.value("AlignmentTieLimit", 0).toInt();
    mDenseCloudLevelString = lSettings.value("DenseCloudLevelString", "N/A").toString();
    mDenseCloudImagesUsed = lSettings.value("DenseCloudImagesUsed", 0).toInt();
    mHasMesh = lSettings.value("HasMesh", false).toBool();
    mMeshFaces = lSettings.value("MeshFaces", 0).toLongLong();
    mMeshVerts = lSettings.value("MeshVerts", 0).toLongLong();
    mTextureCount = lSettings.value("TextureCount", 0).toInt();
    mTextureWidth = lSettings.value("TextureWidth", 0).toInt();
    mTextureHeight = lSettings.value("TextureHeight", 0).toInt();
    lSettings.endGroup();

    // Read the synchronization
    lSettings.beginGroup("Synchronization");
    QString lLastProjFileName = lSettings.value("ProjectFileName", QString()).toString();
    QDateTime lProjFileTimestamp = QDateTime::fromSecsSinceEpoch(lSettings.value("ProjectFileTimestamp", 0).toLongLong(), Qt::TimeZone);
    // Read last modified for each image folder
    QDateTime lRawTimestamp = QDateTime::fromSecsSinceEpoch(lSettings.value("RawTimestamp", 0).toLongLong(), Qt::TimeZone);
    QDateTime lProcessedTimestamp = QDateTime::fromSecsSinceEpoch(lSettings.value("ProcessedTimestamp", 0).toLongLong(), Qt::TimeZone);
    QDateTime lMasksTimestamp = QDateTime::fromSecsSinceEpoch(lSettings.value("MasksTimestamp", 0).toLongLong(), Qt::TimeZone);
    lSettings.endGroup();

    // Check Synchronization
    checkSynchronization(lLastProjFileName, lProjFileTimestamp, lRawTimestamp, lProcessedTimestamp, lMasksTimestamp);

    // Update synchronization if needed
//    if(!mIsSynchronized) {
//        updateOutOfSyncSession();
//    }
}

void PSSessionData::checkSynchronization(QString pProjName, QDateTime pProjTime, QDateTime pRawTime, QDateTime pProcTime, QDateTime pMaskTime) {
    if(pProjName != mPSProjectFile.fileName()) {
        mIsSynchronized = false;
        qWarning() << "Project name is no longer synchronized";
    } else if (pProjTime.toSecsSinceEpoch() != lastModified(mPSProjectFile)) {
        mIsSynchronized = false;
        qWarning() << "Project QDateTime is no longer synchronized";
    } else if (pRawTime.toSecsSinceEpoch() != lastModified(QFileInfo(mRawFolder, QString()))) {
        mIsSynchronized = false;
        qWarning() << "RawFiles DateTime is no longer synchronized";
    } else if (pProcTime.toSecsSinceEpoch() != lastModified(QFileInfo(mProcessedFolder, QString()))) {
        mIsSynchronized = false;
        qWarning() << "ProcessedFiles DateTime is no longer synchronized";
    } else if (pMaskTime.toSecsSinceEpoch() != lastModified(QFileInfo(mMasksFolder, QString()))) {
        mIsSynchronized = false;
        qWarning() << "MasksFiles DateTime is no longer synchronized";
    } else {
        mIsSynchronized = true;
        qWarning() << "Files are synchronized";
    }
}

void PSSessionData::updateOutOfSyncSession() {
    // Parse the PhotoScan XML file
    parseProjectXMLAndCache();

    // Reset image counts
    mRawFileCount = mProcessedFileCount = mMaskFileCount = -1;

    // Ensure the image files lists are initialized and image counts are accurate
    mBlockWritingOfSettings = true;
    getRawFileList(); getProcessedFileList(); getMaskFileList();
    mBlockWritingOfSettings = false;

    writeGeneralSettings();
}

void PSSessionData::writeExposureSettings(ExposureSettings pExpSettings) {
    QSettings lSettings(mSettings, QSettings::IniFormat);

    // Write those to the settings file for the application
    lSettings.beginGroup("Exposure");

    lSettings.setValue("WhiteBalanceMode", static_cast<int>(pExpSettings.getWBMode()));
    lSettings.setValue("WhiteBalanceMode/R", pExpSettings.getWBCustom()[0]);
    lSettings.setValue("WhiteBalanceMode/G1", pExpSettings.getWBCustom()[1]);
    lSettings.setValue("WhiteBalanceMode/B", pExpSettings.getWBCustom()[2]);
    lSettings.setValue("WhiteBalanceMode/G2", pExpSettings.getWBCustom()[3]);

    lSettings.setValue("BrightnessMode", static_cast<int>(pExpSettings.getBrightMode()));
    lSettings.setValue("BrightnessMode/Scaler", pExpSettings.getBrightScale());

    // Close the group
    lSettings.endGroup();
}

ExposureSettings PSSessionData::readExposureSettings() const {
    ExposureSettings lDefSettings = ExposureSettings::DEFAULT_EXPOSURE;

    QSettings lSettings(mSettings, QSettings::IniFormat);
    lSettings.beginGroup("Exposure");

    int lWBOrdinal = lSettings.value("WhiteBalanceMode",
                                     static_cast<int>(lDefSettings.getWBMode())).toInt();
    double lWBCustom[4] = {
        lSettings.value("WhiteBalanceMode/R", lDefSettings.getWBCustom()[0]).toDouble(),
        lSettings.value("WhiteBalanceMode/G1", lDefSettings.getWBCustom()[1]).toDouble(),
        lSettings.value("WhiteBalanceMode/B", lDefSettings.getWBCustom()[2]).toDouble(),
        lSettings.value("WhiteBalanceMode/G2", lDefSettings.getWBCustom()[3]).toDouble()
    };

    int lBrightMOrdinal = lSettings.value("BrightnessMode", static_cast<int>(lDefSettings.getBrightMode())).toInt();
    double lBrightScaler = lSettings.value("BrightnessMode/Scaler", lDefSettings.getBrightScale()).toDouble();

    // Close the group and dispose of this object (cause you know, garbage collection)
    lSettings.endGroup();

    return ExposureSettings(
                static_cast<ExposureSettings::WhiteBalanceMode>(lWBOrdinal), lWBCustom,
                static_cast<ExposureSettings::BrightnessMode>(lBrightMOrdinal), lBrightScaler);
}

QString PSSessionData::describeImageAlignPhase() const {
    if (!hasProject()) { return "N/A"; }
    QString lData(mAlignmentLevelString);
    lData += " (" + QString::number(mChunkImages) + " - " +
            QString::number(mAlignmentFeatureLimit/1000) + "k/" +
            QString::number(mAlignmentTieLimit/1000) + "k)";
    return lData;
}

uchar PSSessionData::getAlignPhaseStatus() const {
    if(hasProject() && mChunkImages > 0 && mChunkCameras > 0) {
        double ratio = mChunkImages/static_cast<double>(mChunkCameras);
        if(ratio >= .95) { return 0; }
        if(ratio >= .6667) { return 1; }
        if(ratio >= .3333) { return 2; }
        if(ratio >= .1) { return 3; }
        return 4;
    }
    return 5;
}

QString PSSessionData::describeDenseCloudPhase() const {
    if (!hasProject()) { return "N/A"; }
    QString lData(mDenseCloudLevelString);
    lData += " (" + QString::number(mDenseCloudImagesUsed) + ")";
    return lData;
}

int PSSessionData::getDenseCloudDepthImages() const {
    if(!hasProject()) { return 0; }
    return mDenseCloudImagesUsed;
}

uchar PSSessionData::getDenseCloudPhaseStatus() const {
    if (hasProject() && mChunkCameras > 0 && mDenseCloudImagesUsed > 0) {
        double ratio = mDenseCloudImagesUsed/static_cast<double>(mChunkCameras);
        if(ratio >= .950) { return 0; }
        if(ratio < .6667) { return 1; }
        if(ratio < .3333) { return 2; }
        if(ratio < .100) { return 3; }
        return 4;
    }
    return 5;
}

QString PSSessionData::describeModelGenPhase() const {
    if(!hasProject() || !mHasMesh) { return "N/A"; }
    if(mMeshFaces >= 1000000) {
        return QString::asprintf("%.1fM faces", mMeshFaces/1000000.0);
    } else {
        return QString::asprintf("%.1fK faces", mMeshFaces/1000.0);
    }
}

uchar PSSessionData::getModelGenPhaseStatus() const {
    // Examine the model resolution
    if(!hasProject() || !mHasMesh || mMeshFaces == 0) {
        return 5;
    } else if(mMeshFaces < 5000) {
        return 4;
    } else if(mMeshFaces < 10000) {
        return 3;
    } else if(mMeshFaces < 50000) {
        return 2;
    } else if(mMeshFaces < 1000000) {
        return 1;
    } else {
        return 0;
    }
}

long long PSSessionData::getModelFaceCount() const {
    if(!hasProject() || !mHasMesh) return -1L;
    return mMeshFaces;
}

long long PSSessionData::getModelVertexCount() const {
    if(!hasProject() || !mHasMesh) return -1L;
    return mMeshVerts;
}

QString PSSessionData::describeTextureGenPhase() const {
    if(!hasProject() || mTextureCount == 0) { return "N/A"; }
    return QString::asprintf("%d @ (%d x %d)", mTextureCount, mTextureWidth, mTextureHeight);
}

uchar PSSessionData::getTextureGenPhaseStatus() const {
    // Examine the texture resolution
    if(!hasProject() || mTextureWidth == 0 || mTextureHeight == 0) {
        return 5;
    } else if(mTextureWidth < 1024 || mTextureHeight < 1024) {
        return 4;
    } else if(mTextureWidth < 2048 || mTextureHeight < 2048) {
        return 3;
    } else if(mTextureWidth < 3072 || mTextureHeight < 3072) {
        return 2;
    } else if(mTextureWidth < 4096 || mTextureHeight < 4069) {
        return 1;
    } else {
        return 0;
    }
}

int PSSessionData::getActiveChunkIndex() const {
    return mActiveChunkIndex;
}

int PSSessionData::getChunkCount() const {
    return mChunkCount;
}

QVector<PSSessionData*> PSSessionData::getNeedsApproval() {
    return sNeedsApproval;
}

void PSSessionData::clearNeedsApproval() {
    sNeedsApproval.clear();
}
