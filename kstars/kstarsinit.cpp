/***************************************************************************
                          kstarsinit.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Mon Feb 25 2002
    copyright            : (C) 2002 by Jason Harris
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

#include <QFile>
#include <QDir>
#include <QTextStream>

#include <kactioncollection.h>
#include <kactionmenu.h>
#include <kiconloader.h>
#include <kmenu.h>
#include <kstatusbar.h>
#include <ktip.h>
#include <kmessagebox.h>
#include <kstandardaction.h>
#include <kstandarddirs.h>
#include <ktoggleaction.h>
#include <ktoolbar.h>
#include <kicon.h>
#include <knewstuff3/knewstuffaction.h>

#include "Options.h"
#include "fov.h"
#include "kstars.h"
#include "kstarsdata.h"
#include "skymap.h"
#include "skyobjects/skyobject.h"
#include "skyobjects/ksplanetbase.h"
#include "simclock.h"
#include "widgets/timestepbox.h"
#include "oal/equipmentwriter.h"
#include "oal/observeradd.h"
#include "skycomponents/skymapcomposite.h"
#include "texturemanager.h"

#include <config-kstars.h>

#ifdef HAVE_INDI_H
#include "indi/drivermanager.h"
#include "indi/guimanager.h"
#endif

//This file contains functions that kstars calls at startup (except constructors).
//These functions are declared in kstars.h

namespace {
    // A lot of KAction is defined there. In order to decrease amount
    // of boilerplate code a trick with << operator overloading is used.
    // This makes code more concise and readable.
    //
    // When data type could not used directly. Either because of
    // overloading rules or because one data type have different
    // semantics its wrapped into struct.
    //
    // Downside is unfamiliar syntax and really unhelpful error
    // messages due to general abuse of << overloading

    // Set KAction text
    KAction* operator << (KAction* ka, QString text) {
        ka->setText(text);
        return ka;
    }
    // Set icon for KAction
    KAction* operator << (KAction* ka, const KIcon& icon) {
        ka->setIcon(icon);
        return ka;
    }
    // Set keyboard shortcut
    KAction* operator << (KAction* ka, const KShortcut& sh) {
        ka->setShortcuts(sh);
        return ka;
    }

    // Add action to group. AddToGroup struct acts as newtype wrapper
    // in order to allow overloading.
    struct AddToGroup {
        QActionGroup* grp;
        AddToGroup(QActionGroup* g) : grp(g) {}
    };
    KAction* operator << (KAction* ka, AddToGroup g) {
        g.grp->addAction(ka);
        return ka;
    }

    // Set checked property. Checked is newtype wrapper.
    struct Checked {
        bool flag;
        Checked(bool f) : flag(f) {}
    };
    KAction* operator << (KAction* ka, Checked chk) {
        ka->setCheckable(true);
        ka->setChecked(chk.flag);
        return ka;
    }

    // Set tool tip. ToolTip is used as newtype wrapper.
    struct ToolTip {
        QString tip;
        ToolTip(QString msg) : tip(msg) {}
    };
    KAction* operator << (KAction* ka, const ToolTip& tool) {
        ka->setToolTip(tool.tip);
        return ka;
    }

    // Create new KToggleAction and connect slot to toggled(bool) signal
    KAction* newToggleAction(KActionCollection* col, QString name, QString text,
                             QObject* receiver, const char* member) {
        KAction* ka = col->add<KToggleAction>(name) << text;
        QObject::connect(ka, SIGNAL( toggled(bool) ), receiver, member);
        return ka;
    }
}

void KStars::initActions() {
    KIconLoader::global()->addAppDir( "kstars" );
    KAction *ka;

    // ==== File menu ================
    ka = KNS3::standardAction(i18n("Download New Data..."), this, SLOT(slotDownload()), actionCollection(), "get_data")
        << KShortcut( Qt::CTRL+Qt::Key_D );
    ka->setWhatsThis(i18n("Downloads new data"));
    ka->setToolTip(ka->whatsThis());
    ka->setStatusTip(ka->whatsThis());

#ifdef HAVE_CFITSIO_H
    actionCollection()->addAction("open_file", this, SLOT(slotOpenFITS()) )
        << i18n("Open FITS...")
        << KIcon("document-open")
        << KShortcut( Qt::CTRL+Qt::Key_O );
#endif

    actionCollection()->addAction("export_image", this, SLOT( slotExportImage() ) )
        << i18n("&Save Sky Image...")
        << KIcon("document-export-image")
        << KShortcut( Qt::CTRL+Qt::Key_I );
    actionCollection()->addAction("run_script", this, SLOT( slotRunScript() ))
        << i18n("&Run Script...")
        << KIcon("system-run" )
        << KShortcut( Qt::CTRL+Qt::Key_R );
    actionCollection()->addAction("printing_wizard", this, SLOT(slotPrintingWizard() ) )
            << i18nc("start Printing Wizard", "Printing &Wizard");
    actionCollection()->addAction( KStandardAction::Print, "print", this, SLOT( slotPrint() ) );
    actionCollection()->addAction( KStandardAction::Quit,  "quit",  this, SLOT( close() ) );

    // ==== Time Menu ================
    actionCollection()->addAction("time_to_now", this, SLOT( slotSetTimeToNow() ))
        << i18n("Set Time to &Now")
        << KShortcut( Qt::CTRL+Qt::Key_E )
        << KIcon("clock");

    actionCollection()->addAction("time_dialog", this, SLOT( slotSetTime() ) )
        << i18nc("set Clock to New Time", "&Set Time..." )
        << KShortcut( Qt::CTRL+Qt::Key_S )
        << KIcon("view-history");

    ka = actionCollection()->add<KToggleAction>("clock_startstop")
        << i18n("Stop &Clock" )
        << KIcon("media-playback-pause" );
    if ( ! StartClockRunning )
        ka->toggle();
    QObject::connect( ka, SIGNAL( triggered() ), this, SLOT( slotToggleTimer() ) );
    QObject::connect(data()->clock(), SIGNAL(clockToggled(bool)), ka, SLOT(setChecked(bool)) );
    //UpdateTime() if clock is stopped (so hidden objects get drawn)
    QObject::connect(data()->clock(), SIGNAL(clockToggled(bool)), this, SLOT(updateTime()) );
    actionCollection()->addAction("time_step_forward", this, SLOT( slotStepForward() ) )
        << i18n("Advance one step forward in time")
        << KIcon("media-skip-forward" )
        << KShortcut( Qt::Key_Greater, Qt::Key_Period );
    actionCollection()->addAction("time_step_backward", this, SLOT( slotStepBackward() ) )
        << i18n("Advance one step backward in time")
        << KIcon("media-skip-backward" )
        << KShortcut( Qt::Key_Less, Qt::Key_Comma );

    // ==== Pointing Menu ================
    actionCollection()->addAction("zenith", this, SLOT( slotPointFocus() ) )
        << i18n("&Zenith")
        << KShortcut("Z");
    actionCollection()->addAction("north", this, SLOT( slotPointFocus() ) )
        << i18n("&North")
        << KShortcut("N");
    actionCollection()->addAction("east", this, SLOT( slotPointFocus() ) )
        << i18n("&East")
        << KShortcut("E");
    actionCollection()->addAction("south", this, SLOT( slotPointFocus() ) )
        << i18n("&South")
        << KShortcut("S");
    actionCollection()->addAction("west", this, SLOT( slotPointFocus() ) )
        << i18n("&West")
        << KShortcut("W");

    actionCollection()->addAction("find_object", this, SLOT( slotFind() ) )
        << i18n("&Find Object...")
        << KIcon("edit-find")
        << KShortcut( Qt::CTRL+Qt::Key_F );
    actionCollection()->addAction("track_object", this, SLOT( slotTrack() ) )
        << i18n("Engage &Tracking")
        << KIcon("object-locked" )
        << KShortcut( Qt::CTRL+Qt::Key_T  );
    actionCollection()->addAction("manual_focus", this, SLOT( slotManualFocus() ) )
        << i18n("Set Coordinates &Manually..." )
        << KShortcut( Qt::CTRL+Qt::Key_M );

    // ==== View Menu ================
    actionCollection()->addAction( KStandardAction::ZoomIn,  "zoom_in",  map(), SLOT( slotZoomIn() ) );
    actionCollection()->addAction( KStandardAction::ZoomOut, "zoom_out", map(), SLOT( slotZoomOut() ) );
    actionCollection()->addAction("zoom_default", map(), SLOT( slotZoomDefault() ) )
        << i18n("&Default Zoom")
        << KIcon("zoom-fit-best" )
        << KShortcut( Qt::CTRL+Qt::Key_Z );
    actionCollection()->addAction("zoom_set", this, SLOT( slotSetZoom() ) )
        << i18n("&Zoom to Angular Size..." )
        << KIcon("zoom-original" )
        << KShortcut( Qt::CTRL+Qt::SHIFT+Qt::Key_Z );

    actionCollection()->addAction( KStandardAction::FullScreen, this, SLOT( slotFullScreen() ) );

    actionCollection()->addAction("coordsys", this, SLOT( slotCoordSys() ) )
        << (Options::useAltAz() ? i18n("Switch to star globe view (Equatorial &Coordinates)"): i18n("Switch to horizonal view (Horizontal &Coordinates)"))
        << KShortcut("Space" );

    #ifdef HAVE_OPENGL
    Q_ASSERT( SkyMap::Instance() ); // This assert should not fail, because SkyMap is already created by now. Just throwing it in anyway.
    actionCollection()->addAction("opengl", SkyMap::Instance(), SLOT( slotToggleGL() ) )
        << (Options::useGL() ? i18n("Switch to QPainter backend"): i18n("Switch to OpenGL backend"));
    #endif

    actionCollection()->addAction("project_lambert", this, SLOT( slotMapProjection() ) )
        << i18n("&Lambert Azimuthal Equal-area" )
        << KShortcut("F5" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::Lambert);
    actionCollection()->addAction("project_azequidistant", this, SLOT( slotMapProjection() ) )
        << i18n("&Azimuthal Equidistant" )
        << KShortcut("F6" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::AzimuthalEquidistant);
    actionCollection()->addAction("project_orthographic", this, SLOT( slotMapProjection() ) )
        << i18n("&Orthographic" )
        << KShortcut("F7" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::Orthographic);
    actionCollection()->addAction("project_equirectangular", this, SLOT( slotMapProjection() ) )
        << i18n("&Equirectangular" )
        << KShortcut("F8" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::Equirectangular);
    actionCollection()->addAction("project_stereographic", this, SLOT( slotMapProjection() ) )
        << i18n("&Stereographic" )
        << KShortcut("F9" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::Stereographic);
    actionCollection()->addAction("project_gnomonic", this, SLOT( slotMapProjection() ) )
        << i18n("&Gnomonic" )
        << KShortcut("F10" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::Gnomonic);
    actionCollection()->addAction("project_interrupted", this, SLOT( slotMapProjection() ) )
        << i18n("&Interrupted Sinusoidal" )
        << KShortcut("F11" )
        << AddToGroup(projectionGroup)
        << Checked(Options::projection() == SkyMap::InterruptedSinusoidal);

    //Settings Menu:
    //Info Boxes option actions
    KAction* kaBoxes = actionCollection()->add<KToggleAction>("show_boxes" )
        << i18nc("Show the information boxes", "Show &Info Boxes")
        << Checked( Options::showInfoBoxes() );
    connect( kaBoxes, SIGNAL(toggled(bool)), map(), SLOT(slotToggleInfoboxes(bool)));
    kaBoxes->setChecked( Options::showInfoBoxes() );

    ka = actionCollection()->add<KToggleAction>("show_time_box")
        << i18nc("Show time-related info box", "Show &Time Box");
    connect(kaBoxes, SIGNAL( toggled(bool) ), ka,    SLOT( setEnabled(bool) ) );
    connect(ka,      SIGNAL( toggled(bool) ), map(), SLOT( slotToggleTimeBox(bool)));
    ka->setChecked( Options::showTimeBox() );
    ka->setEnabled( Options::showInfoBoxes() );

    ka = actionCollection()->add<KToggleAction>("show_focus_box")
        << i18nc("Show focus-related info box", "Show &Focus Box");
    connect(kaBoxes, SIGNAL( toggled(bool) ), ka,    SLOT( setEnabled(bool) ) );
    connect(ka,      SIGNAL( toggled(bool) ), map(), SLOT( slotToggleFocusBox(bool)));
    ka->setChecked( Options::showFocusBox() );
    ka->setEnabled( Options::showInfoBoxes() );

    ka = actionCollection()->add<KToggleAction>("show_location_box")
        << i18nc("Show location-related info box", "Show &Location Box");
    connect(kaBoxes, SIGNAL( toggled(bool) ), ka,    SLOT( setEnabled(bool) ) );
    connect(ka,      SIGNAL( toggled(bool) ), map(), SLOT( slotToggleGeoBox(bool)));
    ka->setChecked( Options::showGeoBox() );
    ka->setEnabled( Options::showInfoBoxes() );


    //Toolbar options
    newToggleAction( actionCollection(), "show_mainToolBar", i18n("Show Main Toolbar"),
                     toolBar("kstarsToolBar"), SLOT(setVisible(bool)));
    newToggleAction( actionCollection(), "show_viewToolBar", i18n("Show View Toolbar"),
                     toolBar( "viewToolBar" ), SLOT(setVisible(bool)));

    //Statusbar view options
    newToggleAction( actionCollection(), "show_statusBar", i18n("Show Statusbar"),
                     this, SLOT(slotShowGUIItem(bool)));
    newToggleAction( actionCollection(), "show_sbAzAlt",   i18n("Show Az/Alt Field"),
                     this, SLOT(slotShowGUIItem(bool)));
    newToggleAction( actionCollection(), "show_sbRADec",   i18n("Show RA/Dec Field"),
                     this, SLOT(slotShowGUIItem(bool)));

    //Color scheme actions.  These are added to the "colorschemes" KActionMenu.
    colorActionMenu = actionCollection()->add<KActionMenu>("colorschemes" );
    colorActionMenu->setText( i18n("C&olor Schemes" ) );
    addColorMenuItem( i18n("&Classic" ), "cs_classic" );
    addColorMenuItem( i18n("&Star Chart" ), "cs_chart" );
    addColorMenuItem( i18n("&Night Vision" ), "cs_night" );
    addColorMenuItem( i18n("&Moonless Night" ), "cs_moonless-night" );

    //Add any user-defined color schemes:
    QFile file( KStandardDirs::locate("appdata", "colors.dat" ) ); //determine filename in local user KDE directory tree.
    if ( file.exists() && file.open( QIODevice::ReadOnly ) ) {
        QTextStream stream( &file );
        while ( !stream.atEnd() ) {
            QString line = stream.readLine();
            QString schemeName = line.left( line.indexOf( ':' ) );
            QString actionname = "cs_" + line.mid( line.indexOf( ':' ) +1, line.indexOf( '.' ) - line.indexOf( ':' ) - 1 );
            addColorMenuItem( i18n( schemeName.toLocal8Bit() ), actionname.toLocal8Bit() );
        }
        file.close();
    }

    //Add FOV Symbol actions
    fovActionMenu = actionCollection()->add<KActionMenu>("fovsymbols" );
    fovActionMenu->setText( i18n("&FOV Symbols" ) );
    repopulateFOV();

    actionCollection()->addAction("geolocation", this, SLOT( slotGeoLocator() ) )
        << i18nc("Location on Earth", "&Geographic..." )
        << KIcon("applications-internet" )
        << KShortcut( Qt::CTRL+Qt::Key_G );
    actionCollection()->addAction( KStandardAction::Preferences, "configure", this, SLOT( slotViewOps() ) );
    actionCollection()->addAction("startwizard", this, SLOT( slotWizard() ) )
        << i18n("Startup Wizard..." )
        << KIcon("tools-wizard" );

    // Updates actions
    actionCollection()->addAction( "update_comets", this, SLOT( slotUpdateComets() ) )
        << i18n( "Update comets orbital elements" );
    actionCollection()->addAction( "update_asteroids", this, SLOT( slotUpdateAsteroids() ) )
        << i18n( "Update asteroids orbital elements" );
    actionCollection()->addAction("update_supernovae", this, SLOT(slotUpdateSupernovae() ) )
        << i18n( "Update Recent Supernovae data" );
    actionCollection()->addAction("update_satellites", this, SLOT(slotUpdateSatellites() ) )
        << i18n( "Update satellites orbital elements" );

    //Tools Menu:
    actionCollection()->addAction("astrocalculator", this, SLOT( slotCalculator() ) )
        << i18n("Calculator")
        << KIcon("accessories-calculator" )
        << KShortcut( Qt::CTRL+Qt::Key_C );

    actionCollection()->addAction("moonphasetool", this, SLOT( slotMoonPhaseTool() ) )
        << i18n("Moon Phase Calendar");

    actionCollection()->addAction("obslist", this, SLOT( slotObsList() ) )
        << i18n("Observation Planner")
        << KShortcut( Qt::CTRL+Qt::Key_L );

    actionCollection()->addAction("altitude_vs_time", this, SLOT( slotAVT() ) )
        << i18n("Altitude vs. Time")
        << KShortcut( Qt::CTRL+Qt::Key_A );
    actionCollection()->addAction("whats_up_tonight", this, SLOT( slotWUT() ) )
        << i18n("What's up Tonight")
        << KShortcut(Qt::CTRL+Qt::Key_U );
    actionCollection()->addAction("whats_interesting", this, SLOT( slotWISettings() ) )
        << i18n("What's Interesting...")
        << KShortcut(Qt::CTRL+Qt::Key_W );
    actionCollection()->addAction("skycalendar", this, SLOT( slotCalendar() ) )
        << i18n("Sky Calendar");

#ifdef HAVE_INDI_H
#ifndef Q_WS_WIN
        actionCollection()->addAction("ekos", this, SLOT( slotEkos() ) )
            << i18n("Ekos");
#endif
#endif

//FIXME: implement glossary
//     ka = actionCollection()->addAction("glossary");
//     ka->setText( i18n("Glossary...") );
//     ka->setShortcuts( KShortcut(Qt::CTRL+Qt::Key_K ) );
//     connect( ka, SIGNAL( triggered() ), this, SLOT( slotGlossary() ) );

    actionCollection()->addAction("scriptbuilder", this, SLOT( slotScriptBuilder() ) )
        << i18n("Script Builder")
        << KShortcut(Qt::CTRL+Qt::Key_B );
    actionCollection()->addAction("solarsystem", this, SLOT( slotSolarSystem() ) )
        << i18n("Solar System")
        << KShortcut(Qt::CTRL+Qt::Key_Y );
    actionCollection()->addAction("jmoontool", this, SLOT( slotJMoonTool() ) )
        << i18n("Jupiter's Moons")
        << KShortcut(Qt::CTRL+Qt::Key_J );
    actionCollection()->addAction("flagmanager", this, SLOT( slotFlagManager() ) )
        << i18n("Flags");

    actionCollection()->addAction("ewriter", this, SLOT( slotEquipmentWriter() ) )
        << i18n("Define Equipment...")
        << KShortcut( Qt::CTRL+Qt::Key_0 );
    actionCollection()->addAction("obsadd", this, SLOT( slotObserverAdd() ) )
        << i18n( "Add Observer..." )
        << KShortcut( Qt::CTRL+Qt::Key_1 );

    // ==== observation menu ================
    ka = actionCollection()->addAction("execute", this, SLOT( slotExecute() ) )
        << i18n( "Execute the session Plan..." )
        << KShortcut( Qt::CTRL+Qt::Key_2 );

    // ==== devices Menu ================
#ifdef HAVE_INDI_H
#ifndef Q_WS_WIN


        actionCollection()->addAction("telescope_wizard", this, SLOT( slotTelescopeWizard() ) )
            << i18n("Telescope Wizard...")
            << KIcon("tools-wizard" );
        actionCollection()->addAction("device_manager", this, SLOT( slotINDIDriver() ) )
            << i18n("Device Manager...")
            << KIcon("network-server" );
        ka = actionCollection()->addAction("indi_cpl", this, SLOT( slotINDIPanel() ) )
            << i18n("INDI Control Panel...");
        ka->setEnabled(false);


#endif
#endif

    //Help Menu:
    actionCollection()->addAction( KStandardAction::TipofDay, "help_tipofday", this, SLOT( slotTipOfDay() ) )
	->setWhatsThis(i18n("Displays the Tip of the Day"));

    //	KStandardAction::help(this, SLOT( appHelpActivated() ), actionCollection(), "help_contents" );

    //Add timestep widget for toolbar
    TimeStep = new TimeStepBox( toolBar("kstarsToolBar") );
    // Add a tool tip to TimeStep describing the weird nature of time steps
    QString TSBToolTip = i18nc( "Tooltip describing the nature of the time step control", "Use this to set the rate at which time in the simulation flows.\nFor time step \'X\' up to 10 minutes, time passes at the rate of \'X\' per second.\nFor time steps larger than 10 minutes, frames are displayed at an interval of \'X\'." );
    TimeStep->setToolTip( TSBToolTip );
    TimeStep->tsbox()->setToolTip( TSBToolTip );
    ka = actionCollection()->addAction("timestep_control")
        << i18n("Time step control");
    ka->setDefaultWidget( TimeStep );

    // ==== viewToolBar actions ================
    actionCollection()->add<KToggleAction>("show_stars", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Stars in the display", "Stars" )
        << KIcon("kstars_stars" )
        << ToolTip( i18n("Toggle stars") );
    actionCollection()->add<KToggleAction>("show_deepsky", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Deep Sky Objects in the display", "Deep Sky" )
        << KIcon("kstars_deepsky" )
        << ToolTip( i18n("Toggle deep sky objects") );
    actionCollection()->add<KToggleAction>("show_planets", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Solar System objects in the display", "Solar System" )
        << KIcon("kstars_planets" )
        << ToolTip( i18n("Toggle Solar system objects") );
    actionCollection()->add<KToggleAction>("show_clines", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Constellation Lines in the display", "Const. Lines" )
        << KIcon("kstars_clines" )
        << ToolTip( i18n("Toggle constellation lines") );
    actionCollection()->add<KToggleAction>("show_cnames", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Constellation Names in the display", "Const. Names" )
        << KIcon("kstars_cnames" )
        << ToolTip( i18n("Toggle constellation names") );
    actionCollection()->add<KToggleAction>("show_cbounds", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Constellation Boundaries in the display", "C. Boundaries" )
        << KIcon("kstars_cbound" )
        << ToolTip( i18n("Toggle constellation boundaries") );
    actionCollection()->add<KToggleAction>("show_mw", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Milky Way in the display", "Milky Way" )
        << KIcon("kstars_mw" )
        << ToolTip( i18n("Toggle milky way") );
    actionCollection()->add<KToggleAction>("show_equatorial_grid", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Equatorial Coordinate Grid in the display", "Equatorial coord. grid" )
        << KIcon("kstars_grid" )
        << ToolTip( i18n("Toggle equatorial coordinate grid") );
    actionCollection()->add<KToggleAction>("show_horizontal_grid", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle Horizontal Coordinate Grid in the display", "Horizontal coord. grid" )
        << KIcon("kstars_hgrid" )
        << ToolTip( i18n("Toggle horizontal coordinate grid") );
    actionCollection()->add<KToggleAction>("show_horizon", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle the opaque fill of the ground polygon in the display", "Ground" )
        << KIcon("kstars_horizon" )
        << ToolTip( i18n("Toggle opaque ground") );
    actionCollection()->add<KToggleAction>("show_flags", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle flags in the display", "Flags" )
        << KIcon("kstars_flag" )
        << ToolTip( i18n("Toggle flags") );
    actionCollection()->add<KToggleAction>("show_satellites", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle satellites in the display", "Satellites" )
        << KIcon("kstars_satellites" )
        << ToolTip( i18n("Toggle satellites") );
    actionCollection()->add<KToggleAction>("show_supernovae", this, SLOT( slotViewToolBar() ) )
        << i18nc("Toggle supernovae in the display", "Supernovae" )
        << KIcon("kstars_supernovae" )
        << ToolTip( i18n("Toggle supernovae") );

    setXMLFile("kstarsui.rc" );

    if (Options::fitsDir().isEmpty())
        Options::setFitsDir(QDir:: homePath());
}

void KStars::repopulateFOV() {
    // Read list of all FOVs
    qDeleteAll( data()->availFOVs );
    data()->availFOVs = FOV::readFOVs();
    data()->syncFOV();

    // Iterate through FOVs
    fovActionMenu->menu()->clear();
    foreach(FOV* fov, data()->availFOVs) {
        KToggleAction *kta = actionCollection()->add<KToggleAction>( fov->name() );
        kta->setText( fov->name() );
        if( Options::fOVNames().contains( fov->name() ) ) {
            kta->setChecked(true);
        }
        fovActionMenu->addAction( kta );
        connect( kta, SIGNAL( toggled( bool ) ), this, SLOT( slotTargetSymbol(bool) ) );
    }
    // Add menu bottom
    KAction* ka = actionCollection()->addAction("edit_fov",  this, SLOT( slotFOVEdit() ) )
        << i18n("Edit FOV Symbols...");
    fovActionMenu->addSeparator();
    fovActionMenu->addAction( ka );
}

void KStars::initStatusBar() {
    statusBar()->insertPermanentItem( i18n( " Welcome to KStars " ), 0, 1 );
    statusBar()->setItemAlignment( 0, Qt::AlignLeft | Qt::AlignVCenter );

    QString s = "000d 00m 00s,   +00d 00\' 00\""; //only need this to set the width
    if ( Options::showAltAzField() ) {
        statusBar()->insertPermanentFixedItem( s, 1 );
        statusBar()->setItemAlignment( 1, Qt::AlignRight | Qt::AlignVCenter );
        statusBar()->changeItem( QString(), 1 );
    }

    if ( Options::showRADecField() ) {
        statusBar()->insertPermanentFixedItem( s, 2 );
        statusBar()->setItemAlignment( 2, Qt::AlignRight | Qt::AlignVCenter );
        statusBar()->changeItem( QString(), 2 );
    }

    if ( ! Options::showStatusBar() )
        statusBar()->hide();
}

void KStars::datainitFinished() {
    //Time-related connections
    connect( data()->clock(), SIGNAL( timeAdvanced() ),
             this, SLOT( updateTime() ) );
    connect( data()->clock(), SIGNAL( timeChanged() ),
             this, SLOT( updateTime() ) );

    //Add GUI elements to main window
    buildGUI();

    connect( data()->clock(), SIGNAL( scaleChanged( float ) ),
             map(), SLOT( slotClockSlewing() ) );

    connect( data(),   SIGNAL( update() ),            map(),  SLOT( forceUpdateNow() ) );
    connect( TimeStep, SIGNAL( scaleChanged(float) ), data(), SLOT( setTimeDirection( float ) ) );
    connect( TimeStep, SIGNAL( scaleChanged(float) ),
             data()->clock(), SLOT( setClockScale( float )) );
    connect( TimeStep, SIGNAL( scaleChanged(float) ), map(),  SLOT( setFocus() ) );


    //Initialize Observing List
    obsList = new ObservingList( this );
    eWriter = new EquipmentWriter();
    oAdd = new ObserverAdd;

    //Do not start the clock if "--paused" specified on the cmd line
    if ( StartClockRunning )
        data()->clock()->start();

    // Connect cache function for Find dialog
    connect( data(), SIGNAL( clearCache() ), this,
             SLOT( clearCachedFindDialog() ) );

    //Propagate config settings
    applyConfig( false );

    //show the window.  must be before kswizard and messageboxes
    show();

    //Initialize focus
    initFocus();

    data()->setFullTimeUpdate();
    updateTime();

    //If this is the first startup, show the wizard
    if ( Options::runStartupWizard() ) {
        slotWizard();
    }

    //Show TotD
    KTipDialog::showTip(this, "kstars/tips");

    //DEBUG
    kDebug() << "The current Date/Time is: " << KStarsDateTime::currentDateTime().toString();
}

void KStars::initFocus() {
    //Case 1: tracking on an object
    if ( Options::isTracking() && Options::focusObject() != i18n("nothing") ) {
        SkyObject *oFocus;
        if ( Options::focusObject() == i18n("star") ) {
            SkyPoint p( Options::focusRA(), Options::focusDec() );
            double maxrad = 1.0;

            oFocus = data()->skyComposite()->starNearest( &p, maxrad );
        } else {
            oFocus = data()->objectNamed( Options::focusObject() );
        }

        if ( oFocus ) {
            map()->setFocusObject( oFocus );
            map()->setClickedObject( oFocus );
            map()->setFocusPoint( oFocus );
        } else {
            kWarning() << "Cannot center on "
                       << Options::focusObject()
                       << ": no object found." << endl;
        }

    //Case 2: not tracking, and using Alt/Az coords.  Set focus point using
    //FocusRA as the Azimuth, and FocusDec as the Altitude
    } else if ( ! Options::isTracking() && Options::useAltAz() ) {
        SkyPoint pFocus;
        pFocus.setAz( Options::focusRA() );
        pFocus.setAlt( Options::focusDec() );
        pFocus.HorizontalToEquatorial( data()->lst(), data()->geo()->lat() );
        map()->setFocusPoint( &pFocus );

    //Default: set focus point using FocusRA as the RA and
    //FocusDec as the Dec
    } else {
        SkyPoint pFocus( Options::focusRA(), Options::focusDec() );
        pFocus.EquatorialToHorizontal( data()->lst(), data()->geo()->lat() );
        map()->setFocusPoint( &pFocus );
    }
    data()->setSnapNextFocus();
    map()->setDestination( *map()->focusPoint() );
    map()->setFocus( map()->destination() );

    map()->showFocusCoords();

    //Check whether initial position is below the horizon.
    if ( Options::useAltAz() && Options::showGround() &&
            map()->focus()->alt().Degrees() < -1.0 ) {
        QString caption = i18n( "Initial Position is Below Horizon" );
        QString message = i18n( "The initial position is below the horizon.\nWould you like to reset to the default position?" );
        if ( KMessageBox::warningYesNo( this, message, caption,
                                        KGuiItem(i18n("Reset Position")), KGuiItem(i18n("Do Not Reset")), "dag_start_below_horiz" ) == KMessageBox::Yes ) {
            map()->setClickedObject( NULL );
            map()->setFocusObject( NULL );
            Options::setIsTracking( false );

            data()->setSnapNextFocus(true);

            SkyPoint DefaultFocus;
            DefaultFocus.setAz( 180.0 );
            DefaultFocus.setAlt( 45.0 );
            DefaultFocus.HorizontalToEquatorial( data()->lst(), data()->geo()->lat() );
            map()->setDestination( DefaultFocus );
        }
    }

    //If there is a focusObject() and it is a SS body, add a temporary Trail
    if ( map()->focusObject() && map()->focusObject()->isSolarSystem()
            && Options::useAutoTrail() ) {
        ((KSPlanetBase*)map()->focusObject())->addToTrail();
        data()->temporaryTrail = true;
    }
}

void KStars::buildGUI() {
    //create the texture manager
    TextureManager::Create();
    //create the skymap
    skymap = SkyMap::Create();
    connect(skymap, SIGNAL(mousePointChanged(SkyPoint*)), SLOT(slotShowPositionBar(SkyPoint*)));
    connect(skymap, SIGNAL(zoomChanged()),                SLOT(slotZoomChanged()));
    setCentralWidget( skymap );

    //Initialize menus, toolbars, and statusbars
    initStatusBar();
    initActions();

    setupGUI(StandardWindowOptions (Default & ~Create));

#ifdef Q_WS_WIN
    createGUI("kstarsui-win.rc");
#else
    createGUI("kstarsui.rc");
#endif

    //get focus of keyboard and mouse actions (for example zoom in with +)
    map()->QWidget::setFocus();
    resize( Options::windowWidth(), Options::windowHeight() );

    // check zoom in/out buttons
    if ( Options::zoomFactor() >= MAXZOOM ) actionCollection()->action("zoom_in")->setEnabled( false );
    if ( Options::zoomFactor() <= MINZOOM ) actionCollection()->action("zoom_out")->setEnabled( false );
}
