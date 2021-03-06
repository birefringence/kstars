/*  Ekos
    Copyright (C) 2012 Jasem Mutlaq <mutlaqja@ikarustech.com>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
 */

#include "capture.h"
#include "Options.h"

#include <KMessageBox>
#include <KFileDialog>
#include <lilxml.h>

#include "indi/driverinfo.h"
#include "indi/indifilter.h"
#include "indi/clientmanager.h"
#include "fitsviewer/fitsviewer.h"
#include "fitsviewer/fitsview.h"
#include "kstars.h"
#include "ekosmanager.h"

#include "QProgressIndicator.h"

#include <basedevice.h>

namespace Ekos
{

SequenceJob::SequenceJob()
{
    statusStrings = QStringList() << i18n("Idle") << i18n("In progress") << i18n("Error") << i18n("Aborted") << i18n("Complete");
    status = JOB_IDLE;
    exposure=count=delay=frameType=targetFilter=-1;
    captureFilter=FITS_NONE;
    preview=false;
    showFITS=false;
    activeChip=NULL;
    activeCCD=NULL;
    activeFilter= NULL;
    statusCell = NULL;
    completed=0;
}

void SequenceJob::reset()
{
    // Reset to default values
    activeChip->setBatchMode(false);
    activeChip->setShowFITS(Options::showFITS());
}

void SequenceJob::resetStatus()
{
    status = JOB_IDLE;
    completed=0;
    exposeLeft=0;
    if (preview == false && statusCell)
        statusCell->setText(statusStrings[status]);
}

void SequenceJob::abort()
{
    status = JOB_ABORTED;
    if (preview == false && statusCell)
        statusCell->setText(statusStrings[status]);
    if (activeChip->canAbort())
        activeChip->abortExposure();
    activeChip->setBatchMode(false);
}

void SequenceJob::done()
{
    status = JOB_DONE;

    if (statusCell)
        statusCell->setText(statusStrings[status]);

}

void SequenceJob::prepareCapture()
{

    activeChip->setBatchMode(!preview);

    activeChip->setShowFITS(showFITS);

    activeCCD->setFITSDir(fitsDir);

    activeCCD->setISOMode(isoMode);

    activeCCD->setSeqPrefix(prefix);

    if (activeChip->isBatchMode())
        activeCCD->updateUploadSettings();

    if (targetFilter != -1 && activeFilter != NULL)
    {
        if (targetFilter == currentFilter)
            emit prepareComplete();
        else
            activeFilter->runCommand(INDI_SET_FILTER, &targetFilter);
    }
    else
        emit prepareComplete();

}

SequenceJob::CAPTUREResult SequenceJob::capture(bool isDark)
{

    //if (targetFilter != -1 && activeFilter != NULL)
        //activeFilter->runCommand(INDI_SET_FILTER, &targetFilter);

   if (activeChip->canSubframe() && activeChip->setFrame(x, y, w, h) == false)
   {
        status = JOB_ERROR;

        if (preview == false && statusCell)
            statusCell->setText(statusStrings[status]);

        return CAPTURE_FRAME_ERROR;

   }

    if (activeChip->canBin() && activeChip->setBinning(binX, binY) == false)
    {
        status = JOB_ERROR;

        if (preview == false && statusCell)
            statusCell->setText(statusStrings[status]);

        return CAPTURE_BIN_ERROR;
    }

    if (isDark)
    {
        activeChip->setFrameType(FRAME_DARK);
        activeChip->setCaptureMode(FITS_CALIBRATE);
    }
    else
    {
        activeChip->setFrameType(frameTypeName);
        activeChip->setCaptureMode(FITS_NORMAL);
        activeChip->setCaptureFilter(captureFilter);
    }

    // If filter is different that CCD, send the filter info
    if (activeFilter && activeFilter != activeCCD)
        activeCCD->setFilter(filter);

    status = JOB_BUSY;

    if (preview == false && statusCell)
        statusCell->setText(statusStrings[status]);

    exposeLeft = exposure;

    activeChip->capture(exposure);

    return CAPTURE_OK;

}

void SequenceJob::setTargetFilter(int pos, const QString & name)
{
    targetFilter = pos;
    filter    = name;
}

void SequenceJob::setFrameType(int type, const QString & name)
{
    frameType = type;
    frameTypeName = name;
}

double SequenceJob::getExposeLeft() const
{
    return exposeLeft;
}

void SequenceJob::setExposeLeft(double value)
{
    exposeLeft = value;
}

void SequenceJob::setPrefixSettings(const QString &prefix, bool typeEnabled, bool filterEnabled, bool exposureEnabled)
{
    rawPrefix               = prefix;
    typePrefixEnabled       = typeEnabled;
    filterPrefixEnabled     = filterEnabled;
    expPrefixEnabled        = exposureEnabled;
}

void SequenceJob::getPrefixSettings(QString &prefix, bool &typeEnabled, bool &filterEnabled, bool &exposureEnabled)
{
    prefix          = rawPrefix;
    typeEnabled     = typePrefixEnabled;
    filterEnabled   = filterPrefixEnabled;
    exposureEnabled = expPrefixEnabled;
}
int SequenceJob::getCurrentFilter() const
{
    return currentFilter;
}

void SequenceJob::setCurrentFilter(int value)
{
    currentFilter = value;

    if (currentFilter == targetFilter && (status == JOB_IDLE || status == JOB_ABORTED))
        emit prepareComplete();
}

Capture::Capture()
{
    setupUi(this);

    currentCCD = NULL;
    currentTelescope = NULL;
    currentFilter = NULL;

    filterSlot = NULL;
    filterName = NULL;
    activeJob  = NULL;

    targetChip = NULL;
    guideChip  = NULL;

    deviationDetected = false;
    spikeDetected     = false;

    isAutoGuiding   = false;
    guideDither     = false;
    isAutoFocus     = false;
    autoFocusStatus = false;

    mDirty          = false;
    jobUnderEdit    = false;
    currentFilterPosition   = -1;

    calibrationState = CALIBRATE_NONE;

    pi = new QProgressIndicator(this);

    progressLayout->addWidget(pi, 0, 4, 1, 1);

    seqLister		= new KDirLister();
    seqTimer = new QTimer(this);
    connect(startB, SIGNAL(clicked()), this, SLOT(startSequence()));
    connect(stopB, SIGNAL(clicked()), this, SLOT(stopSequence()));
    connect(seqTimer, SIGNAL(timeout()), this, SLOT(captureImage()));

    connect(CCDCaptureCombo, SIGNAL(activated(int)), this, SLOT(checkCCD(int)));

    connect(FilterCaptureCombo, SIGNAL(activated(int)), this, SLOT(checkFilter(int)));

    connect(displayCheck, SIGNAL(toggled(bool)), this, SLOT(checkPreview(bool)));

    connect(previewB, SIGNAL(clicked()), this, SLOT(captureOne()));

    connect( seqLister, SIGNAL(newItems (const KFileItemList & )), this, SLOT(checkSeqBoundary(const KFileItemList &)));

    connect(addToQueueB, SIGNAL(clicked()), this, SLOT(addJob()));
    connect(removeFromQueueB, SIGNAL(clicked()), this, SLOT(removeJob()));
    connect(queueUpB, SIGNAL(clicked()), this, SLOT(moveJobUp()));
    connect(queueDownB, SIGNAL(clicked()), this, SLOT(moveJobDown()));
    connect(selectFITSDirB, SIGNAL(clicked()), this, SLOT(saveFITSDirectory()));
    connect(queueSaveB, SIGNAL(clicked()), this, SLOT(saveSequenceQueue()));
    connect(queueSaveAsB, SIGNAL(clicked()), this, SLOT(saveSequenceQueueAs()));
    connect(queueLoadB, SIGNAL(clicked()), this, SLOT(loadSequenceQueue()));
    connect(resetB, SIGNAL(clicked()), this, SLOT(resetJobs()));
    connect(queueTable, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(editJob(QModelIndex)));
    connect(queueTable, SIGNAL(itemSelectionChanged()), this, SLOT(resetJobEdit()));

    addToQueueB->setIcon(KIcon("list-add"));
    removeFromQueueB->setIcon(KIcon("list-remove"));
    queueUpB->setIcon(KIcon("go-up"));
    queueDownB->setIcon(KIcon("go-down"));
    selectFITSDirB->setIcon(KIcon("document-open-folder"));
    queueLoadB->setIcon(KIcon("document-open"));
    queueSaveB->setIcon(KIcon("document-save"));
    queueSaveAsB->setIcon(KIcon("document-save-as"));
    resetB->setIcon(KIcon("system-reboot"));

    fitsDir->setText(Options::fitsDir());

    seqExpose = 0;
    seqTotalCount = 0;
    seqCurrentCount = 0;
    seqDelay = 0;
    useGuideHead = false;
    guideDither = false;

    foreach(QString filter, FITSViewer::filterTypes)
        filterCombo->addItem(filter);

    displayCheck->setEnabled(Options::showFITS());
    displayCheck->setChecked(Options::showFITS());
    guideDeviationCheck->setChecked(Options::enforceGuideDeviation());
    guideDeviation->setValue(Options::guideDeviation());
    autofocusCheck->setChecked(Options::enforceAutofocus());
    parkCheck->setChecked(Options::autoParkTelescope());
    //meridianCheck->setChecked(Options::autoMeridianFlip());
}

Capture::~Capture()
{
    qDeleteAll(jobs);
}

void Capture::addCCD(ISD::GDInterface *newCCD, bool isPrimaryCCD)
{
    ISD::CCD *ccd = static_cast<ISD::CCD *> (newCCD);

    CCDCaptureCombo->addItem(ccd->getDeviceName());

    CCDs.append(ccd);

    if (isPrimaryCCD)
    {
        checkCCD(CCDs.count()-1);
        CCDCaptureCombo->setCurrentIndex(CCDs.count()-1);
    }
    else
    {
        checkCCD(0);
        CCDCaptureCombo->setCurrentIndex(0);
    }
}

void Capture::addGuideHead(ISD::GDInterface *newCCD)
{
    QString guiderName = newCCD->getDeviceName() + QString(" Guider");

    if (CCDCaptureCombo->contains(guiderName) == false)
    {
        CCDCaptureCombo->addItem(guiderName);
        CCDs.append(static_cast<ISD::CCD *> (newCCD));
    }
}

void Capture::addFilter(ISD::GDInterface *newFilter)
{
    foreach(ISD::GDInterface *filter, Filters)
    {
        if (!strcmp(filter->getDeviceName(), newFilter->getDeviceName()))
            return;
    }

    FilterCaptureCombo->addItem(newFilter->getDeviceName());

    Filters.append(static_cast<ISD::Filter *>(newFilter));

    checkFilter(0);

    FilterCaptureCombo->setCurrentIndex(0);

}

void Capture::startSequence()
{
    if (darkSubCheck->isChecked())
    {
        KMessageBox::error(this, i18n("Auto dark subtract is not supported in batch mode."));
        return;
    }

    Options::setGuideDeviation(guideDeviation->value());
    Options::setEnforceGuideDeviation(guideDeviationCheck->isChecked());
    Options::setEnforceAutofocus(autofocusCheck->isChecked());
    //Options::setAutoMeridianFlip(meridianCheck->isChecked());
    Options::setAutoParkTelescope(parkCheck->isChecked());

    if (queueTable->rowCount() ==0)
        addJob();

    SequenceJob *first_job = NULL;

    foreach(SequenceJob *job, jobs)
    {
        if (job->getStatus() == SequenceJob::JOB_IDLE || job->getStatus() == SequenceJob::JOB_ABORTED)
        {
            first_job = job;
            break;
        }
    }

    if (first_job == NULL)
    {
        appendLogText(i18n("No pending jobs found. Please add a job to the sequence queue."));
        return;
    }

    deviationDetected = false;
    spikeDetected     = false;

    prepareJob(first_job);

}

void Capture::stopSequence()
{

    retries              = 0;
    seqTotalCount        = 0;
    seqCurrentCount      = 0;

    if (activeJob)
    {
        if (activeJob->getStatus() == SequenceJob::JOB_BUSY)
        {
            if (Options::playCCDAlarm())
                KStars::Instance()->ekosManager()->playError();
            activeJob->abort();
        }

        activeJob->reset();
    }

    secondsLabel->clear();
    currentCCD->disconnect(this);

    currentCCD->setFITSDir("");

    imgProgress->reset();
    imgProgress->setEnabled(false);

    fullImgCountOUT->setText(QString());
    currentImgCountOUT->setText(QString());
    exposeOUT->setText(QString());

    startB->setEnabled(true);
    stopB->setEnabled(false);

    if (displayCheck->isChecked())
        previewB->setEnabled(true);
    else
        previewB->setEnabled(false);

    pi->stopAnimation();
    seqTimer->stop();

}

void Capture::checkCCD(int ccdNum)
{
    if (ccdNum == -1)
        ccdNum = CCDCaptureCombo->currentIndex();

    foreach(ISD::CCD *ccd, CCDs)
        disconnect(ccd, SIGNAL(numberUpdated(INumberVectorProperty*)), this, SLOT(processCCDNumber(INumberVectorProperty*)));

    if (ccdNum <= CCDs.count())
    {
        int x,y,w,h;
        double min,max,step;
        int xstep=0, ystep=0;
        QString frameProp = QString("CCD_FRAME");

        // Check whether main camera or guide head only
        currentCCD = CCDs.at(ccdNum);

        if (CCDCaptureCombo->itemText(ccdNum).right(6) == QString("Guider"))
        {
            frameProp = QString("GUIDER_FRAME");
            useGuideHead = true;
            targetChip = currentCCD->getChip(ISD::CCDChip::GUIDE_CCD);
        }
        else
        {
            currentCCD = CCDs.at(ccdNum);
            targetChip = currentCCD->getChip(ISD::CCDChip::PRIMARY_CCD);
            useGuideHead = false;
        }

        frameWIN->setEnabled(targetChip->canSubframe());
        frameHIN->setEnabled(targetChip->canSubframe());
        frameXIN->setEnabled(targetChip->canSubframe());
        frameYIN->setEnabled(targetChip->canSubframe());

        binXIN->setEnabled(targetChip->canBin());
        binYIN->setEnabled(targetChip->canBin());

        if (targetChip->canBin())
        {
            int binx=1,biny=1;
            targetChip->getMaxBin(&binx, &biny);
            binXIN->setMaximum(binx);
            binYIN->setMaximum(biny);
            targetChip->getBinning(&binx, &biny);
            binXIN->setValue(binx);
            binYIN->setValue(biny);
        }
        else
        {
            binXIN->setValue(1);
            binYIN->setValue(1);
        }

        if (currentCCD->getMinMaxStep(frameProp, "WIDTH", &min, &max, &step))
        {
            if (step == 0)
                xstep = (int) max * 0.05;
            else
                xstep = step;

            frameWIN->setMinimum(min);
            frameWIN->setMaximum(max);
            frameWIN->setSingleStep(xstep);
        }

        if (currentCCD->getMinMaxStep(frameProp, "HEIGHT", &min, &max, &step))
        {
            if (step == 0)
                ystep = (int) max * 0.05;
            else
                ystep = step;

            frameHIN->setMinimum(min);
            frameHIN->setMaximum(max);
            frameHIN->setSingleStep(ystep);
        }

        if (currentCCD->getMinMaxStep(frameProp, "X", &min, &max, &step))
        {
            if (step == 0)
                step = xstep;

            frameXIN->setMinimum(min);
            frameXIN->setMaximum(max);
            frameXIN->setSingleStep(step);
        }

        if (currentCCD->getMinMaxStep(frameProp, "Y", &min, &max, &step))
        {
            if (step == 0)
                step = ystep;

            frameYIN->setMinimum(min);
            frameYIN->setMaximum(max);
            frameYIN->setSingleStep(step);
        }

        if (targetChip->getFrame(&x,&y,&w,&h))
        {
            frameXIN->setValue(x);
            frameYIN->setValue(y);
            frameWIN->setValue(w);
            frameHIN->setValue(h);
        }

        QStringList frameTypes = targetChip->getFrameTypes();

        frameTypeCombo->clear();

        if (frameTypes.isEmpty())
            frameTypeCombo->setEnabled(false);
        else
        {
            frameTypeCombo->setEnabled(true);
            frameTypeCombo->addItems(frameTypes);
            frameTypeCombo->setCurrentIndex(targetChip->getFrameType());
        }

        connect(currentCCD, SIGNAL(numberUpdated(INumberVectorProperty*)), this, SLOT(processCCDNumber(INumberVectorProperty*)), Qt::UniqueConnection);
    }
}

void Capture::processCCDNumber(INumberVectorProperty *nvp)
{
    if (currentCCD && currentCCD->getUploadMode() != ISD::CCD::UPLOAD_LOCAL)
        return;

    if (activeJob && activeJob->getStatus() == SequenceJob::JOB_BUSY &&
            activeJob->getExposeLeft() == 0 && nvp->s == IPS_OK &&
            (!strcmp(nvp->name, "CCD_EXPOSURE") || !strcmp(nvp->name, "GUIDER_EXPOSURE")))

    {
        newFITS(0);
    }
}

void Capture::syncFrameType(ISD::GDInterface *ccd)
{
    if (strcmp(ccd->getDeviceName(), CCDCaptureCombo->currentText().toLatin1()))
        return;

    ISD::CCDChip *tChip = NULL;
    tChip = (static_cast<ISD::CCD *> (ccd) )->getChip(ISD::CCDChip::PRIMARY_CCD);

    QStringList frameTypes = tChip->getFrameTypes();

    frameTypeCombo->clear();

    if (frameTypes.isEmpty())
        frameTypeCombo->setEnabled(false);
    else
    {
        frameTypeCombo->setEnabled(true);
        frameTypeCombo->addItems(frameTypes);
        frameTypeCombo->setCurrentIndex(tChip->getFrameType());
    }


}

void Capture::checkFilter(int filterNum)
{

    if (filterNum == -1)
        filterNum = FilterCaptureCombo->currentIndex();

    QStringList filterAlias = Options::filterAlias();

    if (filterNum <= Filters.count())
        currentFilter = Filters.at(filterNum);

    ITextVectorProperty *activeDevices = currentCCD->getBaseDevice()->getText("ACTIVE_DEVICES");
    if (activeDevices)
    {
        IText *activeFilter = IUFindText(activeDevices, "ACTIVE_FILTER");
        if (activeFilter && strcmp(activeFilter->text, currentFilter->getDeviceName()))
        {
            IUSaveText(activeFilter, currentFilter->getDeviceName());

            currentCCD->getDriverInfo()->getClientManager()->sendNewText(activeDevices);
        }
    }

    FilterPosCombo->clear();

    filterName   = currentFilter->getBaseDevice()->getText("FILTER_NAME");
    filterSlot = currentFilter->getBaseDevice()->getNumber("FILTER_SLOT");

    if (filterSlot == NULL)
    {
        KMessageBox::error(0, i18n("Unable to find FILTER_SLOT property in driver %1", currentFilter->getBaseDevice()->getDeviceName()));
        return;
    }

    for (int i=0; i < filterSlot->np[0].max; i++)
    {
        QString item;

        if (filterName != NULL && (i < filterName->ntp))
            item = filterName->tp[i].text;
        else if (i < filterAlias.count() && filterAlias[i].isEmpty() == false)
            item = filterAlias.at(i);
        else
            item = QString("Filter_%1").arg(i+1);

        FilterPosCombo->addItem(item);

    }

    FilterPosCombo->setCurrentIndex( (int) filterSlot->np[0].value-1);

    currentFilterPosition = (int) filterSlot->np[0].value;

    if (activeJob && (activeJob->getStatus() == SequenceJob::JOB_ABORTED || activeJob->getStatus() == SequenceJob::JOB_IDLE))
        activeJob->setCurrentFilter(currentFilterPosition);

}

void Capture::newFITS(IBLOB *bp)
{
    ISD::CCDChip *tChip = NULL;

    // If there is no active job, ignore
    if (activeJob == NULL)
        return;

    if (currentCCD->getUploadMode() != ISD::CCD::UPLOAD_LOCAL)
    {
        if (bp == NULL)
        {
            stopSequence();
            return;
        }

        if (!strcmp(bp->name, "CCD2"))
            tChip = currentCCD->getChip(ISD::CCDChip::GUIDE_CCD);
        else
            tChip = currentCCD->getChip(ISD::CCDChip::PRIMARY_CCD);

        if (tChip != targetChip)
            return;

        if (targetChip->getCaptureMode() == FITS_FOCUS || targetChip->getCaptureMode() == FITS_GUIDE)
            return;

        // If the FITS is not for our device, simply ignore
        if (QString(bp->bvp->device)  != currentCCD->getDeviceName() || (startB->isEnabled() && previewB->isEnabled()))
            return;

        if (calibrationState == CALIBRATE_START)
        {
            calibrationState = CALIBRATE_DONE;
            seqTimer->start(seqDelay);
            return;
        }

        if (darkSubCheck->isChecked() && calibrationState == CALIBRATE_DONE)
        {
            calibrationState = CALIBRATE_NONE;

            FITSView *calibrateImage = targetChip->getImage(FITS_CALIBRATE);
            FITSView *currentImage   = targetChip->getImage(FITS_NORMAL);

            FITSImage *image_data = NULL;

            if (currentImage)
                image_data = currentImage->getImageData();

            if (image_data && calibrateImage && currentImage)
                image_data->subtract(calibrateImage->getImageData()->getImageBuffer());
        }
    }

    secondsLabel->setText(i18n("Complete."));

    if (seqTotalCount <= 0)
    {
       jobs.removeOne(activeJob);
       delete (activeJob);
       activeJob = NULL;
       stopSequence();
       return;
    }

    seqCurrentCount++;
    activeJob->setCompleted(seqCurrentCount);
    imgProgress->setValue(seqCurrentCount);

    appendLogText(i18n("Received image %1 out of %2.", seqCurrentCount, seqTotalCount));

    currentImgCountOUT->setText( QString::number(seqCurrentCount));

    // if we're done
    if (seqCurrentCount == seqTotalCount)
    {
        activeJob->done();

        stopSequence();

        SequenceJob *next_job = NULL;

        foreach(SequenceJob *job, jobs)
        {
            if (job->getStatus() == SequenceJob::JOB_IDLE || job->getStatus() == SequenceJob::JOB_ABORTED)
            {
                next_job = job;
                break;
            }
        }

        if (next_job)
            prepareJob(next_job);
        else
        {
            if (Options::playCCDAlarm())
                    KStars::Instance()->ekosManager()->playOk();

            if (parkCheck->isChecked() && currentTelescope && currentTelescope->canPark())
            {
                appendLogText(i18n("Parking telescope..."));
                emit telescopeParking();
                currentTelescope->Park();
                return;
            }
        }

        //Resume guiding if it was suspended before
        if (isAutoGuiding && currentCCD->getChip(ISD::CCDChip::GUIDE_CCD) == guideChip)
            emit suspendGuiding(false);

        return;
    }

    isAutoFocus = (autofocusCheck->isEnabled() && autofocusCheck->isChecked());
    if (isAutoFocus)
        autoFocusStatus = false;

    if (isAutoGuiding && currentCCD->getChip(ISD::CCDChip::GUIDE_CCD) == guideChip)
        emit suspendGuiding(false);

    if (isAutoGuiding && guideDither)
    {
            secondsLabel->setText(i18n("Dithering..."));
            emit exposureComplete();
    }
    else if (isAutoFocus)
    {
        secondsLabel->setText(i18n("Focusing..."));
        emit checkFocus(HFRPixels->value());
    }
    else
        seqTimer->start(seqDelay);
}

void Capture::captureOne()
{
    addJob(true);

    prepareJob(jobs.last());
}

void Capture::captureImage()
{
    seqTimer->stop();
    bool isDark=false;
    SequenceJob::CAPTUREResult rc=SequenceJob::CAPTURE_OK;

    if (useGuideHead == false && darkSubCheck->isChecked() && calibrationState == CALIBRATE_NONE)
        isDark = true;


     rc = activeJob->capture(isDark);

     switch (rc)
     {
        case SequenceJob::CAPTURE_OK:
         if (isDark)
         {
            calibrationState = CALIBRATE_START;
            appendLogText(i18n("Capturing dark frame..."));
         }
         else
           appendLogText(i18n("Capturing image..."));
         break;

        case SequenceJob::CAPTURE_FRAME_ERROR:
            appendLogText(i18n("Failed to set sub frame."));
            stopSequence();
            break;

        case SequenceJob::CAPTURE_BIN_ERROR:
            appendLogText(i18n("Failed to set binning."));
            stopSequence();
            break;
     }


}

void Capture::resumeCapture()
{
    appendLogText(i18n("Dither complete."));

    if (isAutoFocus && autoFocusStatus == false)
    {
        secondsLabel->setText(i18n("Focusing..."));
        emit checkFocus(HFRPixels->value());
        return;
    }

    seqTimer->start(seqDelay);
}

/*******************************************************************************/
/* Update the prefix for the sequence of images to be captured                 */
/*******************************************************************************/
void Capture::updateSequencePrefix( const QString &newPrefix, const QString &dir)
{
    seqPrefix = newPrefix;

    seqLister->setNameFilter(QString("*.fits"));

    seqCount = 1;

    seqLister->openUrl(dir);

    checkSeqBoundary(seqLister->items());

}

/*******************************************************************************/
/* Determine the next file number sequence. That is, if we have file1.png      */
/* and file2.png, then the next sequence should be file3.png		       */
/*******************************************************************************/
void Capture::checkSeqBoundary(const KFileItemList & items)
{
    int newFileIndex;
    QString tempName;

    KFileItemList::const_iterator it = items.begin();
    const KFileItemList::const_iterator end = items.end();
    for ( ; it != end; ++it )
    {
        tempName = (*it).name();

        // find the prefix first
        if (tempName.startsWith(seqPrefix) == false || tempName.endsWith(".fits") == false)
            continue;

        if (seqPrefix.isEmpty() == false)
           tempName.remove(seqPrefix + '_');


        int usIndex = tempName.indexOf('_');

        if (usIndex == -1)
            usIndex = tempName.indexOf('.');

        tempName.remove(usIndex, tempName.size() - usIndex);

        bool indexOK = false;

        newFileIndex = tempName.toInt(&indexOK);


        if (indexOK && newFileIndex >= seqCount)
            seqCount = newFileIndex + 1;

        //qDebug() << "Now the tempName is " << tempName << " conversion is " << (indexOK ? "OK" : "Failed") << " and valu is " << newFileIndex
          //          << " and seqCount is " << seqCount << endl;
    }

    currentCCD->setSeqCount(seqCount);

}

void Capture::checkPreview(bool enable)
{
    if (enable && currentCCD->getUploadMode() == ISD::CCD::UPLOAD_LOCAL)
    {
        appendLogText(i18n("Cannot enable preview while CCD upload mode is set to local. Change upload mode to client or both and try again."));
        displayCheck->disconnect(this);
        displayCheck->setChecked(false);
        connect(displayCheck, SIGNAL(toggled(bool)), this, SLOT(checkPreview(bool)));
        return;
    }

    previewB->setEnabled(enable);
}

void Capture::appendLogText(const QString &text)
{

    logText.insert(0, i18nc("log entry; %1 is the date, %2 is the text", "%1 %2", QDateTime::currentDateTime().toString("yyyy-MM-ddThh:mm:ss"), text));

    emit newLog();
}

void Capture::clearLog()
{
    logText.clear();
    emit newLog();
}

void Capture::updateCaptureProgress(ISD::CCDChip * tChip, double value)
{

    if (targetChip != tChip || targetChip->getCaptureMode() != FITS_NORMAL)
        return;

    exposeOUT->setText(QString::number(value, 'd', 2));

    if (activeJob)
        activeJob->setExposeLeft(value);

    if (value == 0)
    {
        if (isAutoGuiding && currentCCD->getChip(ISD::CCDChip::GUIDE_CCD) == guideChip)
            emit suspendGuiding(true);

            secondsLabel->setText(i18n("Downloading..."));
    }
    // JM: Don't change to i18np, value is DOUBLE, not Integer.
    else if (value <= 1)
        secondsLabel->setText(i18n("second left"));
    else
        secondsLabel->setText(i18n("seconds left"));
}

void Capture::addJob(bool preview)
{
    SequenceJob *job = NULL;
    QString imagePrefix;

    if (preview == false && darkSubCheck->isChecked())
    {
        KMessageBox::error(this, i18n("Auto dark subtract is not supported in batch mode."));
        return;
    }

    if (jobUnderEdit)
        job = jobs.at(queueTable->currentRow());
    else
        job = new SequenceJob();

    if (job == NULL)
    {
        kWarning() << "Job is NULL!" << endl;
        return;
    }

    if (ISOCheck->isChecked())
        job->setISOMode(true);
    else
        job->setISOMode(false);

    job->setPreview(preview);

    job->setFITSDir(fitsDir->text());

    job->setShowFITS(displayCheck->isChecked());

    imagePrefix = prefixIN->text();

    constructPrefix(imagePrefix);

    job->setPrefixSettings(prefixIN->text(), frameTypeCheck->isChecked(), filterCheck->isChecked(), expDurationCheck->isChecked());
    job->setFrameType(frameTypeCombo->currentIndex(), frameTypeCombo->currentText());
    job->setPrefix(imagePrefix);

    if (filterSlot != NULL && currentFilter != NULL)
       job->setTargetFilter(FilterPosCombo->currentIndex()+1, FilterPosCombo->currentText());

    job->setExposure(exposureIN->value());

    job->setCount(countIN->value());

    job->setBin(binXIN->value(), binYIN->value());

    job->setDelay(delayIN->value() * 1000);		/* in ms */

    job->setActiveChip(targetChip);
    job->setActiveCCD(currentCCD);
    job->setActiveFilter(currentFilter);

    job->setFrame(frameXIN->value(), frameYIN->value(), frameWIN->value(), frameHIN->value());

    if (jobUnderEdit == false)
        jobs.append(job);

    // Nothing more to do if preview
    if (preview)
        return;

    int currentRow = 0;
    if (jobUnderEdit == false)
    {

        currentRow = queueTable->rowCount();
        queueTable->insertRow(currentRow);
    }
    else
        currentRow = queueTable->currentRow();

    QTableWidgetItem *status = jobUnderEdit ? queueTable->item(currentRow, 0) : new QTableWidgetItem();
    status->setText(job->getStatusString());
    status->setTextAlignment(Qt::AlignHCenter);
    status->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    job->setStatusCell(status);

    QTableWidgetItem *filter = jobUnderEdit ? queueTable->item(currentRow, 1) : new QTableWidgetItem();
    filter->setText("--");
    if (frameTypeCombo->currentText().compare("Bias", Qt::CaseInsensitive) &&
            frameTypeCombo->currentText().compare("Dark", Qt::CaseInsensitive) &&
            FilterPosCombo->count() > 0)
        filter->setText(FilterPosCombo->currentText());

    filter->setTextAlignment(Qt::AlignHCenter);
    filter->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QTableWidgetItem *type = jobUnderEdit ? queueTable->item(currentRow, 2) : new QTableWidgetItem();
    type->setText(frameTypeCombo->currentText());
    type->setTextAlignment(Qt::AlignHCenter);
    type->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QTableWidgetItem *bin = jobUnderEdit ? queueTable->item(currentRow, 3) : new QTableWidgetItem();
    bin->setText(QString("%1x%2").arg(binXIN->value()).arg(binYIN->value()));
    bin->setTextAlignment(Qt::AlignHCenter);
    bin->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QTableWidgetItem *exp = jobUnderEdit ? queueTable->item(currentRow, 4) : new QTableWidgetItem();
    exp->setText(QString::number(exposureIN->value()));
    exp->setTextAlignment(Qt::AlignHCenter);
    exp->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    QTableWidgetItem *count = jobUnderEdit ? queueTable->item(currentRow, 5) : new QTableWidgetItem();
    count->setText(QString::number(countIN->value()));
    count->setTextAlignment(Qt::AlignHCenter);
    count->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    if (jobUnderEdit == false)
    {
        queueTable->setItem(currentRow, 0, status);
        queueTable->setItem(currentRow, 1, filter);
        queueTable->setItem(currentRow, 2, type);
        queueTable->setItem(currentRow, 3, bin);
        queueTable->setItem(currentRow, 4, exp);
        queueTable->setItem(currentRow, 5, count);
    }

    removeFromQueueB->setEnabled(true);

    if (queueTable->rowCount() > 0)
    {
        queueSaveAsB->setEnabled(true);
        queueSaveB->setEnabled(true);
        resetB->setEnabled(true);
        mDirty = true;
    }

    if (queueTable->rowCount() > 1)
    {
        queueUpB->setEnabled(true);
        queueDownB->setEnabled(true);
    }

    if (jobUnderEdit)
    {
        jobUnderEdit = false;
        resetJobEdit();
        appendLogText(i18n("Job #%1 changes applied.", currentRow+1));
    }

}

void Capture::removeJob()
{
    int currentRow = queueTable->currentRow();

    if (currentRow < 0)
    {
        currentRow = queueTable->rowCount()-1;
        if (currentRow < 0)
            return;
    }

    queueTable->removeRow(currentRow);

    SequenceJob *job = jobs.at(currentRow);
    jobs.removeOne(job);
    delete (job);

    if (queueTable->rowCount() == 0)
        removeFromQueueB->setEnabled(false);

    if (queueTable->rowCount() == 1)
    {
        queueUpB->setEnabled(false);
        queueDownB->setEnabled(false);
    }

    for (int i=0; i < jobs.count(); i++)
        jobs.at(i)->setStatusCell(queueTable->item(i, 0));

    queueTable->selectRow(queueTable->currentRow());

    if (queueTable->rowCount() == 0)
    {
        queueSaveAsB->setEnabled(false);
        queueSaveB->setEnabled(false);
        resetB->setEnabled(false);
    }

    mDirty = true;


}

void Capture::moveJobUp()
{
    int currentRow = queueTable->currentRow();

    int columnCount = queueTable->columnCount();

    if (currentRow <= 0 || queueTable->rowCount() == 1)
        return;

    int destinationRow = currentRow - 1;

    for (int i=0; i < columnCount; i++)
    {
        QTableWidgetItem *downItem = queueTable->takeItem(currentRow, i);
        QTableWidgetItem *upItem   = queueTable->takeItem(destinationRow, i);

        queueTable->setItem(destinationRow, i, downItem);
        queueTable->setItem(currentRow, i, upItem);
    }

    SequenceJob *job = jobs.takeAt(currentRow);

    jobs.removeOne(job);
    jobs.insert(destinationRow, job);

    queueTable->selectRow(destinationRow);

    for (int i=0; i < jobs.count(); i++)
      jobs.at(i)->setStatusCell(queueTable->item(i, 0));


    mDirty = true;

}

void Capture::moveJobDown()
{
    int currentRow = queueTable->currentRow();

    int columnCount = queueTable->columnCount();

    if (currentRow < 0 || queueTable->rowCount() == 1 || (currentRow+1) == queueTable->rowCount() )
        return;

    int destinationRow = currentRow + 1;

    for (int i=0; i < columnCount; i++)
    {
        QTableWidgetItem *downItem = queueTable->takeItem(currentRow, i);
        QTableWidgetItem *upItem   = queueTable->takeItem(destinationRow, i);

        queueTable->setItem(destinationRow, i, downItem);
        queueTable->setItem(currentRow, i, upItem);
    }

    SequenceJob *job = jobs.takeAt(currentRow);

    jobs.removeOne(job);
    jobs.insert(destinationRow, job);

    queueTable->selectRow(destinationRow);

    for (int i=0; i < jobs.count(); i++)
        jobs.at(i)->setStatusCell(queueTable->item(i, 0));

    mDirty = true;

}

void Capture::prepareJob(SequenceJob *job)
{
    activeJob = job;

    if (currentFilterPosition > 0)
    {
        activeJob->setCurrentFilter(currentFilterPosition);

        if (currentFilterPosition != activeJob->getTargetFilter())
        {
            appendLogText(i18n("Changing filter to %1...", FilterPosCombo->itemText(activeJob->getTargetFilter()-1)));
            pi->startAnimation();
            previewB->setEnabled(false);
            startB->setEnabled(false);
            stopB->setEnabled(true);
        }
    }

    connect(activeJob, SIGNAL(prepareComplete()), this, SLOT(executeJob()));

    job->prepareCapture();
}

void Capture::executeJob()
{
    activeJob->disconnect(this);

    if (activeJob->isPreview())
        seqTotalCount = -1;
    else
        seqTotalCount = activeJob->getCount();

    seqDelay = activeJob->getDelay();

    seqCurrentCount = activeJob->getCompleted();

    if (activeJob->isPreview() == false)
    {
        fullImgCountOUT->setText( QString::number(seqTotalCount));
        currentImgCountOUT->setText(QString::number(seqCurrentCount));

        // set the progress info
        imgProgress->setEnabled(true);
        imgProgress->setMaximum(seqTotalCount);
        imgProgress->setValue(seqCurrentCount);

        if (currentCCD->getUploadMode() != ISD::CCD::UPLOAD_LOCAL)
            updateSequencePrefix(activeJob->getPrefix(), activeJob->getFITSDir());
    }

    // Update button status
    startB->setEnabled(false);
    stopB->setEnabled(true);
    previewB->setEnabled(false);

    pi->startAnimation();

    useGuideHead = (activeJob->getActiveChip()->getType() == ISD::CCDChip::PRIMARY_CCD) ? false : true;

    connect(currentCCD, SIGNAL(BLOBUpdated(IBLOB*)), this, SLOT(newFITS(IBLOB*)));
    connect(currentCCD, SIGNAL(newExposureValue(ISD::CCDChip*,double)), this, SLOT(updateCaptureProgress(ISD::CCDChip*,double)));

    captureImage();

}

void Capture::enableGuideLimits()
{
    guideDeviationCheck->setEnabled(true);
    guideDeviation->setEnabled(true);
}

void Capture::setGuideDeviation(double delta_ra, double delta_dec)
{
    if (guideDeviationCheck->isChecked() == false || activeJob == NULL)
        return;

    // We don't enforce limit on previews
    if (activeJob->isPreview() || activeJob->getExposeLeft() == 0)
        return;

    double deviation_rms = sqrt(delta_ra*delta_ra + delta_dec*delta_dec);

    QString deviationText = QString("%1").arg(deviation_rms, 0, 'g', 3);

    if (activeJob->getStatus() == SequenceJob::JOB_BUSY)
    {
        if (deviation_rms > guideDeviation->value())
        {
            // Ignore spikes ONCE
            if (spikeDetected == false)
            {
                spikeDetected = true;
                return;
            }

            spikeDetected = false;
            deviationDetected = true;
            appendLogText(i18n("Guiding deviation %1 exceeded limit value of %2 arcsecs, aborting exposure.", deviationText, guideDeviation->value()));
            stopSequence();
        }
        return;
    }

    if (activeJob->getStatus() == SequenceJob::JOB_ABORTED && deviationDetected)
    {
        if (deviation_rms <= guideDeviation->value())
        {
            deviationDetected = false;
            appendLogText(i18n("Guiding deviation %1 is now lower than limit value of %2 arcsecs, resuming exposure.", deviationText, guideDeviation->value()));
            startSequence();
            return;
        }
    }

}

void Capture::setGuideDither(bool enable)
{
    guideDither = enable;
}

void Capture::setAutoguiding(bool enable, bool isDithering)
{
    isAutoGuiding = enable;
    guideDither   = isDithering;
}

void Capture::updateAutofocusStatus(bool status)
{
    autoFocusStatus = status;

    if (status)
    {
        autofocusCheck->setEnabled(true);
        HFRPixels->setEnabled(true);
    }

    if (isAutoFocus && activeJob && activeJob->getStatus() == SequenceJob::JOB_BUSY)
    {
        if (status)
            seqTimer->start(seqDelay);
        else
        {
            appendLogText(i18n("Autofocus failed. Aborting exposure..."));
            secondsLabel->setText("");
            stopSequence();
        }
    }
}

void Capture::setTelescope(ISD::GDInterface *newTelescope)
{
    currentTelescope = static_cast<ISD::Telescope*> (newTelescope);

    connect(currentTelescope, SIGNAL(numberUpdated(INumberVectorProperty*)), this, SLOT(updateScopeCoords(INumberVectorProperty*)));

    syncTelescopeInfo();
}

void Capture::syncTelescopeInfo()
{
    if (currentCCD && currentTelescope && currentTelescope->isConnected())
    {
        parkCheck->setEnabled(currentTelescope->canPark());

        ITextVectorProperty *activeDevices = currentCCD->getBaseDevice()->getText("ACTIVE_DEVICES");
        if (activeDevices)
        {
            IText *activeTelescope = IUFindText(activeDevices, "ACTIVE_TELESCOPE");
            if (activeTelescope)
            {
                IUSaveText(activeTelescope, currentTelescope->getDeviceName());

                currentCCD->getDriverInfo()->getClientManager()->sendNewText(activeDevices);
            }
        }
    }
}

void Capture::saveFITSDirectory()
{
    QString dir = KFileDialog::getExistingDirectory(fitsDir->text());

    if (!dir.isEmpty())
        fitsDir->setText(dir);
}

void Capture::updateScopeCoords(INumberVectorProperty *coord)
{

    /* Meridian Flip TODO
     * 1. Calculate pier side (west or east) from coord + geographic coords to get Alt/Az
     * 2. Decide when to flip mount
     * 3. Stop guiding
     * 4. Flip mount
     * 5. Plate solve
     * 6. Swap DEC axis in guiding?
     * 7. Auto select guide start (RapidGuide?)
     * 8. Start guiding
     * 9. Resume exposure
    */

}

void Capture::loadSequenceQueue()
{
    KUrl fileURL = KFileDialog::getOpenUrl( KUrl(), "*.esq|Ekos Sequence Queue");
    if (fileURL.isEmpty())
        return;

    if (fileURL.isValid() == false)
    {
       QString message = i18n( "Invalid URL: %1", fileURL.path() );
       KMessageBox::sorry( 0, message, i18n( "Invalid URL" ) );
    }

    QFile sFile;
    sFile.setFileName(fileURL.path());

    if ( !sFile.open( QIODevice::ReadOnly))
    {
        QString message = i18n( "Unable to open file %1",  fileURL.path());
        KMessageBox::sorry( 0, message, i18n( "Could Not Open File" ) );
        return;
    }

    //QTextStream instream(&sFile);

    qDeleteAll(jobs);
    queueTable->clearContents();

    LilXML *xmlParser = newLilXML();
    char errmsg[MAXRBUF];
    XMLEle *root = NULL;
    XMLEle *ep;
    char c;

    while ( sFile.getChar(&c))
    {
        root = readXMLEle(xmlParser, c, errmsg);

        if (root)
        {
             for (ep = nextXMLEle(root, 1) ; ep != NULL ; ep = nextXMLEle(root, 0))
             {
                 if (!strcmp(tagXMLEle(ep), "Park"))
                 {
                     if (parkCheck->isEnabled() == false)
                         continue;

                     if (!strcmp(pcdataXMLEle(ep), "1"))
                         parkCheck->setChecked(true);
                     else
                         parkCheck->setChecked(false);

                 }
                 else
                 {
                     processJobInfo(ep);
                 }

             }
             delXMLEle(root);
        }
        else if (errmsg[0])
        {
            appendLogText(QString(errmsg));
            delLilXML(xmlParser);
            return;
        }
    }

    sequenceURL = fileURL;
    mDirty = false;
    delLilXML(xmlParser);

}

bool Capture::processJobInfo(XMLEle *root)
{

    XMLEle *ep;
    XMLEle *subEP;

    for (ep = nextXMLEle(root, 1) ; ep != NULL ; ep = nextXMLEle(root, 0))
    {
        if (!strcmp(tagXMLEle(ep), "Exposure"))
            exposureIN->setValue(atof(pcdataXMLEle(ep)));
        else if (!strcmp(tagXMLEle(ep), "Binning"))
        {
            subEP = findXMLEle(ep, "X");
            if (subEP)
                binXIN->setValue(atoi(pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "Y");
            if (subEP)
                binYIN->setValue(atoi(pcdataXMLEle(subEP)));
        }
        else if (!strcmp(tagXMLEle(ep), "Frame"))
        {
            subEP = findXMLEle(ep, "X");
            if (subEP)
                frameXIN->setValue(atoi(pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "Y");
            if (subEP)
                frameYIN->setValue(atoi(pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "W");
            if (subEP)
                frameWIN->setValue(atoi(pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "H");
            if (subEP)
                frameHIN->setValue(atoi(pcdataXMLEle(subEP)));
        }
        else if (!strcmp(tagXMLEle(ep), "Filter"))
        {
            FilterPosCombo->setCurrentIndex(atoi(pcdataXMLEle(ep))-1);
        }
        else if (!strcmp(tagXMLEle(ep), "Type"))
        {
            frameTypeCombo->setCurrentIndex(atoi(pcdataXMLEle(ep)));
        }
        else if (!strcmp(tagXMLEle(ep), "Prefix"))
        {
            subEP = findXMLEle(ep, "RawPrefix");
            if (subEP)
                prefixIN->setText(pcdataXMLEle(subEP));
            subEP = findXMLEle(ep, "TypeEnabled");
            if (subEP)
                frameTypeCheck->setChecked( !strcmp("1", pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "FilterEnabled");
            if (subEP)
                filterCheck->setChecked( !strcmp("1", pcdataXMLEle(subEP)));
            subEP = findXMLEle(ep, "ExpEnabled");
            if (subEP)
                expDurationCheck->setChecked( !strcmp("1", pcdataXMLEle(subEP)));
        }
        else if (!strcmp(tagXMLEle(ep), "Count"))
        {
            countIN->setValue(atoi(pcdataXMLEle(ep)));
        }
        else if (!strcmp(tagXMLEle(ep), "Delay"))
        {
            delayIN->setValue(atoi(pcdataXMLEle(ep)));
        }
        else if (!strcmp(tagXMLEle(ep), "FITSDirectory"))
        {
            fitsDir->setText(pcdataXMLEle(ep));
        }
        else if (!strcmp(tagXMLEle(ep), "ISOMode"))
        {
            ISOCheck->setChecked( !strcmp("1", pcdataXMLEle(ep)));
        }
        else if (!strcmp(tagXMLEle(ep), "ShowFITS"))
        {
            displayCheck->setChecked( !strcmp("1", pcdataXMLEle(ep)));
        }

    }

    addJob(false);

    return true;
}

void Capture::saveSequenceQueue()
{
    KUrl backupCurrent = sequenceURL;

    if (sequenceURL.path().contains("/tmp/"))
        sequenceURL.clear();

    // If no changes made, return.
    if( mDirty == false && !sequenceURL.isEmpty())
        return;

    if (sequenceURL.isEmpty())
    {
        sequenceURL = KFileDialog::getSaveUrl( KUrl(), "*.esq |Ekos Sequence Queue");
        // if user presses cancel
        if (sequenceURL.isEmpty())
        {
            sequenceURL = backupCurrent;
            return;
        }

        if (sequenceURL.path().contains('.') == 0)
            sequenceURL.setPath(sequenceURL.path() + ".esq");

        if (QFile::exists(sequenceURL.path()))
        {
            int r = KMessageBox::warningContinueCancel(0,
                        i18n( "A file named \"%1\" already exists. "
                              "Overwrite it?", sequenceURL.fileName() ),
                        i18n( "Overwrite File?" ),
                        KGuiItem(i18n( "&Overwrite" )) );
            if(r==KMessageBox::Cancel) return;
        }
    }

    if ( sequenceURL.isValid() )
    {
        if ( (saveSequenceQueue(sequenceURL.path())) == false)
        {
            KMessageBox::error(0, i18n("Failed to save sequence queue"), i18n("FITS Save"));
            return;
        }

        mDirty = false;

    } else
    {
        QString message = i18n( "Invalid URL: %1", sequenceURL.url() );
        KMessageBox::sorry( 0, message, i18n( "Invalid URL" ) );
    }

}

void Capture::saveSequenceQueueAs()
{
    sequenceURL.clear();
    saveSequenceQueue();
}

bool Capture::saveSequenceQueue(const QString &path)
{
    QFile file;
    QString rawPrefix;
    bool typeEnabled, filterEnabled, expEnabled;

    file.setFileName(path);

    if ( !file.open( QIODevice::WriteOnly))
    {
        QString message = i18n( "Unable to write to file %1",  path);
        KMessageBox::sorry( 0, message, i18n( "Could Not Open File" ) );
        return false;
    }

    QTextStream outstream(&file);

    outstream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
    outstream << "<SequenceQueue>" << endl;
    outstream << "<Park>" << (parkCheck->isChecked() ? 1 : 0) << "</Park>" << endl;
    foreach(SequenceJob *job, jobs)
    {
        job->getPrefixSettings(rawPrefix, typeEnabled, filterEnabled, expEnabled);

         outstream << "<Job>" << endl;

         outstream << "<Exposure>" << job->getExposure() << "</Exposure>" << endl;
         outstream << "<Binning>" << endl;
            outstream << "<X>"<< job->getXBin() << "</X>" << endl;
            outstream << "<Y>"<< job->getXBin() << "</Y>" << endl;
         outstream << "</Binning>" << endl;
         outstream << "<Frame>" << endl;
            outstream << "<X>" << job->getSubX() << "</X>" << endl;
            outstream << "<Y>" << job->getSubY() << "</Y>" << endl;
            outstream << "<W>" << job->getSubW() << "</W>" << endl;
            outstream << "<H>" << job->getSubH() << "</H>" << endl;
        outstream << "</Frame>" << endl;
        outstream << "<Filter>" << job->getTargetFilter() << "</Filter>" << endl;
        outstream << "<Type>" << job->getFrameType() << "</Type>" << endl;
        outstream << "<Prefix>" << endl;
            //outstream << "<CompletePrefix>" << job->getPrefix() << "</CompletePrefix>" << endl;
            outstream << "<RawPrefix>" << rawPrefix << "</RawPrefix>" << endl;
            outstream << "<TypeEnabled>" << (typeEnabled ? 1 : 0) << "</TypeEnabled>" << endl;
            outstream << "<FilterEnabled>" << (filterEnabled ? 1 : 0) << "</FilterEnabled>" << endl;
            outstream << "<ExpEnabled>" << (expEnabled ? 1 : 0) << "</ExpEnabled>" << endl;
        outstream << "</Prefix>" << endl;
        outstream << "<Count>" << job->getCount() << "</Count>" << endl;
        // ms to seconds
        outstream << "<Delay>" << job->getDelay()/1000 << "</Delay>" << endl;
        outstream << "<FITSDirectory>" << job->getFITSDir() << "</FITSDirectory>" << endl;
        outstream << "<ISOMode>" << (job->getISOMode() ? 1 : 0) << "</ISOMode>" << endl;
        outstream << "<ShowFITS>" << (job->isShowFITS() ? 1 : 0) << "</ShowFITS>" << endl;

        outstream << "</Job>" << endl;
    }

    outstream << "</SequenceQueue>" << endl;

    appendLogText(i18n("Sequence queue saved to %1", path));
    file.close();
   return true;
}

void Capture::resetJobs()
{
    if (KMessageBox::warningContinueCancel(NULL, i18n("Are you sure you want to reset status of all jobs?"),
                                           i18n("Reset job status"), KStandardGuiItem::cont(), KStandardGuiItem::cancel(),
                                           "reset_job_status_warning") !=KMessageBox::Continue)
        return;

    foreach(SequenceJob *job, jobs)
        job->resetStatus();

    stopSequence();    
}

void Capture::editJob(QModelIndex i)
{
    SequenceJob *job = jobs.at(i.row());
    if (job == NULL)
        return;
    QString rawPrefix;
    bool typeEnabled, filterEnabled, expEnabled;

     job->getPrefixSettings(rawPrefix, typeEnabled, filterEnabled, expEnabled);

   exposureIN->setValue(job->getExposure());
   binXIN->setValue(job->getXBin());
   binYIN->setValue(job->getYBin());
   frameXIN->setValue(job->getSubX());
   frameYIN->setValue(job->getSubY());
   frameWIN->setValue(job->getSubW());
   frameHIN->setValue(job->getSubH());
   FilterPosCombo->setCurrentIndex(job->getTargetFilter()-1);
   frameTypeCombo->setCurrentIndex(job->getFrameType());
   prefixIN->setText(rawPrefix);
   frameTypeCheck->setChecked(typeEnabled);
   filterCheck->setChecked(filterEnabled);
   expDurationCheck->setChecked(expEnabled);
   countIN->setValue(job->getCount());
   delayIN->setValue(job->getDelay()/1000);
   fitsDir->setText(job->getFITSDir());
   ISOCheck->setChecked(job->getISOMode());
   displayCheck->setChecked(job->isShowFITS());

   appendLogText(i18n("Editing job #%1...", i.row()+1));

   addToQueueB->setIcon(KIcon("svn-update"));

   jobUnderEdit = true;

}

void Capture::resetJobEdit()
{
   if (jobUnderEdit)
       appendLogText(i18n("Editing job canceled."));

   jobUnderEdit = false;
   addToQueueB->setIcon(KIcon("list-add"));
}

void Capture::constructPrefix(QString &imagePrefix)
{
    if (frameTypeCheck->isChecked())
    {
        if (imagePrefix.isEmpty() == false)
            imagePrefix += '_';

        imagePrefix += frameTypeCombo->currentText();
    }
    if (filterCheck->isChecked() && FilterPosCombo->currentText().isEmpty() == false &&
            frameTypeCombo->currentText().compare("Bias", Qt::CaseInsensitive) &&
                        frameTypeCombo->currentText().compare("Dark", Qt::CaseInsensitive))
    {
        if (imagePrefix.isEmpty() == false || frameTypeCheck->isChecked())
            imagePrefix += '_';

        imagePrefix += FilterPosCombo->currentText();
    }
    if (expDurationCheck->isChecked())
    {
        if (imagePrefix.isEmpty() == false || frameTypeCheck->isChecked())
            imagePrefix += '_';

        imagePrefix += QString::number(exposureIN->value(), 'd', 0) + QString("_secs");
    }
}

}

#include "capture.moc"
