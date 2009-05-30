/***************************************************************************
                          telescopeprop.cpp  -  description
                             -------------------
    begin                : Wed June 8th 2005
    copyright            : (C) 2005 by Jasem Mutlaq
    email                : mutlaqja@ikarustech.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "telescopeprop.h"

#include <kpushbutton.h>
#include <kcombobox.h>
#include <klineedit.h>
#include <kmessagebox.h>
#include <kstandarddirs.h>

#include <QListWidget>

#include <vector>

#include "kstars.h"
#include "indimenu.h"
#include "indidriver.h"

telescopeProp::telescopeProp(QWidget* parent, const char* /*name*/, bool modal, Qt::WFlags /*fl*/)
        : QDialog(parent)
{

    ksw = (KStars *) parent;

    ksw->establishINDI();
    indi_driver = ksw->indiDriver();
    newScopePending = false;

    ui = new Ui::scopeProp();
    ui->setupUi(this);

    setModal(modal);

    connect (ui->newB, SIGNAL(clicked()), this, SLOT(newScope()));
    connect (ui->saveB, SIGNAL(clicked()), this, SLOT(saveScope()));
    connect (ui->removeB, SIGNAL(clicked()), this, SLOT(removeScope()));
    connect (ui->telescopeListBox, SIGNAL(currentRowChanged(int)),this, SLOT(updateScopeDetails(int)));
    connect(ui->closeB, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->restoreB, SIGNAL(clicked()), this, SLOT(restoreDefault()));

    // Fill the combo box with drivers
    ui->driverCombo->addItems(indi_driver->driversList);

    // Fill the list box with telescopes
    //for (unsigned int i=0; i < indi_driver->devices.size(); i++)
    foreach (IDevice *dev, indi_driver->devices)
    {
        if (dev->deviceType == KSTARS_TELESCOPE && dev->primary_xml)
            ui->telescopeListBox->addItem(dev->tree_label);
    }

    ui->telescopeListBox->setCurrentRow(0);
    updateScopeDetails(0);


}

telescopeProp::~telescopeProp()
{
}

void telescopeProp::newScope()
{

    ui->driverCombo->clearEditText();
    ui->labelEdit->clear();
    ui->focalEdit->clear();
    ui->versionEdit->clear();
    ui->apertureEdit->clear();

    ui->telescopeListBox->clearFocus();
    ui->driverCombo->setFocus();

    /* FIXME This call causes KStars to crash under Qt 4.1
      Possible bug?? 
      JM: This is a bug in Qt 4.1, have to wait for Qt 4.2 for a fix */
    ui->telescopeListBox->clearSelection();

    newScopePending = true;

}

void telescopeProp::saveScope()
{
    IDevice *dev (NULL);
    double focal_length(-1), aperture(-1);
    int finalIndex = -1;
    bool is_ok=false;

    if (ui->labelEdit->text().isEmpty())
    {
        KMessageBox::error(NULL, i18n("Telescope label is missing."));
        return;
    }

    if (ui->driverCombo->currentText().isEmpty())
    {
        KMessageBox::error(NULL, i18n("Telescope driver is missing."));
        return;
    }

    if (ui->versionEdit->text().isEmpty())
    {
        KMessageBox::error(NULL, i18n("Telescope driver version is missing."));
        return;
    }

    if (ui->telescopeListBox->currentRow() != -1)
        finalIndex = findDeviceIndex(ui->telescopeListBox->currentRow());

     // FIXME add GUI warnings for negative values after KDE 4.3 is released
     focal_length = ui->focalEdit->text().toDouble(&is_ok);
     if (is_ok == false)
	   focal_length = -1;
     aperture = ui->apertureEdit->text().toDouble(&is_ok);
     if (is_ok == false)
	   aperture = -1;

    // Add new scope
    if (newScopePending)
    {

        dev = new IDevice(ui->labelEdit->text(), ui->labelEdit->text(), ui->driverCombo->currentText(), ui->versionEdit->text());

        dev->deviceType  = KSTARS_TELESCOPE;
	dev->primary_xml = true;

         dev->focal_length = focal_length;
         dev->aperture = aperture;

        indi_driver->devices.append(dev);

        ui->telescopeListBox->addItem(ui->labelEdit->text());

        ui->telescopeListBox->setCurrentRow(ui->telescopeListBox->count() - 1);

    }
    else
    {
        if (finalIndex == -1) return;
        indi_driver->devices[finalIndex]->tree_label  	= ui->labelEdit->text();
        indi_driver->devices[finalIndex]->version 	= ui->versionEdit->text();
        indi_driver->devices[finalIndex]->driver 	= ui->driverCombo->currentText();

        indi_driver->devices[finalIndex]->focal_length = focal_length;
        indi_driver->devices[finalIndex]->aperture = aperture;
    }

    indi_driver->saveDevicesToDisk();

    newScopePending = false;

    ui->driverCombo->clearFocus();
    ui->labelEdit->clearFocus();
    ui->focalEdit->clearFocus();
    ui->apertureEdit->clearFocus();

    KMessageBox::information(NULL, i18n("You need to restart KStars for changes to take effect."));

}

