/***************************************************************************
                          kspopupmenu.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Sat Feb 27 2003
    copyright            : (C) 2001 by Jason Harris
    email                : jharris@30doradus.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qlabel.h>

#include "kspopupmenu.h"
#include "kstars.h"
#include "kstarsdata.h"
#include "starobject.h"
#include "ksmoon.h"
#include "skyobject.h"
#include "ksplanetbase.h"
#include "skymap.h"

#include "indimenu.h"
#include "devicemanager.h"
#include "indidevice.h"
#include "indigroup.h"
#include "indiproperty.h"

KSPopupMenu::KSPopupMenu( QWidget *parent )
 : KMenu( parent )
{
	ksw = ( KStars* )parent;
}

KSPopupMenu::~KSPopupMenu()
{
}

void KSPopupMenu::createEmptyMenu( SkyObject *nullObj ) {
	initPopupMenu( nullObj, i18n( "Empty sky" ), QString(), QString(), true, true, false, false, false, true, false );

	insertItem( i18n( "First Generation Digitized Sky Survey", "Show 1st-Gen DSS Image" ), ksw->map(), SLOT( slotDSS() ) );
	insertItem( i18n( "Second Generation Digitized Sky Survey", "Show 2nd-Gen DSS Image" ), ksw->map(), SLOT( slotDSS2() ) );
}

void KSPopupMenu::createStarMenu( StarObject *star ) {
	//Add name, rise/set time, center/track, and detail-window items
	initPopupMenu( star, star->translatedLongName(), i18n( "Spectral type: %1" ).arg(star->sptype()),
		i18n( "star" ) );

//If the star is named, add custom items to popup menu based on object's ImageList and InfoList
	if ( star->name() != "star" ) {
		addLinksToMenu( star );
	} else {
		insertItem( i18n( "First Generation Digitized Sky Survey", "Show 1st-Gen DSS Image" ), ksw->map(), SLOT( slotDSS() ) );
		insertItem( i18n( "Second Generation Digitized Sky Survey", "Show 2nd-Gen DSS Image" ), ksw->map(), SLOT( slotDSS2() ) );
	}
}

void KSPopupMenu::createDeepSkyObjectMenu( SkyObject *obj ) {
	QString TypeName = ksw->data()->typeName( obj->type() );
	QString secondName = obj->translatedName2();
	if ( obj->longname() != obj->name() ) secondName = obj->translatedLongName();

	initPopupMenu( obj, obj->translatedName(), secondName, TypeName );
	addLinksToMenu( obj );
}

void KSPopupMenu::createCustomObjectMenu( SkyObject *obj ) {
	QString TypeName = ksw->data()->typeName( obj->type() );
	QString secondName = obj->translatedName2();
	if ( obj->longname() != obj->name() ) secondName = obj->translatedLongName();

	initPopupMenu( obj, obj->translatedName(), secondName, TypeName );

	addLinksToMenu( obj, true, false ); //don't allow user to add more links (temporary)
}

void KSPopupMenu::createPlanetMenu( SkyObject *p ) {
	bool addTrail( ! ((KSPlanetBase*)p)->hasTrail() );
	QString oname;
	if ( p->name() == "Moon" ) {
		oname = ((KSMoon *)p)->phaseName();
	}
	initPopupMenu( p, p->translatedName(), oname, i18n("Solar System"), true, true, true, true, addTrail );
	addLinksToMenu( p, false ); //don't offer DSS images for planets
}

void KSPopupMenu::addLinksToMenu( SkyObject *obj, bool showDSS, bool allowCustom ) {
	QString sURL;
	QStringList::Iterator itList, itTitle, itListEnd;

	itList  = obj->ImageList.begin();
	itTitle = obj->ImageTitle.begin();
	itListEnd = obj->ImageList.end();

	int id = 100;
	for ( ; itList != itListEnd; ++itList ) {
		QString t = QString(*itTitle);
		sURL = QString(*itList);
		insertItem( i18n( "Image/info menu item (should be translated)", t.toLocal8Bit() ), ksw->map(), SLOT( slotImage( int ) ), 0, id++ );
		++itTitle;
	}

	if ( showDSS ) {
	  insertItem( i18n( "First Generation Digitized Sky Survey", "Show 1st-Gen DSS Image" ), ksw->map(), SLOT( slotDSS() ) );
	  insertItem( i18n( "Second Generation Digitized Sky Survey", "Show 2nd-Gen DSS Image" ), ksw->map(), SLOT( slotDSS2() ) );
	  insertSeparator();
	}
	else if ( obj->ImageList.count() ) insertSeparator();

	itList  = obj->InfoList.begin();
	itTitle = obj->InfoTitle.begin();
	itListEnd = obj->InfoList.end();

	id = 200;
	for ( ; itList != itListEnd; ++itList ) {
		QString t = QString(*itTitle);
		sURL = QString(*itList);
		insertItem( i18n( "Image/info menu item (should be translated)", t.toLocal8Bit() ), ksw->map(), SLOT( slotInfo( int ) ), 0, id++ );
		++itTitle;
	}

	if ( allowCustom ) {
		insertSeparator();
		insertItem( i18n( "Add Link..." ), ksw->map(), SLOT( addLink() ), 0, id++ );
	}
}

bool KSPopupMenu::addINDI(void)
{
	INDIMenu *indiMenu = ksw->getINDIMenu();
	DeviceManager *mgr;
	INDI_D *dev;
	INDI_G *grp;
	INDI_P *prop(NULL);
	INDI_E *element;
	int id=0;
	
	if (indiMenu->mgr.count() == 0)
		return false;
	
	foreach ( mgr, indiMenu->mgr )
	{
		foreach (dev, mgr->indi_dev )
		{
			if (!dev->INDIStdSupport)
				continue;

			KMenu *menuDevice = new KMenu();
			insertItem(dev->label, menuDevice);

			foreach (grp, dev->gl )
			{
				foreach (prop, grp->pl )
				{
					//Only std are allowed to show. Movement is somewhat problematic 
					//due to an issue with the LX200 telescopes (the telescope does 
					//not update RA/DEC while moving N/W/E/S) so it's better off the 
					//skymap. It's avaiable in the INDI control panel nonetheless.
					//CCD_EXPOSE_DURATION is an INumber property, but it's so common 
					//that it's better to include in the context menu

					if (prop->stdID == -1 || prop->stdID == MOVEMENT) continue;
					// Only switches are shown
					if (prop->guitype != PG_BUTTONS && prop->guitype != PG_RADIO 
							&& prop->stdID !=CCD_EXPOSE_DURATION) continue;

					menuDevice->insertSeparator();

					prop->assosiatedPopup = menuDevice;

					if (prop->stdID == CCD_EXPOSE_DURATION)
					{
						menuDevice->insertItem (prop->label, id);
						menuDevice->setItemChecked(id, false);
						//kDebug() << "Expose ID: " << id << endl;
						id++;
					}
					else
					{
						foreach ( element, prop->el )
						{
							menuDevice->insertItem (element->label, id++);
							if (element->state == PS_ON)
							{
								// Slew, Track, Sync, Exppse are never checked in the skymap
								if ( (element->name != "SLEW") && (element->name != "TRACK") &&
										(element->name != "SYNC") )
									menuDevice->setItemChecked(id, true);
								else
									menuDevice->setItemChecked(id, false);
							}
							else
								menuDevice->setItemChecked(id, false);
						}
					}

					QObject::connect(menuDevice, SIGNAL(activated(int)), prop, SLOT (convertSwitch(int)));

				} // end property
			} // end group

			// For telescopes, add option to center the telescope position
			if ( dev->findElem("RA") || dev->findElem("ALT"))
			{
				menuDevice->insertSeparator();
				menuDevice->insertItem(i18n("Center && Track Crosshair"), id++);
				if (dev->findElem("RA"))
					prop = dev->findElem("RA")->pp;
				else
					prop = dev->findElem("ALT")->pp;

				prop->assosiatedPopup = menuDevice;
				QObject::connect(menuDevice, SIGNAL(activated(int)), prop, SLOT(convertSwitch(int)));	
			}
		} // end device
	} // end device manager

	return true;
}

void KSPopupMenu::initPopupMenu( SkyObject *obj, const QString &_s1, const QString &s2, const QString &s3,
		bool showRiseSet, bool showCenterTrack, bool showDetails, bool showTrail, bool addTrail, 
		bool showAngularDistance, bool showObsList ) {
	
	clear();
	QString s1 = _s1;
	if ( s1.isEmpty() ) s1 = i18n( "Empty sky" );

	bool showLabel( true );
	if ( s1 == i18n( "star" ) || s1 == i18n( "Empty sky" ) ) showLabel = false;

	aName = addTitle( s1 );
	if ( ! s2.isEmpty() ) aName2 = addTitle( s2 );
	if ( ! s3.isEmpty() ) aType = addTitle( s3 );

	aConstellation = addTitle( ksw->data()->skyComposite()->constellation( obj ) );
	
	//Insert Rise/Set/Transit labels
	if ( showRiseSet && obj ) {
		insertSeparator();
		aRiseTime = addTitle( i18n( "Rise time: %1" ).arg("00:00") );
		aSetTime  = addTitle( i18n( "the time at which an object falls below the horizon", 
			"Set time: %1" ).arg(" 00:00") );
		aTransitTime = addTitle( i18n( "Transit time: %1" ).arg("00:00") );

		setRiseSetLabels( obj );
	}

	//Insert item for centering on object
	if ( showCenterTrack && obj ) {
		insertSeparator();
		insertItem( i18n( "Center && Track" ), ksw->map(), SLOT( slotCenter() ) );
	}
	
	//Insert item for measuring distances
	//FIXME: add key shortcut to menu items properly!
	if ( showAngularDistance && obj ) {
		if (! (ksw->map()->isAngleMode()) ) {
			insertItem( i18n( "Angular Distance To...            [" ), ksw->map(), 
					SLOT( slotBeginAngularDistance() ) );
		} else {
			insertItem( i18n( "Compute Angular Distance          ]" ), ksw->map(), 
					SLOT( slotEndAngularDistance() ) );
		}
	}


	//Insert item for Showing details dialog
	if ( showDetails && obj ) {
		insertItem( i18n( "Show Detailed Information Dialog", "Details" ), ksw->map(), 
					SLOT( slotDetail() ) );
	}

	//Insert "Add/Remove Label" item
	if ( showLabel && obj ) {
		if ( ksw->map()->isObjectLabeled( obj ) ) {
			insertItem( i18n( "Remove Label" ), ksw->map(), SLOT( slotRemoveObjectLabel() ) );
		} else {
			insertItem( i18n( "Attach Label" ), ksw->map(), SLOT( slotAddObjectLabel() ) );
		}
	}

	if ( showObsList && obj ) {
		if ( ksw->observingList()->contains( obj ) )
			insertItem( i18n("Remove From List"), ksw->observingList(), SLOT( slotRemoveObject() ) );
		else 
			insertItem( i18n("Add to List"), ksw->observingList(), SLOT( slotAddObject() ) );
	}
	
	if ( showTrail && obj && obj->isSolarSystem() ) {
		if ( addTrail ) {
			insertItem( i18n( "Add Trail" ), ksw->map(), SLOT( slotAddPlanetTrail() ) );
		} else {
			insertItem( i18n( "Remove Trail" ), ksw->map(), SLOT( slotRemovePlanetTrail() ) );
		}
	}

	insertSeparator();

	if (addINDI())
		insertSeparator();
}

void KSPopupMenu::setRiseSetLabels( SkyObject *obj ) {
	if ( ! obj ) return;
	
	QString rt, rt2, rt3;
	QTime rtime = obj->riseSetTime( ksw->data()->ut(), ksw->geo(), true );
	dms rAz = obj->riseSetTimeAz( ksw->data()->ut(), ksw->geo(), true );

	if ( rtime.isValid() ) {
		//We can round to the nearest minute by simply adding 30 seconds to the time.
		rt = i18n( "Rise time: %1" ).arg( rtime.addSecs(30).toString( "hh:mm" ) );

	} else if ( obj->alt()->Degrees() > 0 ) {
		rt = i18n( "No rise time: Circumpolar" );
	} else {
		rt = i18n( "No rise time: Never rises" );
	}

	KStarsDateTime dt = ksw->data()->ut();
	QTime stime = obj->riseSetTime( dt, ksw->geo(), false );

	QString st, st2, st3;
	dms sAz = obj->riseSetTimeAz( dt,  ksw->geo(), false );
	
	if ( stime.isValid() ) {
		//We can round to the nearest minute by simply adding 30 seconds to the time.
		st = i18n( "the time at which an object falls below the horizon", "Set time: %1" ).arg( stime.addSecs(30).toString( "hh:mm" ) );

	} else if ( obj->alt()->Degrees() > 0 ) {
		st = i18n( "No set time: Circumpolar" );
	} else {
		st = i18n( "No set time: Never rises" );
	}

	QTime ttime = obj->transitTime( dt, ksw->geo() );
	dms trAlt = obj->transitAltitude( dt, ksw->geo() );
	QString tt, tt2, tt3;

	if ( ttime.isValid() ) {
		//We can round to the nearest minute by simply adding 30 seconds to the time.
		tt = i18n( "Transit time: %1" ).arg( ttime.addSecs(30).toString( "hh:mm" ) );
	} else {
		tt = "--:--";
	}

	aRiseTime->setText( rt );
	aSetTime->setText( st );
	aTransitTime->setText( tt ) ;
}

#include "kspopupmenu.moc"