int telescopeProp::findDeviceIndex(int listIndex)
{
    int finalIndex = -1;

    for (int i=0; i < indi_driver->devices.count(); i++)
    {
        if (indi_driver->devices[i]->tree_label == ui->telescopeListBox->item(listIndex)->text())
        {
            finalIndex = i;
            break;
        }
    }

    return finalIndex;

}

void telescopeProp::updateScopeDetails(int index)
{

    int finalIndex = -1;
    newScopePending = false;
    bool foundFlag(false);

    ui->focalEdit->clear();
    ui->apertureEdit->clear();


    finalIndex = findDeviceIndex(index);
    if (finalIndex == -1)
    {
        kDebug() << "final index is invalid. internal error.";
        return;
    }

    for (int i=0; i < ui->driverCombo->count(); i++)
        if (indi_driver->devices[finalIndex]->driver == ui->driverCombo->itemText(i))
        {
            ui->driverCombo->setCurrentIndex(i);
            foundFlag = true;
            break;
        }

    if (foundFlag == false)
        ui->driverCombo->setEditText(indi_driver->devices[finalIndex]->driver);

    ui->labelEdit->setText(indi_driver->devices[finalIndex]->tree_label);

    ui->versionEdit->setText(indi_driver->devices[finalIndex]->version);

    if (indi_driver->devices[finalIndex]->focal_length != -1)
        ui->focalEdit->setText(QString::number(indi_driver->devices[finalIndex]->focal_length));

    if (indi_driver->devices[finalIndex]->aperture != -1)
        ui->apertureEdit->setText(QString::number(indi_driver->devices[finalIndex]->aperture));

}

void telescopeProp::removeScope()
{

    int index, finalIndex;

    index = ui->telescopeListBox->currentRow();
    finalIndex = findDeviceIndex(index);

    if (KMessageBox::warningContinueCancel( 0, i18n("Are you sure you want to remove %1?", indi_driver->devices[finalIndex]->tree_label), i18n("Delete Confirmation"),KStandardGuiItem::del())!=KMessageBox::Continue)
        return;

    ui->telescopeListBox->takeItem(index);

    delete (indi_driver->devices.takeAt(finalIndex));
    //indi_driver->devices.erase(indi_driver->devices.begin() + finalIndex);

    indi_driver->saveDevicesToDisk();

}

void telescopeProp::restoreDefault()
{
	QString driver_fullpath;
	QFile driver_file;

	driver_fullpath = KStandardDirs::locateLocal( "appdata", "drivers.xml");

	/* FIXME add more appropiate warning & confirmation dialogs after 4.3 is released */
	if (driver_fullpath.isEmpty())
		return;

	driver_file.setFileName(driver_fullpath);

	if (driver_file.remove())
	        KMessageBox::information(NULL, i18n("You need to restart KStars for changes to take effect."));
}

#include "telescopeprop.moc"

