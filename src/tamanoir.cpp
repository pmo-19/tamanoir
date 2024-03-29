/***************************************************************************
 *           tamanoirapp.cpp - Main application GUI+Main thread
 *
 *  Tue Oct 23 22:10:56 2007
 *  Copyright  2007  Christophe Seyve
 *  Email cseyve@free.fr
 ****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define TAMANOIRAPP_CPP

#include "tamanoir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgproc.h"

// include files for QT
#include <QFileInfo>
#include <QSplashScreen>

// Preferences UI
#include "prefsdialog.h"
#include "tamanoirwizard.h"
#include <sys/time.h>

/* File for logging messages */
extern FILE * logfile;


extern u8 g_debug_imgverbose;
extern u8 g_debug_savetmp;
extern u8 g_debug_correlation;
extern u8 g_evaluate_mode;
extern u8 g_debug_dust_seek;
extern QString g_application_path;


/// Debug level for Tamanoir processing thread
u8 g_debug_TmThread = TMLOG_INFO;

/// Debug level for Tamanoir GUI
u8 g_debug_TamanoirApp = TMLOG_INFO;
u8 g_debug_TamanoirThread = TMLOG_DEBUG;


#ifdef WIN32
// NTFS/FAT is not protected against multiple write, so we use a mutex to lock printf
static Mutex_t * logfile_mutex = NULL;
#define TMAPP_printf(a,...)       { \
			if( (a)<=g_debug_TamanoirApp ) { \
					if(!logfile_mutex) { \
						logfile_mutex = new Mutex_t; \
						MUTEX_INIT(logfile_mutex) \
					} \
					MUTEX_LOCK(logfile_mutex) \
					struct timeval l_nowtv; gettimeofday (&l_nowtv, NULL); \
					fprintf(logfile,"%03d.%03d %s [TmApp]::%s:%d : ", \
							(int)(l_nowtv.tv_sec%1000), (int)(l_nowtv.tv_usec/1000), \
							TMLOG_MSG((a)), __func__,__LINE__); \
					fprintf(logfile,__VA_ARGS__); \
					fprintf(logfile,"\n"); \
					fflush(logfile); \
					MUTEX_UNLOCK(logfile_mutex) \
			} \
	}


#define TMTHR_printf(a,...)       { \
			if( (a)<=g_debug_TamanoirThread ) { \
					if(!logfile_mutex) { \
						logfile_mutex = new Mutex_t; \
						MUTEX_INIT(logfile_mutex) \
					} \
					MUTEX_LOCK(logfile_mutex) \
					struct timeval l_nowtv; gettimeofday (&l_nowtv, NULL); \
					fprintf(logfile,"%d.%03d %s [TmThread]::%s:%d : ", \
							(int)(l_nowtv.tv_sec), (int)(l_nowtv.tv_usec/1000), \
							TMLOG_MSG((a)), __func__,__LINE__); \
					fprintf(logfile,__VA_ARGS__); \
					fprintf(logfile,"\n"); \
					fflush(logfile); \
					MUTEX_UNLOCK(logfile_mutex) \
			} \
	}
#else // not WIN32 = MacOS X and Linux : protected filesystem, no nee for Mutex
#define TMAPP_printf(a,...)       { \
			if( (a)<=g_debug_TamanoirApp ) { \
					struct timeval l_nowtv; gettimeofday (&l_nowtv, NULL); \
					fprintf(logfile,"%03d.%03d %s [TmApp]::%s:%d : ", \
							(int)(l_nowtv.tv_sec%1000), (int)(l_nowtv.tv_usec/1000), \
							TMLOG_MSG((a)), __func__,__LINE__); \
					fprintf(logfile,__VA_ARGS__); \
					fprintf(logfile,"\n"); \
					fflush(logfile); \
			} \
	}


#define TMTHR_printf(a,...)       { \
			if( (a)<=g_debug_TamanoirThread ) { \
					struct timeval l_nowtv; gettimeofday (&l_nowtv, NULL); \
					fprintf(logfile,"%d.%03d %s [TmThread]::%s:%d : ", \
							(int)(l_nowtv.tv_sec), (int)(l_nowtv.tv_usec/1000), \
							TMLOG_MSG((a)), __func__,__LINE__); \
					fprintf(logfile,__VA_ARGS__); \
					fprintf(logfile,"\n"); \
					fflush(logfile); \
			} \
	}
#endif


#ifdef SIMPLE_VIEW
u8 g_debug_displaylabel = 0;
#else
u8 g_debug_displaylabel = 1;
#endif

#define changeCurCommand(_cmd) { \
			fprintf(stderr, "CHG COMMAND from %s:%d ", __func__, __LINE__); \
			setCurCommand((_cmd)); \
	}

/// Period of background processing threadpolling
#define TMAPP_TIMEOUT	1000

/// Debug management of skipped/known dusts
u8 g_debug_list = 0;

/** constructor */
TamanoirApp::TamanoirApp(QWidget * l_parent)
	: QMainWindow(l_parent)
{
#ifdef WIN32
	logfile = fopen("tamanoir.log", "w");
#endif
	fprintf(logfile, "TamanoirApp::%s:%d : ...\n", __func__, __LINE__);
	statusBar()->showMessage( QString("") );
	m_fileDialog = NULL;
	m_pImgProc = NULL;
	m_curCommand = PROTH_NOTHING;
	m_pProcThread = NULL;
	m_pProgressDialog = NULL;
	force_mode = false;
	cropPixmapLabel_last_button = Qt::NoButton;
	is_src_selected = true;
	m_searchCloneSrc = true; // search for clone src candidate
	m_unsaved_changes = false;

	m_draw_on = TMMODE_NOFORCE;

	// Clear options
	memset(&g_options, 0, sizeof(tm_options));
	memset(&g_display_options, 0, sizeof(tm_display_options));

	memset(&m_current_dust, 0, sizeof(t_correction));

	QString homeDirStr = QString("/home/");
	if(getenv("HOME")) {
		homeDirStr = QString(getenv("HOME"));
	}
	QByteArray homedir_utf8 = homeDirStr.toUtf8();
	strcpy(g_display_options.currentDir, homedir_utf8.data());

	ui.setupUi((QMainWindow *)this);
	ui.infoFrame->hide();

	ui.correctPixmapLabel->setFocusPolicy(Qt::StrongFocus);


	m_resize_rect_xscale = m_resize_rect_yscale = 2;
	m_resize_rect = false;
	m_overCorrected = false;

	// Load last options
	loadOptions();

	connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(on_refreshTimer_timeout()));

	ui.prevButton->setEnabled(TRUE);
	ui.loadingTextLabel->setText(QString(""));
	ui.cloneButton->setCheckable(true);

	m_resize_rect = false;

	m_main_display_rect = ui.mainPixmapLabel->maximumSize();
	m_nav_x_block = m_nav_y_block = 0;
#ifdef SIMPLE_VIEW
	ui.fullScreenButton->setCheckable(TRUE);
	ui.diffPixmapLabel->hide();
	ui.growPixmapLabel->hide();
	ui.hotPixelsCheckBox->hide();
// Hide auto button because its use if not very well defined
	//ui.autoButton->hide();
	ui.loadingTextLabel->hide();
	//ui.hiddenFrame->hide();
#endif
	int size_w = (ui.cropPixmapLabel->size().width()/4)*4+2;
	int size_h = (ui.cropPixmapLabel->size().height()/4)*4+2;

	m_blockSize = cvSize(size_w, size_h);

	if(!g_display_options.hide_wizard) {
		on_actionTutorial_activated();
	}
}


/** destructor */
TamanoirApp::~TamanoirApp()
{
	purge();

	TMAPP_printf(TMLOG_INFO, "at quit : memory usage:")
	tmPrintIplImages();
}


void TamanoirApp::purge() {
	if(m_pProcThread) {
		delete m_pProcThread;
		m_pProcThread = NULL;
	}
	if(m_pImgProc) {
		delete m_pImgProc;
		m_pImgProc = NULL;
	}
}

QSplashScreen * g_splash = NULL;

void TamanoirApp::on_actionAbout_activated() {
	QDir dir(g_application_path);
	QPixmap pix(":/icon/tamanoir_about.png");
	if(g_splash) {
		g_splash->setPixmap(pix);
	}
	else {
		g_splash = new QSplashScreen(pix, Qt::WindowStaysOnTopHint );
	}


	QString verstr, cmd = QString("Ctrl+");
	QString item = QString("<li>"), itend = QString("</li>\n");
	g_splash->showMessage(tr("<b>Tamanoir</b> version: ")
						  + verstr.sprintf("svn%04d%02d%02d", VERSION_YY, VERSION_MM, VERSION_DD)

						  + QString("<br>Website: <a href=\"http://tamanoir.googlecode.com/\">http://tamanoir.googlecode.com/</a><br><br>")
						  + tr("Developer: ") + QString("C. Seyve - ")
						  + tr("Artist: ") + QString("M. Lecarme<br><br>")
							);
	repaint();// to force display of splash

	g_splash->show();
	g_splash->raise(); // for full screen mode
	g_splash->update();

}

void TamanoirApp::on_actionTutorial_activated() {
	//
	TamanoirWizard * wiz = new TamanoirWizard();
	wiz->exec();
}

void TamanoirApp::on_actionFull_screen_activated() {
	on_fullScreenButton_clicked();
}


void TamanoirApp::on_actionShortcuts_activated() {
	QDir dir(g_application_path);
	QPixmap pix(":/icon/tamanoir_splash.png");
	if(g_splash) {
		g_splash->setPixmap(pix);
	}
	else {
		g_splash = new QSplashScreen(pix, Qt::WindowStaysOnTopHint );
	}

	QString verstr, cmd = QString("Ctrl+");
	QString item = QString("<li>"), itend = QString("</li>\n");
	g_splash->showMessage(tr("<b>Tamanoir</b> version: ")
						  + verstr.sprintf("svn%04d%02d%02d", VERSION_YY, VERSION_MM, VERSION_DD)
						  + QString("<br>Website & Wiki: <a href=\"http://tamanoir.googlecode.com/\">http://tamanoir.googlecode.com/</a><br><br>")
						  + tr("Shortcuts :<br><ul>\n")
							+ item + cmd + tr("O: Open a picture file") + itend
							+ item + cmd + tr("S: Save corrected image") + itend
							+ item + cmd + tr("H: Display version information") + itend
							+ item + tr("M: Mark current crop area in red") + itend
							+ item + tr("A: Apply proposed correction") + itend
							+ item + tr("&rarr;: Go to next proposal") + itend
							+ item + tr("&larr;: Go to previous proposal") + itend
							+ item + tr("C: Switch to clone tool mode") + itend
//							+ item + tr("") + itend
//							+ item + cmd + tr("") + itend
//							+ item + tr("") + itend
//							+ item + cmd + tr("") + itend
						+ QString("</ul>\n")
							);
	repaint();// to force display of splash

	g_splash->show();
	g_splash->raise(); // for full screen mode
	g_splash->update();
}

void TamanoirApp::on_actionPreferences_activated() {
	PrefsDialog *widget = new PrefsDialog;

	widget->show();
}


void TamanoirApp::on_fullScreenButton_clicked() {
	if(	!isFullScreen()) {
		// hide menu bar to gain some space
		menuBar()->hide();
		showFullScreen();
	} else {
		menuBar()->show();
		showNormal();
	}
}

void TamanoirApp::resizeEvent(QResizeEvent * e) {
	// Resize components
	if(!e) return;

	int size_w = ((int)(ui.cropPixmapLabel->size().width()/4) )*4;
	int size_h = ((int)(ui.cropPixmapLabel->size().height()/4) )*4;
	m_blockSize = cvSize(size_w, size_h);
	size_w += 2;
	size_h += 2;

	ui.cropPixmapLabel->resize( size_w, size_h);
	ui.correctPixmapLabel->resize( size_w, size_h);

	// Then force update of crops
	updateDisplay();
}


const char * getCommandName(int cmd) {
	return g_command_names[cmd];
}

void TamanoirApp::on_refreshTimer_timeout() {
	if(m_pProcThread) {
		if(refreshTimer.isActive()
		   && (m_curCommand == PROTH_NOTHING
			   || m_curCommand == PROTH_SEARCH)
			&& m_pProcThread->getCommand() == PROTH_NOTHING) {
			// Stop timer only if not in auto mode
			if( !m_pProcThread->getModeAuto()) {
				TMAPP_printf(TMLOG_DEBUG, "PROTH_NOTHING && not in auto mode "
							 "=> stop refresh timer")
				refreshTimer.stop();
			}

			if(m_curCommand == PROTH_SEARCH) // We were searching for first dust
			{
				setCurCommand(PROTH_NOTHING);
				on_skipButton_clicked();
			}

			lockTools(false);
		}

		if(m_curCommand == PROTH_NOTHING &&
		   m_pProcThread->getCommand() != PROTH_NOTHING) {

			changeCurCommand ( m_pProcThread->getCommand() );
		}

		TMAPP_printf(TMLOG_INFO, "m_curCommand=%s m_pProcThread.cmd=%s progress=%d\n",
				getCommandName(m_curCommand), getCommandName(m_pProcThread->getCommand()),
				m_pImgProc->getProgress())

		// If nothing changed, e.g. if an action is still running,
		// just update GUI for the progress
		if( m_curCommand == m_pProcThread->getCommand()
			|| m_pProcThread->getModeAuto()
			) {

			// Update Progress bar
			ui.overAllProgressBar->setValue( m_pImgProc->getProgress() );

			if(m_pProgressDialog &&
			   m_pProcThread->getModeAuto()) {
				TMAPP_printf(TMLOG_DEBUG, "auto mode => refresh progress bar : %d %%",
							 m_pImgProc->getProgress() )
			}

			// Do specific updates
			switch(m_curCommand) {
			default:
				if( m_pProcThread->getModeAuto()) {
					TMAPP_printf(TMLOG_DEBUG, "auto mode => refresh global image : %d %%",
								 m_pImgProc->getProgress() )

					// Refresh progression of marks on main display
					updateMainDisplay();
				}
				break;
			case PROTH_LOAD_FILE:
				#ifdef SIMPLE_VIEW
				ui.correctPixmapLabel->resize(ui.cropPixmapLabel->size().width(), ui.centralwidget->height());
				#endif

			case PROTH_PREPROC:
				// File is loaded and preproc

				// Check if input image is 16bit, disable the Inpaint button
				if(m_pImgProc->getOriginal() &&
						m_pImgProc->getOriginal()->depth != IPL_DEPTH_8U) {
					ui.inpaintButton->setEnabled(false);
				} else {
					ui.inpaintButton->setEnabled(true);
				}

				// refresh main because we may have missed the end of load
				refreshMainDisplay();
				// then update cropped
				updateDisplay();
				break;
			case PROTH_SEARCH:
				updateDisplay();
				break;
			}


			TMAPP_printf(TMLOG_DEBUG, "return at progress=%d %%",
						 m_pImgProc->getProgress() )
			return;
		}

		// If we were saving a file
		if( (m_curCommand == PROTH_SAVE_FILE)
			&& (m_pProcThread->getCommand() == PROTH_NOTHING)) {
			changeCurCommand(PROTH_NOTHING);
			// The timer will be stopped at next iteration and the GUI blocks will be enabled again
		}


		// If we WERE loading a file and now it's done
		if( (m_curCommand == PROTH_LOAD_FILE
			 || m_curCommand == PROTH_PREPROC
			 || m_curCommand == PROTH_OPTIONS
			)
			&& (m_pProcThread->getCommand() == PROTH_NOTHING
				|| m_pProcThread->getCommand() == PROTH_SEARCH)
		) {
			TMAPP_printf(TMLOG_INFO, "LOADING/PREPROC/SETOPTIONS Finished ")

			refreshMainDisplay();

			// update display for current dust
			// First check if a new dust if available
			updateDisplay();

			QFileInfo fi(m_currentFile);
			IplImage * curImage = m_pImgProc->getOriginal();
			if(!curImage) {
				statusBar()->showMessage( tr("Could not load file ")
					+ fi.fileName() );
			} else if(m_curCommand == PROTH_LOAD_FILE) {
				QString imginfo_str;
				imginfo_str.sprintf("%d x %d x %d", curImage->width, curImage->height, 8*tmByteDepth(curImage));
				imginfo_str+= tr(" bit");

				statusBar()->showMessage( tr("Loaded file. Image: ")
					+ fi.fileName() + tr(". Size:") + imginfo_str);
				ui.loadingTextLabel->setText( fi.fileName() );

				// Guess dpi by image size and ratio
				static bool do_guess_dpi = true;
				// Change resolution after reading in file
				tm_options l_options = m_pImgProc->getOptions();

				if(!do_guess_dpi) { // guess only once
					fprintf(stderr, "TamanoirApp::%s:%d : LOADING FINISHED => "
							"resolution=%d dpi (current=%d dpi)\n", __func__, __LINE__,
							l_options.dpi, g_options.dpi);

					m_pProcThread->setOptions(g_options);
				} else {
					do_guess_dpi = false; // no more guess
					int guess_dpi = 0;
					tm_film_size film_guess = getFilmType(curImage->width, curImage->height, &guess_dpi);

					float diff_dpi = fabsf( (float)(guess_dpi - l_options.dpi)*2.f
										   / (float)(guess_dpi + l_options.dpi));
					TMAPP_printf(TMLOG_INFO, "LOADING FINISHED => "
							"resolution in file=%d dpi (current=%d guess=%d dpi) => diff=%g %%\n",
							l_options.dpi, g_options.dpi, guess_dpi,
							100.f * diff_dpi)


					// lower than 300 dpi is the resolution of print, not of scan
					if( (l_options.dpi >0 && l_options.dpi<300
						&& g_options.dpi != l_options.dpi)

						|| (guess_dpi !=  0 &&
							diff_dpi > 0.4f // 40 % difference
							)
						) {
						QString dpistr, guess_str;
						dpistr.sprintf("%d", l_options.dpi);
						guess_str.sprintf("%d", guess_dpi);

						QMessageBox msgBox(
								QMessageBox::Question,
								tr("Tamanoir - Resolution mismatch"),
								tr("The resolution of ") + dpistr
										   + tr(" dpi, read in file or previously set, "
												"may be too low for the scanner resolution. "
												"It may be the print resolution. \n"
											  "Do you want to apply this resolution of ")
										   + dpistr + tr(" dpi read in file ? The resolution may probably be ")
										   + guess_str + tr(" dpi for ")
										   + QString(film_guess.format),
										   0
										   );
						QString fileDpiStr;
						fileDpiStr.sprintf("%d dpi", l_options.dpi);
						msgBox.addButton(fileDpiStr, QMessageBox::ActionRole);
						if(g_options.dpi != l_options.dpi) {
							QString optDpiStr;
							optDpiStr.sprintf("%d dpi", g_options.dpi);
							msgBox.addButton(optDpiStr, QMessageBox::ActionRole);
						}
						if(guess_dpi != l_options.dpi
						   && guess_dpi != g_options.dpi ) {
							QString guessDpiStr;
							guessDpiStr.sprintf("%d dpi", guess_dpi);
							msgBox.addButton(guessDpiStr, QMessageBox::ActionRole);
						}

						// Propose resolutions
						msgBox.exec();

						// Read user's choice
						QString answerStr = msgBox.clickedButton()->text();
						TMAPP_printf(TMLOG_INFO, "answer='%s'",
									 answerStr.toUtf8().data())

						int dpi = 2400;
						if(sscanf(answerStr.toUtf8().data(), "%d", &dpi) == 1)
						{
							l_options.dpi = dpi;

							TMAPP_printf(TMLOG_INFO, "ignore resolution of file => FORCE FORMER RESOLUTION => "
								"resolution=%d dpi => current=g_options.dpi=%d\n",
								l_options.dpi, g_options.dpi)

							// apply old resolution
	//						changeCurCommand (PROTH_OPTIONS);
							m_pProcThread->setOptions(l_options);
						}
					}
				}


				// We used a local resolution different from old one
				if( l_options.dpi > 0 ) {
					g_options.dpi = l_options.dpi;

					// update resolution button
					QString dpistr;
					dpistr.sprintf("%d", g_options.dpi);

					int ind = ui.dpiComboBox->findText(dpistr, Qt::MatchContains);
					if(ind >= 0) {
						TMAPP_printf(TMLOG_INFO, "Use supported resolution in l_options=%d dpi => combo index %d",
									 g_options.dpi,
									 ind)
						ui.dpiComboBox->setCurrentIndex(ind);

					} else { // add an item
						ui.dpiComboBox->insertItem(-1, dpistr + tr(" dpi"));
						TMAPP_printf(TMLOG_INFO, "Add a new resolution in l_options=%d dpi in combo",
									 g_options.dpi)

						ind = ui.dpiComboBox->findText(dpistr, Qt::MatchContains);
						if(ind >= 0) ui.dpiComboBox->setCurrentIndex(ind);
					}
				}
			}

			// If loading is finished,
			if(m_curCommand == PROTH_LOAD_FILE) {
				TMAPP_printf(TMLOG_INFO, "LOADING FINISHED => PREPROC "
						 "m_curCommand=%d m_pProcThread=%d\n",
						 m_curCommand, m_pProcThread->getCommand())

				m_pProcThread->runPreProcessing();
				changeCurCommand (PROTH_PREPROC);
			}
			else if(m_curCommand == PROTH_PREPROC) {
				ui.overAllProgressBar->setValue(0);
				changeCurCommand (PROTH_NOTHING);
				TMAPP_printf(TMLOG_DEBUG, "PRE-PROCESSING FINISHED ! "
						 "m_curCommand=%d m_pProcThread=%d",
						 m_curCommand, m_pProcThread->getCommand())

				m_current_dust.crop_width = 0;// to prevent from removing the first dust
				on_skipButton_clicked();
			} else {
				changeCurCommand (PROTH_NOTHING);
			}

/*			//
			if( (m_curCommand == PROTH_PREPROC
			   || m_curCommand == PROTH_OPTIONS)
			   && !m_pProcThread->getModeAuto()) {
				TMAPP_printf(TMLOG_INFO, "Load/option change finished, skipped to next dust : m_curCommand=%d m_pProcThread=%d",
					m_curCommand, m_pProcThread->getCommand());

				on_skipButton_clicked();
			}*/
		}
	}
}

void TamanoirApp::on_mainPixmapLabel_signalMouseMoveEvent(QMouseEvent * e) {
	on_mainPixmapLabel_signalMousePressEvent(e);
}



void TamanoirApp::on_mainPixmapLabel_signalMousePressEvent(QMouseEvent * e) {
	m_overCorrected = false;


	if(e && m_pImgProc) {
		// Keep current dust in mind to get back to it when done
		if(m_pProcThread && !force_mode) {
			m_pProcThread->insertCorrection(m_current_dust);

			if(g_debug_list) {
				//
				fprintf(stderr, "TamanoirApp::%s:%d : !force=> m_pProcThread->insertCorrection(current_dust=%d,%d)\n",
						__func__, __LINE__,
						m_current_dust.crop_x + m_current_dust.rel_seed_x,
						m_current_dust.crop_y + m_current_dust.rel_seed_y);
			}
		}

		// Indicate we are forcing the position of correction, and so
		// we must not store in skipped dusts list
		force_mode = true;

		IplImage * origImage = m_pImgProc->getGrayscale();
		if(!origImage)
			return;
		IplImage * displayImage = m_pImgProc->getDisplayImage();


		//int scaled_width = m_main_display_rect.width()-12;
		//int scaled_height = m_main_display_rect.height()-12;
		int scaled_width = displayImage->width;
		int scaled_height = displayImage->height;

		float scale_x = (float)origImage->width / (float)scaled_width;
		float scale_y = (float)origImage->height / (float)scaled_height;

		//fprintf(stderr, "TamanoirApp::%s:%d : e=%d,%d x scale=%gx%g\n", __func__, __LINE__,
		//		e->pos().x(), e->pos().y(), scale_x, scale_y);

		// Create a fake dust in middle
		int crop_w = m_blockSize.width; //ui.cropPixmapLabel->size().width()-2;
		int crop_h = m_blockSize.height; //ui.cropPixmapLabel->size().height()-2;
		int offset_x = (ui.mainPixmapLabel->size().width() - scaled_width)/2;// pixmap is centered
		int offset_y = (ui.mainPixmapLabel->size().height() - scaled_height)/2;// pixmap is centered

		memset(&m_current_dust, 0, sizeof(t_correction));
		m_current_dust.crop_x = std::max(0, (int)roundf( (e->pos().x()-offset_x) * scale_x) -crop_w/2);
		m_current_dust.crop_y = std::max(0, (int)roundf( (e->pos().y()-offset_y) * scale_y) -crop_h/2);
		m_current_dust.crop_width = crop_w;
		m_current_dust.crop_height = crop_h;

		// Limit to right and bottom
		if(m_current_dust.crop_x + m_current_dust.crop_width >= origImage->width) {
			m_current_dust.crop_x = std::max(0, origImage->width - m_current_dust.crop_width-1);
		}
		if(m_current_dust.crop_y + m_current_dust.crop_height >= origImage->height) {
			m_current_dust.crop_y = std::max(0, origImage->height - m_current_dust.crop_height-1);
		}

		// Clip
		m_current_dust.rel_src_x = m_current_dust.rel_dest_x = crop_w / 2;
		m_current_dust.rel_src_y = m_current_dust.rel_dest_y = crop_h / 2;
		m_current_dust.rel_dest_y += 20;
		m_current_dust.copy_width = m_current_dust.copy_height = 16;
		m_current_dust.area = 1;

		m_pImgProc->setCopySrc(&m_current_dust, crop_w / 2, crop_h / 2);

		updateDisplay();
	}
}

/*
 * Mark current block in red
 */
void TamanoirApp::on_markButton_clicked() {
	fprintf(stderr, "%s:%d ...\n", __func__, __LINE__);
	if(m_pImgProc) {

		IplImage * origImage = m_pImgProc->getGrayscale();
		if(!origImage)
			return;

		IplImage * displayImage = m_pImgProc->getStillDisplayImage();


		//int scaled_width = m_main_display_rect.width()-12;
		//int scaled_height = m_main_display_rect.height()-12;
		int scaled_width = displayImage->width;
		int scaled_height = displayImage->height;

		float scale_x = (float)scaled_width / (float)origImage->width;
		float scale_y = (float)scaled_height / (float)origImage->height;

		//fprintf(stderr, "TamanoirApp::%s:%d : e=%d,%d x scale=%gx%g\n", __func__, __LINE__,
		//		e->pos().x(), e->pos().y(), scale_x, scale_y);

		// Create a fake dust in middle
		int crop_w = m_current_dust.crop_width; //ui.cropPixmapLabel->size().width()-2;
		int crop_h = m_current_dust.crop_height; //ui.cropPixmapLabel->size().height()-2;
		fprintf(stderr, "TamanoirApp::%s:%d : mark current %d,%d +%dx%d "
				"=> %g,%g+%gx%g\n",
				__func__, __LINE__,
				m_current_dust.crop_x ,
				m_current_dust.crop_y ,
				crop_w ,
				crop_h ,
				m_current_dust.crop_x * scale_x,
				m_current_dust.crop_y * scale_y,
				crop_w * scale_x,
				crop_h * scale_y);

		// Mark rectangle in image
		tmMarkFailureRegion(displayImage,
			m_current_dust.crop_x * scale_x,
			m_current_dust.crop_y * scale_y,
			crop_w * scale_x,
			crop_h * scale_y,
			COLORMARK_FAILED);

		// Then update
		updateDisplay();
	}

}

void TamanoirApp::moveBlock() {
	if(!m_pImgProc) return;
	CvSize blockSize = m_pImgProc->getDisplayCropSize();

/*	fprintf(stderr, "[TamanoirApp]::%s:%d block:%d,%d / blocks of size %dx%d\n", __func__, __LINE__,
			m_nav_x_block, m_nav_y_block,
			blockSize.width, blockSize.height
			); */

	if(blockSize.width <= 0 || blockSize.height <= 0) return;

	IplImage * origImage = m_pImgProc->getGrayscale();
	if(!origImage) return;


	if(blockSize.height * m_nav_y_block > origImage->height) {
		// move to right
		if((m_nav_x_block+1)*blockSize.width < origImage->width) {
			m_nav_y_block = 0;
			m_nav_x_block++;
		} else { // nothing to do, we'are already at bottom-right of image
			// just prevent y to be larger than image
			m_nav_y_block = origImage->height/blockSize.height;
		}
	}

	if(m_nav_y_block<0) {
		if(m_nav_x_block>0) {
			m_nav_x_block--;
			// and move y to bottom
			m_nav_y_block = origImage->height / blockSize.height;
		}
	}
	// Create a fake dust in middle
	int crop_w = blockSize.width;
	int crop_h = blockSize.height;

	memset(&m_current_dust, 0, sizeof(t_correction));
	m_current_dust.crop_x = crop_w * m_nav_x_block;
	m_current_dust.crop_y = crop_h * m_nav_y_block;

	// Create a dummy dust replacement here
	m_current_dust.rel_src_x = m_current_dust.rel_dest_x = crop_w / 2;
	m_current_dust.rel_src_y = m_current_dust.rel_dest_y = crop_h / 2;
	m_current_dust.rel_dest_x += 20;
	m_current_dust.rel_dest_y += 20;
	m_current_dust.copy_width = m_current_dust.copy_height = 16;
	m_current_dust.area = 1;

	m_pImgProc->setCopySrc(&m_current_dust, crop_w / 2, crop_h / 2);

	updateDisplay();

}



void TamanoirApp::on_topLeftButton_clicked() {
	m_nav_x_block = m_nav_y_block = 0;
	moveBlock();
}

void TamanoirApp::on_pageDownButton_clicked() {
	m_nav_y_block++;
	moveBlock();
}
void TamanoirApp::on_pageUpButton_clicked() {
	m_nav_y_block--;
	moveBlock();
}

void TamanoirApp::on_undoButton_clicked() {
	if(m_pImgProc) {
		fprintf(stderr, "TamanoirApp::%s:%d : UNDO\n", __func__, __LINE__);
		m_pImgProc->undo();
		updateDisplay();
	}
}
void TamanoirApp::on_dustInfoButton_toggled(bool state) {
	if(state)
		ui.infoFrame->show();
	else
		ui.infoFrame->hide();
	fprintf(stderr, "TamanoirApp::%s:%d : show info = %s\n",
			__func__, __LINE__, state?"TRUE":"FALSE");
}


void TamanoirApp::on_searchCloneSrcCheckBox_toggled(bool state) {
	m_searchCloneSrc = state;
	fprintf(stderr, "TamanoirApp::%s:%d : m_searchCloneSrc = %s\n",
			__func__, __LINE__, m_searchCloneSrc?"TRUE":"FALSE");
}

void TamanoirApp::on_inpaintButton_toggled(bool state) {
	m_draw_on = (state ? TMMODE_INPAINT: TMMODE_NOFORCE);

	if(state) {
		ui.cloneButton->blockSignals(true);
		ui.cloneButton->setChecked(false);
		ui.cloneButton->blockSignals(false);
	}

	switch(m_draw_on) {
	default:
	case TMMODE_NOFORCE:
		ui.cropPixmapLabel->setCursor( Qt::ArrowCursor );
		break;
	case TMMODE_CLONE:
		ui.cropPixmapLabel->setCursor( Qt::CrossCursor );
		break;
	case TMMODE_INPAINT: {

		m_current_dust.copy_width =
			m_current_dust.copy_height = 9;

		// Update cursor for drawing a small circle
		updateCroppedCursor();

		//
		if(m_pImgProc) {
			m_pImgProc->setCopySrc(&m_current_dust, m_current_dust.rel_seed_x, m_current_dust.rel_seed_y);
		}
		updateDisplay();//to remove the arrow
		}break;
	}

	updateDisplay();
}

void TamanoirApp::on_cloneButton_toggled(bool state) {

	m_draw_on = (state ? TMMODE_CLONE : TMMODE_NOFORCE);
	if(state) {
		ui.inpaintButton->blockSignals(true);
		ui.inpaintButton->setChecked(false);
		ui.inpaintButton->blockSignals(false);
	}

	switch(m_draw_on) {
	default:
	case TMMODE_NOFORCE:
		ui.cropPixmapLabel->setCursor( Qt::ArrowCursor );
		break;
	case TMMODE_CLONE:
		ui.cropPixmapLabel->setCursor( Qt::CrossCursor );
		if(m_pImgProc) {
			m_pImgProc->setCopySrc(&m_current_dust, m_current_dust.rel_seed_x, m_current_dust.rel_seed_y);
		}
		updateDisplay();//to remove the arrow
		break;
	case TMMODE_INPAINT:
		ui.cropPixmapLabel->setCursor( Qt::WhatsThisCursor );
		break;
	}
}








// return distance to a corrected dust
int distToEllipse(t_correction current_dust, int mouse_x, int mouse_y) {
	//
	float dx = (float)(mouse_x - current_dust.rel_src_x);
	float dy = (float)(current_dust.rel_src_y - mouse_y);
	float radius_x = current_dust.copy_width*0.5f;
	if(radius_x < 1) return 100;
	float radius_y = current_dust.copy_height*0.5f;
	if(radius_y < 1) return 100;

	double theta = atan2(dy/radius_y, dx/radius_x); // angle center->mouse

	float xborder = cos(theta)*radius_x; // position of ellipse in this direction
	float yborder = sin(theta)*radius_y;

	float bx = dx-xborder;
	float by = dy-yborder;

	float dist = sqrtf(bx*bx+by*by);
/*
	fprintf(stderr, "%s:%d: dx,y=%g,%g th=%g=%g deg / A=%g, B=%g border=%g,%g => d=%g,%g  dist %g\n",
			__func__, __LINE__,
		dx, dy, theta, theta*180.f/CV_PI,
		radius_x, radius_y,
		xborder, yborder,
		bx, by, dist);
*/
	return (int)dist;
}


// return distance to a corrected dust
int radiusOfEllipse(t_correction current_dust, int mouse_x, int mouse_y) {
	//
	float dx = (float)(mouse_x - current_dust.rel_src_x);
	float dy = (float)(mouse_y - current_dust.rel_src_y);
	float radius_x = current_dust.copy_width*0.5f; if(radius_x < 1) return 100;
	float radius_y = current_dust.copy_height*0.5f; if(radius_y < 1) return 100;
	double theta = atan2(-dy/radius_y, dx/radius_x);
	float xborder = cos(theta)*radius_x;
	float yborder = -sin(theta)*radius_y;

	float dist = sqrtf(xborder*xborder+yborder*yborder);
	return (int)dist;
}
// return distance to a corrected dust
int distToEllipseCenter(t_correction current_dust, int mouse_x, int mouse_y) {
	//
	float dx = (float)(mouse_x - current_dust.rel_src_x);
	float dy = (float)(mouse_y - current_dust.rel_src_y);

	float dist = sqrtf(dx*dx+dy*dy);
	return (int)dist;
}

int distantToRect(t_correction current_dust, int mouse_x, int mouse_y) {
	int dist_to_border = 100;
		int border_tolerance = 5;

	// Distance to border of source rectangle
	if(		mouse_x >= current_dust.rel_src_x-current_dust.copy_width/2 -border_tolerance
		&& mouse_x <= current_dust.rel_src_x+current_dust.copy_width/2 +border_tolerance
		&& mouse_y >= current_dust.rel_src_y-current_dust.copy_height/2-border_tolerance
		&& mouse_y <= current_dust.rel_src_y+current_dust.copy_height/2+border_tolerance ) {

		int dx = tmmin( abs(current_dust.rel_src_x+current_dust.copy_width/2 - mouse_x),
					abs(current_dust.rel_src_x-current_dust.copy_width/2 - mouse_x) );

		int dy = tmmin( abs(current_dust.rel_src_y+current_dust.copy_height/2 - mouse_y),
					abs(current_dust.rel_src_y-current_dust.copy_height/2 - mouse_y) );

		dist_to_border = tmmin(	dx, dy );
	}

	return dist_to_border;
}








void TamanoirApp::on_cropPixmapLabel_signalMouseReleaseEvent(QMouseEvent * ) {
	m_overCorrected = false;
	if(m_pImgProc) {
		m_pImgProc->lockInpaintDrawing(false);
		if(m_draw_on == TMMODE_INPAINT) {
			ui.cropPixmapLabel->setCursor(Qt::WaitCursor);

			updateDisplay(); // compute inpainting
			// restor cursor
			updateCroppedCursor();
		}
	}

	//fprintf(stderr, "TamanoirApp::%s:%d : ...\n", __func__, __LINE__);
	cropPixmapLabel_last_button = Qt::NoButton;
}

void TamanoirApp::on_cropPixmapLabel_signalMousePressEvent(QMouseEvent * e) {
	m_unsaved_changes = true;

	//fprintf(stderr, "TamanoirApp::%s:%d : ...\n", __func__, __LINE__);
	if(e && m_pProcThread && m_pImgProc) {
		int dist_to_border = 100;

		int mouse_x = e->pos().x();
		int mouse_y = e->pos().y();

		dist_to_border = distToEllipse(m_current_dust, mouse_x, mouse_y);


		// Dist to destination
		int dx_dest = abs(e->pos().x() - (m_current_dust.rel_dest_x ));
		int dy_dest = abs(e->pos().y() - (m_current_dust.rel_dest_y ));
		float dist_dest = sqrt((float)(dx_dest*dx_dest + dy_dest*dy_dest ));
		int dist_to_dest = (int)dist_dest;

		// Dist to source
		int dx_src = abs(e->pos().x() - (m_current_dust.rel_src_x ));
		int dy_src = abs(e->pos().y() - (m_current_dust.rel_src_y ));
		float dist_src = sqrt((float)(dx_src*dx_src + dy_src*dy_src ));
		int dist_to_src = (int)dist_src;

		//fprintf(stderr, "TamanoirApp::%s:%d : dist to border=%d / src=%d / dest=%d\n",
		//		__func__, __LINE__, dist_to_border, dist_to_src, dist_to_src );
		switch(e->button()) {
		case Qt::NoButton:
			//fprintf(stderr, "TamanoirApp::%s:%d : NoButton...\n", __func__, __LINE__);
			cropPixmapLabel_last_button = Qt::NoButton;
			ui.cropPixmapLabel->setCursor( Qt::ArrowCursor );
			m_resize_rect = false;
			return;
			break;
		case Qt::LeftButton: {
			if(!m_draw_on) {
				// Check if the click is near the border of the rectangle
				if( dist_to_border <= 5
					&& dist_to_border <= dist_to_src) {

					cropPixmapLabel_last_button = Qt::RightButton; // e->button();
					ui.cropPixmapLabel->setCursor( Qt::SizeFDiagCursor);


					float ell_x = e->pos().x() - m_current_dust.rel_src_x;
					float ell_y = m_current_dust.rel_src_y - e->pos().y(); // in trigo reference


					float ell_A = (float)m_current_dust.copy_width/2.f;
					float ell_B = (float)m_current_dust.copy_height/2.f;

					/*
					  ell_x = A cos (th)
					  ell_y = B sin (th)
					  => since we known ell_x, ell_y, theta
						A = ell_x / cos (th)
						B = ell_y / sin (th)
					  */
/*					float ell_dist = sqrtf( ell_x*ell_x + ell_y*ell_y );
					float cos_th = ell_x / ell_dist;
					float sin_th = ell_y / ell_dist;
					float theta = atan2(ell_y/ell_B, ell_x/ell_A);

					fprintf(stderr, "%s:%d : in ellipse : x,y=%g,%g th=%g A,B=%g,%g\n",
							__func__, __LINE__,
							ell_x,ell_y, theta,
							ell_A, ell_B);
*/
					// get coef
					if(m_current_dust.copy_width > 0)
						m_resize_rect_xscale = 2.f*(float)ell_A/(float)fabsf(ell_x);
					if(m_current_dust.copy_height > 0)
						m_resize_rect_yscale = 2.f*(float)ell_B/(float)fabsf(ell_y);
					m_resize_rect = true;

//					fprintf(stderr, "%s:%d : FIXME : bad formula : resize ellipse => scale %g,%g\n",
//							__func__ , __LINE__,m_resize_rect_xscale, m_resize_rect_yscale );

					return;
				} else {
					m_resize_rect = false;
				}
			}


			if(tmmin(dist_to_src, dist_to_dest) < tmmin(dist_to_border, 50)) {
				if(dist_src < dist_dest) {
					// Move src
					is_src_selected = true;
				} else {
					// Move dest
					is_src_selected = false;
				}

				if(!m_draw_on) {
					if(is_src_selected) { // use hand for source
						ui.cropPixmapLabel->setCursor( Qt::ClosedHandCursor );
					} else { // and cross for dest
						ui.cropPixmapLabel->setCursor( Qt::CrossCursor );
					}
				}
			}

			if(m_draw_on) {
				// Move dest
				is_src_selected = false;
			}


			cropPixmapLabel_last_button = e->button();
			if(is_src_selected) {
				// Move src
				m_current_dust.rel_src_x = e->pos().x();
				m_current_dust.rel_src_y = e->pos().y();

				if(g_debug_displaylabel) {
					// FIXME : display info on source

				}

			} else {

				if(m_draw_on == TMMODE_CLONE) {

					/** No search when we click = click=apply clone **/
					m_pImgProc->applyCorrection(m_current_dust, true);
				} else {
					// Move dest only if not in search mode
					m_current_dust.rel_dest_x = m_current_dust.rel_seed_x = e->pos().x();
					m_current_dust.rel_dest_y = m_current_dust.rel_seed_y = e->pos().y();
					// draw in inpainting mask
					m_pImgProc->lockInpaintDrawing(true);
					m_pImgProc->drawInpaintCircle(m_current_dust);
					/** No search when we click = click=apply inpainting **/
					//m_pImgProc->applyInpainting(current_dust, true);
				}
			}

			int center_x = m_current_dust.rel_src_x;
			int center_y = m_current_dust.rel_src_y;
			m_pImgProc->setCopySrc(&m_current_dust,
				center_x, center_y);
			updateDisplay();
			}
			break;
		case Qt::RightButton: { // Right is for moving border
			cropPixmapLabel_last_button = Qt::NoButton;
			if(m_draw_on) {	// Move src
				m_current_dust.rel_src_x = e->pos().x();
				m_current_dust.rel_src_y = e->pos().y();
			} else {
				// Check if the click is near the rectangle
				int dist_to_border2 = distToEllipse(m_current_dust, e->pos().x(), e->pos().y());

				if(dist_to_border2 <= 5) {
					cropPixmapLabel_last_button = e->button();
					ui.cropPixmapLabel->setCursor( Qt::SizeFDiagCursor);
				}
			}
			} break;
		default:
			//fprintf(stderr, "TamanoirApp::%s:%d : Button = %d...\n", __func__, __LINE__,
			//	(int)e->button());

			break;
		}
	}
}


void TamanoirApp::on_cropPixmapLabel_signalMouseMoveEvent(QMouseEvent * e) {
	m_overCorrected = false;

	if(e && m_pProcThread && m_pImgProc) {

		int dist_to_border = 100;

		int mouse_x = e->pos().x();
		int mouse_y = e->pos().y();
		int border_tolerance = 5;

		dist_to_border = distToEllipse(m_current_dust, mouse_x, mouse_y);


		// Dist to destination
		int dx_dest = abs(mouse_x - m_current_dust.rel_dest_x );
		int dy_dest = abs(mouse_y - m_current_dust.rel_dest_y );
		int dist_to_dest = sqrtf(dx_dest*dx_dest +  dy_dest *dy_dest );

		// Dist to source
		int dx_src = abs(mouse_x - m_current_dust.rel_src_x );
		int dy_src = abs(mouse_y - m_current_dust.rel_src_y );
		int dist_to_src = sqrtf( dx_src*dx_src+ dy_src *dy_src);



		switch(cropPixmapLabel_last_button) {
		default:
		case Qt::NoButton: { // We just move the mouse without clicking
			if(!m_draw_on) { // not drawing = maybe just resizing the source or moving source/dest point
				if( dist_to_border <= border_tolerance
					&& dist_to_border <= dist_to_src) {
					ui.cropPixmapLabel->setCursor( Qt::SizeFDiagCursor);
					return;
				}

				int dist_to_center = radiusOfEllipse(m_current_dust, mouse_x, mouse_y);
				// First check if click is closer to src or dest
				if(tmmin(dist_to_src, dist_to_dest)
					< dist_to_center/2
							) {
					if(dist_to_src<dist_to_dest)
						ui.cropPixmapLabel->setCursor( Qt::OpenHandCursor);
					else
						ui.cropPixmapLabel->setCursor( Qt::CrossCursor);
				}
				else {
					ui.cropPixmapLabel->setCursor( Qt::ArrowCursor);
				}


			} else { // we try to clone or to inpaint
				m_current_dust.rel_dest_x = m_current_dust.rel_seed_x = mouse_x;
				m_current_dust.rel_dest_y = m_current_dust.rel_seed_y = mouse_y;

				// Compute correction
				//g_debug_imgverbose = 255;
				//g_debug_correlation = 255;
				if((m_draw_on == TMMODE_CLONE && m_searchCloneSrc)
				   //||(m_draw_on == TMMODE_INPAINT)
				   ) {
					t_correction search_correct = m_current_dust;
					u8 old_debug = g_debug_imgverbose, old_correlation = g_debug_correlation;

					int ret = m_pImgProc->findDust(m_current_dust.crop_x+m_current_dust.rel_seed_x,
											m_current_dust.crop_y+m_current_dust.rel_seed_y,
											&search_correct,
											m_draw_on /* used to prevent from correcting automatically in trust mode */);
					g_debug_imgverbose = old_debug;
					g_debug_correlation = old_correlation;

					if(ret > 0) {
						/*
						fprintf(stderr, "TamanoirApp::%s:%d : Seed = %d, %d => ret=%d\n", __func__, __LINE__,
								current_dust.crop_x+current_dust.rel_seed_x ,
								current_dust.crop_y+current_dust.rel_seed_y ,
								ret);
								*/
						m_current_dust = search_correct;

						//m_pImgProc->applyCorrection(search_correct, true);
					}
				}
			}

			int center_x = m_current_dust.rel_src_x;
			int center_y = m_current_dust.rel_src_y;

			m_pImgProc->setCopySrc(&m_current_dust,
					center_x, center_y);
			updateDisplay();

			}break;
		case Qt::LeftButton:
			{
			int dx = m_current_dust.rel_seed_x - m_current_dust.rel_src_x;
			int dy = m_current_dust.rel_seed_y - m_current_dust.rel_src_y;

			/*fprintf(stderr, "TamanoirApp::%s:%d : Seed = %d, %d => ret=%d\n", __func__, __LINE__,
							current_dust.crop_x+current_dust.rel_seed_x ,
							current_dust.crop_y+current_dust.rel_seed_y ,
							0);*/
			if(is_src_selected) {
				// Move src
				m_current_dust.rel_src_x = mouse_x;
				m_current_dust.rel_src_y = mouse_y;
			} else {
				// Move dest
				m_current_dust.rel_dest_x = mouse_x;
				m_current_dust.rel_dest_y = mouse_y;
			}

			if(m_draw_on == TMMODE_CLONE) {

				// Get move from last position
				m_current_dust.rel_seed_x = m_current_dust.rel_dest_x = mouse_x;
				m_current_dust.rel_seed_y = m_current_dust.rel_dest_y = mouse_y;

				m_current_dust.rel_src_x = m_current_dust.rel_seed_x - dx;
				m_current_dust.rel_src_y = m_current_dust.rel_seed_y - dy;

				/* No search when moving with the button down
				just apply current src->dest correction */
				m_pImgProc->applyCorrection(m_current_dust, true);

			} else if(m_draw_on == TMMODE_INPAINT) {
				m_current_dust.rel_seed_x = m_current_dust.rel_dest_x = mouse_x;
				m_current_dust.rel_seed_y = m_current_dust.rel_dest_y = mouse_y;
				// draw in inpainting mask
				m_pImgProc->lockInpaintDrawing(true);
				m_pImgProc->drawInpaintCircle(m_current_dust);
			}

			int center_x = m_current_dust.rel_src_x;
			int center_y = m_current_dust.rel_src_y;
			m_pImgProc->setCopySrc(&m_current_dust,
				center_x, center_y);
			updateDisplay();
			}break;

		case Qt::RightButton: { // Resize rectangle
			int center_x = m_current_dust.rel_src_x;
			int center_y = m_current_dust.rel_src_y;

			int dest_x = m_current_dust.rel_dest_x;
			int dest_y = m_current_dust.rel_dest_y;

			m_current_dust.rel_dest_x = dest_x;
			m_current_dust.rel_dest_y = dest_y;

			m_current_dust.copy_width = tmmax(1, (int)roundf(m_resize_rect_xscale*fabs(center_x - e->pos().x())));
			m_current_dust.copy_height = tmmax(2, (int)roundf(m_resize_rect_yscale*fabs(center_y - e->pos().y())));

			m_pImgProc->setCopySrc(&m_current_dust,
				center_x, center_y);
			updateDisplay();
			}break;
		}
	}
}

void TamanoirApp::on_correctPixmapLabel_signalFocusInEvent(QFocusEvent * e) {
	if(e && m_pProcThread && m_pImgProc) {
		m_pImgProc->showDebug(true);
		fprintf(stderr, "TmApp::%s:%d : focus in \n", __func__, __LINE__);
		m_overCorrected = true;

		updateDisplay();
	}
}
void TamanoirApp::on_correctPixmapLabel_signalFocusOutEvent(QFocusEvent * e) {
	if(e && m_pProcThread && m_pImgProc) {
		m_pImgProc->showDebug(true);

		fprintf(stderr, "TmApp::%s:%d : focus out \n", __func__, __LINE__);
		m_overCorrected = false;

		updateDisplay();
	}
}

void TamanoirApp::on_correctPixmapLabel_signalMouseMoveEvent(QMouseEvent * e) {

	if(e && m_pProcThread && m_pImgProc) {
		m_pImgProc->showDebug(true);
		m_overCorrected = false;
		int mouse_x = e->pos().x();
		int mouse_y = e->pos().y();

		if( abs(mouse_x - ui.correctPixmapLabel->width() /2)< (ui.correctPixmapLabel->width() /2)-20
			&& abs(mouse_y - ui.correctPixmapLabel->height() /2)< (ui.correctPixmapLabel->height() /2) -20) {
			m_overCorrected = true;
		}

		updateDisplay();
	}
}


void TamanoirApp::updateCroppedCursor()
{

	if(m_draw_on == TMMODE_INPAINT)
	{
		ui.cropPixmapLabel->showCopyVector(false);
		t_correction * l_correction = &m_current_dust;
		int radius = tmmin(l_correction->copy_width,
						   l_correction->copy_height)/2 ;

		if(radius < 1) radius = 1;

		l_correction->copy_width  =
			l_correction->copy_height = radius*2+1;

		// update cursor
		QPixmap pixmap(2*radius+1, 2*radius+1);
		QPainter painter(&pixmap);
		painter.eraseRect(0,0, 2*radius+1, 2*radius+1 );
		QPoint center(radius, radius);
		painter.setPen( QPen(QBrush(qRgb(0,0,0)), 1) );
		painter.drawEllipse( center, radius, radius);
		painter.drawEllipse( center, radius-1, radius-1);

		QPixmap pixmap_bg(pixmap);
		QPoint centerW(radius, radius);
		painter.setPen( QPen(QBrush(qRgb(255,255,255)), 1) );
		painter.drawEllipse( centerW, radius, radius);
		QCursor bmpcursor(pixmap, pixmap_bg, radius, radius);
		ui.cropPixmapLabel->setCursor( bmpcursor);
	}
	else {
		// restore the show copy vector
		ui.cropPixmapLabel->showCopyVector(true);
	}
}


void TamanoirApp::on_cropPixmapLabel_signalWheelEvent(QWheelEvent * e) {
	if(e && m_pProcThread) {
		t_correction * l_correction = &m_current_dust;
		int numDegrees = e->delta() / 8;
		int numSteps = numDegrees / 15;

		int inc = -numSteps * 2;

		int center_x = l_correction->rel_src_x;
		int center_y = l_correction->rel_src_y;


		if(inc < 0) {
			if(l_correction->copy_width<=2 || l_correction->copy_height<=2)
				return;
		}

		l_correction->copy_width  += inc;
		l_correction->copy_height += inc;
		updateCroppedCursor();

		if(m_draw_on != TMMODE_INPAINT) {
			// This function only update l_correction
			m_pImgProc->setCopySrc(l_correction,
				center_x,
				center_y);

			updateDisplay();
		}
	}
}



void TamanoirApp::setArgs(int argc, char **argv) {
	ui.loadingTextLabel->setText(QString(""));
	statusBar()->showMessage( QString("") );
	bool open_load_dialog = false;

	QString argsStr = tr("Args=");
	if(argc > 1) {
		for(int arg=1; arg<argc; arg++) {

			if(argv[arg][0] == '-') //option
			{
				fprintf(stderr, "TamanoirApp::%s:%d option '%s'\n",
					__func__, __LINE__, argv[arg]);
				if(strcasestr(argv[arg], "debug")) // All debug options
				{
					TMAPP_printf(TMLOG_INFO, "activate DEBUG + g_debug_savetmp = g_debug_imgverbose = g_debug_dust_seek = 1")
					g_debug_savetmp = g_debug_imgverbose = g_debug_dust_seek = 1;
					g_debug_TamanoirApp = TMLOG_DEBUG;
				}

				if(strcasestr(argv[arg], "trace")) // All debug options
				{
					TMAPP_printf(TMLOG_INFO, "activate TRACE + g_debug_savetmp = g_debug_imgverbose = 1")
					g_debug_savetmp = g_debug_imgverbose = 1;
					g_debug_TamanoirApp = TMLOG_TRACE;
				}
				if(strcasestr(argv[arg], "save")) // All debug options
					g_debug_savetmp = 1;

			} else {
				QFileInfo fi(argv[arg]);
				if(fi.exists()) {
					if( loadFile( argv[arg] ) >= 0) {
						// don't open the file
						open_load_dialog = false;
					}
				}
			}


			argsStr += QString(argv[arg]);
		}

		statusBar()->showMessage( argsStr );
	}

#ifdef SIMPLE_VIEW
	if(open_load_dialog) {
		on_loadButton_clicked ();
	}
#endif

}



void TamanoirApp::setCurCommand(int cmd) {
	TMAPP_printf(TMLOG_INFO, "Change current command : %d=%s => %d=%s",
				 m_curCommand, g_command_names[m_curCommand],
				 cmd, g_command_names[cmd]);
	m_curCommand = cmd;
}

int TamanoirApp::loadFile(QString s) {
	if(m_fileDialog) { // Hide dialoag
		m_fileDialog->hide();
		m_fileDialog->close();
	}

	QFileInfo fi(s);
	if(!fi.exists()) {
		TMAPP_printf(TMLOG_ERROR, "File '%s' does not exists",
					 s.toUtf8().constData())
		return -1;
	}
	ui.loadingTextLabel->setText(tr("Loading ") + s + " ...");
	show();

	statusBar()->showMessage( tr("Loading and pre-processing ")
							  + m_currentFile + QString("..."));
	statusBar()->update();

	m_currentFile = s;
	QByteArray homedir_utf8 = fi.absolutePath().toUtf8();
	strcpy(g_display_options.currentDir, homedir_utf8.data());

	// save current options to load next time from the same dircetory
	saveOptions();

	// Clear known dusts list
	memset(&m_current_dust, 0, sizeof(t_correction));
	skipped_list.clear();


	TMAPP_printf(TMLOG_INFO, "loading file='%s'...\n", s.toUtf8().data())
	// Open file
	if(!m_pImgProc) {
		m_pImgProc = new TamanoirImgProc( ui.cropPixmapLabel->width() -2,
										  ui.cropPixmapLabel->height() -2);

		refreshMainDisplay();

		m_pImgProc->setOptions(g_options);
	}

	if(!m_pProcThread) { // Create background processing thread
		m_pProcThread = new TamanoirThread(m_pImgProc);
	}


	// Use background thread to load the file without locking the GUI
	int ret = m_pProcThread->loadFile( s );
	if(ret < 0) {
		QMessageBox::critical( 0, tr("Tamanoir"),
			tr("Cannot load file ") + s + tr(". Format or compression is not compatible"));

		return -1;
	}

	changeCurCommand(PROTH_LOAD_FILE);


	m_unsaved_changes = false;

	// Lock tool frame
	lockTools(true);

	// Refresh timer while loading
	refreshTimer.start(TMAPP_TIMEOUT);

	return 0;
}


void TamanoirApp::lockTools(bool lock) {
	ui.cropPixmapLabel->setDisabled(lock);
	ui.correctPixmapLabel->setDisabled(lock);
	ui.toolFrame->setDisabled(lock);
	ui.saveButton->setDisabled(lock);
}

/****************************** Button slots ******************************/
void TamanoirApp::on_actionOpen_2_activated() {

	on_loadButton_clicked();
}

void TamanoirApp::on_actionQuit_activated() {
	if(!m_unsaved_changes) {
		exit(0);
	}
	// May want to save ?
	int ret = QMessageBox::question(this,
				   tr("Tamanoir - Save before quit ?"),
				   tr("There are unsaved changes, do you want to save before quitting ?") ,
				   QMessageBox::Ok,
				   QMessageBox::Cancel);

	if(ret == QMessageBox::Ok) {
		on_saveButton_clicked();
	}
	// Bye
	exit(0);
}

void TamanoirApp::on_actionSave_activated() {
	on_saveButton_clicked();
}

void TamanoirApp::on_loadButton_clicked()
{
	if(!m_fileDialog) {
		m_fileDialog = new QFileDialog(this,
						tr("Tamanoir - Open a picture for cleaning"),
						g_display_options.currentDir,
						tr("Images (*.png *.p*m *.xpm *.jp* *.tif* *.bmp *.cr2"
							"*.PNG *.P*M *.XPM *.JP* *.TIF* *.BMP *.CR2)"));
		m_fileDialog->setFileMode(QFileDialog::ExistingFile);
	}

	m_fileDialog->show();
	QStringList fileNames;
	if (m_fileDialog->exec())
		fileNames = m_fileDialog->selectedFiles();
	else
		return;

	//fprintf(stderr, "TamanoirApp::%s:%d : ...\n", __func__, __LINE__);
	QString s = fileNames.at(0);
	if(s.isEmpty()) {
		//fprintf(stderr, "TamanoirApp::%s:%d : cancelled...\n", __func__, __LINE__);
		return;
	}


	loadFile( s);
}


void TamanoirApp::on_saveButton_clicked()
{
	if(!m_pImgProc) {
		// error, nothing to be saved
		fprintf(stderr, "TamanoirApp::%s:%d : cannot save, no image proc object \n", __func__, __LINE__);
		return;
	}
	fprintf(stderr, "TamanoirApp::%s:%d : saving in original file, and use a copy for backup...\n", __func__, __LINE__);

	QFileInfo fi(m_currentFile);
	QString ext = fi.completeSuffix();
	QString base = fi.completeBaseName();

	m_unsaved_changes = false;

	if(g_display_options.export_layer)
	{
		// FIXME : ask to save layer as PNG ?
		QString strname = base + tr("-mask") + ".png";
		int ret = QMessageBox::question(this,
					   tr("Tamanoir - Save dust layer as image ?"),
					   tr("Do you want to save the dust layer mask as ") + strname
					   + tr(" ?"),
					   QMessageBox::Ok,
					   QMessageBox::Cancel);

		if(ret == QMessageBox::Ok) {
			// Save file with standard cvSaveImage for PNG
			QString pathmask = fi.absolutePath () + "/" + strname;
			IplImage * maskImg = m_pImgProc->getDustMask();
			fprintf(stderr, "TmApp::%s:%d saving mask %p as '%s'",
					__func__, __LINE__,
					maskImg,
					pathmask.toUtf8().data());
			if(maskImg) {
				tmSaveImage(pathmask.toUtf8().data(), maskImg);
			}
		}
	}

	// Save a copy before saving output image
	QString copystr = base + tr("-orig.") + ext;
	if(m_pImgProc) {
		QString msg = tr("Saving ") + m_currentFile;

		lockTools(true);

		// Save a copy if it's not done yet
		QFileInfo ficopy(copystr);

		if(!ficopy.exists()) {
			QDir dir( fi.absoluteDir() );
			dir.rename(m_currentFile, copystr);
			msg+= tr(" + backup as ") + copystr;
		}

		// Save image
		changeCurCommand (m_pProcThread->saveFile( m_currentFile ));

		statusBar()->showMessage( msg );

		// Store statistics
		dust_stats_t stats = m_pImgProc->getDustStats();
		processAndPrintStats(&stats);

		// Display in progress bar
		changeCurCommand (PROTH_SAVE_FILE); //m_pProcThread->getCommand();
		refreshTimer.start(TMAPP_TIMEOUT);
	}
}


void fprintfDisplayOptions(FILE * f, tm_display_options * p_options) {
	if(!f) return;

	fprintf(f, "CurrentDir:%s\n", p_options->currentDir );
	fprintf(f, "Stylesheet:%s\n", p_options->stylesheet);
	fprintf(f, "HideAuto:%s\n", p_options->hide_auto?"T":"F");
	fprintf(f, "hide_wizard:%s\n", p_options->hide_wizard?"T":"F");
	fprintf(f, "ExportLayer:%s\n", p_options->export_layer?"T":"F");

	fflush(f);
}

extern QString g_application_path;
int TamanoirApp::loadOptions() {
	//
	QString optionsPath = g_application_path;
	char homedir[512] = ".";
	if(getenv("HOME")) {
		strcpy(homedir, getenv("HOME"));
		optionsPath = QString(homedir);
	}
#ifndef WIN32
	optionsFile =  optionsPath + QString("/.tamanoirrc");
#else
	optionsFile =  optionsPath + QString("/tamanoir.ini");
#endif
	memset(&g_options, 0, sizeof(tm_options)); // set all to false

	// Read
	QByteArray file_utf8 = optionsFile.toUtf8();
	FILE * foptions = fopen((char *)file_utf8.data(), "r");
	if(!foptions) {
		// Update options
		g_options.filmType = ui.typeComboBox->currentIndex();
		g_options.trust = ui.trustCheckBox->isChecked();
		g_options.hotPixels = ui.hotPixelsCheckBox->isChecked();
		g_options.onlyEmpty = ui.emptyCheckBox->isChecked();
		g_options.sensitivity = ui.sensitivityHorizontalSlider->value();


		on_dpiComboBox_currentIndexChanged(ui.dpiComboBox->currentText());
		TMAPP_printf(TMLOG_ERROR, "no options file : use default in GUI")
		fprintfOptions(stderr, &g_options);
//		int ret = QMessageBox::warning(this, tr("Tamanoir - Options"),
//				tr("cannot open file ") + optionsFile,
//				QMessageBox::Ok);

		return 0;
	}


	// read each line
	char line[512], *ret=line;
	while(ret && !feof(foptions)) {
		line[0] = '#';
		ret= fgets(line, 511, foptions);
		if(ret) {
			if(strlen(line)>1 && line[0] != '#' && line[0] != '\n' && line[0] != '\r' ) {
				// strip eol
				if(line[strlen(line)-1]=='\r')
					line[strlen(line)-1]='\0';
				if(strlen(line)>1)
					if(line[strlen(line)-1]=='\n')
						line[strlen(line)-1]='\0';

				char * separation = strstr(line, ":");
				if(separation) {
					*separation = '\0';
					char * cmd = line, *arg = separation+1;

					fprintf(logfile, "\t%s:%d : cmd='%s' arg='%s'\n",
							__func__, __LINE__, cmd, arg);
/*
# Options :
Trust:F
HotPixels:F
OnlyEmpty:F
FilmType:1
DPI:2450
Sensitivity:36
CurrentDir:/home/tof/Vision/Tamanoir-svn/tamanoir/dataset
Stylesheet:Default (system)
HideAuto:F
hide_wizard:T
ExportLayer:T
  */
					if(strcasestr(cmd, "CurrentDir")) {
						strcpy(g_display_options.currentDir, arg);
					} else
					if(strcasestr(cmd, "Trust")) {
						g_options.trust = (arg[0]=='T');
					} else
					if(strcasestr(cmd, "Hot")) {
						g_options.hotPixels = (arg[0]=='T');
					} else
					if(strcasestr(cmd, "Empty")) {
						g_options.onlyEmpty = (arg[0]=='T');
					} else
					if(strcasestr(cmd, "Film")) {
						g_options.filmType = atoi(arg);
					} else
					if(strcasestr(cmd, "DPI")) {
						g_options.dpi = atoi(arg);
					}
					if(strcasestr(cmd, "Sensitivity")) {
						g_options.sensitivity = atoi(arg);
					}

					if(strcasestr(cmd, "Stylesheet")) {
						strcpy(g_display_options.stylesheet, arg);
					}
					if(strcasestr(cmd, "hide_wizard")) {
						if(strstr(arg, "T"))
							g_display_options.hide_wizard = true;
						else
							g_display_options.hide_wizard = false;
					}
					if(strcasestr(cmd, "HideAuto")) {
						if(strstr(arg, "T"))
							g_display_options.hide_auto = true;
						else
							g_display_options.hide_auto = false;
					}
					if(strcasestr(cmd, "Export")) {
						if(strstr(arg, "T"))
							g_display_options.export_layer = true;
						else
							g_display_options.export_layer = false;
					}
				}
			}
		}

	}

	fclose(foptions);

	fprintf(logfile, "TamanoirApp::%s:%d: read options : \n", __func__, __LINE__);
	fprintfOptions(logfile, &g_options);
	fprintfDisplayOptions(logfile, &g_display_options);

	// Update GUI with those options
	ui.typeComboBox->setCurrentIndex( g_options.filmType );
	ui.trustCheckBox->setChecked( g_options.trust );
	ui.hotPixelsCheckBox->setChecked( g_options.hotPixels );
	ui.emptyCheckBox->setChecked( g_options.onlyEmpty );
	QString str;
	str.sprintf("%d", g_options.dpi);
	int ind = ui.dpiComboBox->findText(str, Qt::MatchContains);
	if(ind >= 0)
		ui.dpiComboBox->setCurrentIndex(ind);
	else { // add an item
		ui.dpiComboBox->insertItem(-1, str + tr(" dpi"));
		fprintf(stderr, "TamanoirApp::%s:%d : Add a resolution: resolution=%d dpi\n", __func__, __LINE__,
				g_options.dpi);

		ind = ui.dpiComboBox->findText(str, Qt::MatchContains);
		if(ind >= 0) ui.dpiComboBox->setCurrentIndex(ind);
	}

	ui.sensitivityHorizontalSlider->setValue( g_options.sensitivity );

	return 1;
}

void TamanoirApp::saveOptions() {
	// Reload Stylesheet
	QString str(g_display_options.stylesheet);
	QString filename=":/qss/tamanoir-" + str + ".qss";

	QFile file(filename);
	TMAPP_printf(TMLOG_INFO, " => stylesheet file='%s' !!\n",
			filename.toUtf8().data())
	file.open(QFile::ReadOnly);

	QString file_styleSheet = QLatin1String(file.readAll());
	setStyleSheet(file_styleSheet);

	// show/hide buttons
	if(g_display_options.hide_auto) {
		ui.autoButton->hide();
	} else {
		ui.autoButton->show();
	}

	QByteArray file_utf8 = optionsFile.toUtf8();
	TMAPP_printf(TMLOG_INFO, "Saving options in '%s'",
				 optionsFile.toUtf8().data())
	FILE * foptions = fopen((char *)file_utf8.data(), "w");
	if(!foptions) {
		TMAPP_printf(TMLOG_ERROR, "Cannot save file '%s'",
					 (char *)file_utf8.data())
		QMessageBox::warning(this, tr("Tamanoir - Options"),
				tr("cannot save file ") + optionsFile,
				QMessageBox::Ok);

		return;
	}

	// Save image processing options
	fprintfOptions(foptions, &g_options);

	// Save display/GUI options
	fprintfDisplayOptions(foptions, &g_display_options);

	fclose(foptions);
}

void TamanoirApp::on_prevButton_clicked() {
	if(skipped_list.isEmpty()) {
		TMAPP_printf(TMLOG_TRACE, "skipped_list is empty => no dust for previous")
		return;
	}

	if(!force_mode && m_pProcThread) {
		if(g_debug_list) {
			fprintf(stderr, "TamanoirApp::%s:%d : !force_mode => insertCorrection(%d,%d)\n",
					__func__, __LINE__,
					m_current_dust.crop_x+m_current_dust.rel_seed_x,
					m_current_dust.crop_y+m_current_dust.rel_seed_y);
		}
		m_pProcThread->insertCorrection(m_current_dust);
	}

	m_current_dust = skipped_list.takeLast();

	if(g_debug_list) {
		fprintf(stderr, "TamanoirApp::%s:%d :skipped_list.takeLast() "
				"=> current=(%d,%d+%dx%d)\n",
				__func__, __LINE__,
				m_current_dust.crop_x+m_current_dust.rel_seed_x,
				m_current_dust.crop_y+m_current_dust.rel_seed_y,
				m_current_dust.copy_width, m_current_dust.copy_height);
	}

	TMAPP_printf(TMLOG_TRACE, "back for one dust")

	if(skipped_list.isEmpty()) {
		ui.prevButton->setEnabled(FALSE);
	}
	updateDisplay();
}


void TamanoirApp::on_rewindButton_clicked() {

	int ret = QMessageBox::warning(this, tr("Tamanoir"),
			tr("Rewind to first dust will make the application ask you "
			"another time to refuse the dusts you have already seen.\n"
			"Do you want to rewind to first dust ?"),
			QMessageBox::Apply,
			QMessageBox::Cancel);

	if(ret == QMessageBox::Cancel)
		return;


	memset(&m_current_dust, 0, sizeof(t_correction));
	if(m_pImgProc) {
		m_pImgProc->firstDust();
	}

	if(m_pProcThread) {
		m_pProcThread->firstDust();
	}

	ui.overAllProgressBar->setValue(0);
	statusBar()->showMessage( tr("Rewind to first dust."));
}

void TamanoirApp::on_skipButton_clicked()
{
	if(m_pProcThread) {
		// Mark skip on image and store for step backward
		if(m_pImgProc) {
			if(m_current_dust.crop_width>0) {
				m_pImgProc->skipCorrection(m_current_dust);
			}
		}

		if(!force_mode  // don't store forced dust correction into skip list
		   && m_current_dust.crop_width>0) {
			if(skipped_list.isEmpty()) {
				// enable previous button because we'll add one dust
				ui.prevButton->setEnabled(TRUE);
			}

			if(g_debug_list) {
				//
				fprintf(stderr, "TamanoirApp::%s:%d : !force=> append to skipped_list : "
						"current_dust=%d,%d+%dx%d\n",
						__func__, __LINE__,
						m_current_dust.crop_x + m_current_dust.rel_seed_x,
						m_current_dust.crop_y + m_current_dust.rel_seed_y,
						m_current_dust.copy_width, m_current_dust.copy_height);
			}

			skipped_list.append(m_current_dust);
		}

		// First check if a new dust if available
		m_current_dust = m_pProcThread->getCorrection();


		if(m_current_dust.copy_width > 0) {
			// Try to keep the same crop x,y to show to user
			// that we did not change the block
			m_current_dust = m_pImgProc->approxCorrection(m_current_dust);

			// Update display with this correction proposal
			updateDisplay();
		} else { // No dust available, wait for a new one
			int state = m_pProcThread->getCommand();

			int next = m_pProcThread->nextDust();
			changeCurCommand (PROTH_SEARCH);

			TMAPP_printf(TMLOG_DEBUG, "m_current_dust.copy_width <= 0 cmd=%d next=%d",
						 state, next)

			if(next == 0) { // Finished
				TMAPP_printf(TMLOG_DEBUG, "=> search is finished")
				ui.overAllProgressBar->setValue(100);

				statusBar()->showMessage( tr("Finished") );

				updateDisplay(); // To show corrections

				/* DISPLAY PERFORMANCES */
				if(g_evaluate_mode) {
					QString msg, s;
					msg = tr("NONE");

					t_perf_stats stats = m_pImgProc->getPerfs();
					int sum = stats.true_positive+stats.no_proposal
						+ stats.false_positive + stats.false_negative;

					if(sum>0) {
						msg += tr("After background processing => ");

						msg += tr("True positive:");
						s.sprintf("%d / %d = %g %%\n", stats.true_positive, sum,
								  100.f * (float)stats.true_positive / sum);
						msg += s;

						msg += tr("No proposal:");
						s.sprintf("%d / %d = %g %%\n", stats.no_proposal, sum,
								  100.f * (float)stats.no_proposal / sum);
						msg += s;

						msg += tr("False positive:");
						s.sprintf("%d / %d = %g %%\n", stats.false_positive, sum,
								  100.f * (float)stats.false_positive / sum);
						msg += s;

						msg += tr("False negative:");
						s.sprintf("%d / %d = %g %%\n", stats.false_negative, sum,
								  100.f * (float)stats.false_negative / sum);
						msg += s;


						QMessageBox::information( 0, tr("Performances"),
											 tr(" ") + msg + QString("."));
					}
				}

				return;
			}


			if(state == PROTH_NOTHING) // Search was done
			{
				changeCurCommand (PROTH_NOTHING);

				updateDisplay();
			} else {
				refreshTimer.start(TMAPP_TIMEOUT);

				lockTools(true);
			}
		}
	}

	force_mode = false;
}



void TamanoirApp::on_correctButton_clicked()
{
	// Apply previous correction
	if(m_pImgProc) {
		m_pImgProc->forceCorrection(m_current_dust, force_mode);
	}
	m_unsaved_changes = true;

	// Clear current dust
	memset(&m_current_dust, 0, sizeof(t_correction));

	if(g_debug_list) {
		fprintf(stderr, "TamanoirApp::%s:%d : clear current (%d,%d) then call on_skipButton_clicked \n",
				__func__, __LINE__,
				m_current_dust.crop_x+m_current_dust.rel_seed_x,
				m_current_dust.crop_y+m_current_dust.rel_seed_y);
	}

	// Then go to next dust
	on_skipButton_clicked();
}



void TamanoirApp::on_autoButton_clicked()
{
	int ret = QMessageBox::warning(this, tr("Tamanoir"),
			tr("The auto mode will rewind to the first dust, and will validate "
			"every dust correction without asking for confirmation.\n"
			"Do you want to process with the Auto mode ?"),
			QMessageBox::Apply,
			QMessageBox::Cancel);

	if(ret == QMessageBox::Cancel)
		return;

	m_unsaved_changes = true;

	// update progress dialog
/*	if(!m_pProgressDialog) {
		m_pProgressDialog = new QProgressDialog(
				tr("Operation in progress."),
				tr("Cancel"),
				0, 100);
		m_pProgressDialog->setWindowModality(Qt::WindowModal);

		connect(m_pProgressDialog, SIGNAL(canceled()), this, SLOT(on_m_pProgressDialog_canceled()));
	}



	m_pProgressDialog->setValue(0);
	m_pProgressDialog->show();
	m_pProgressDialog->update();
*/
	statusBar()->showMessage(tr("Auto-correct running... please wait."));
	statusBar()->update();

	// Lock control panel
	lockTools(true);

	TMAPP_printf(TMLOG_INFO, "Launch AUTO MODE processing...")
	lockTools(true);
		char logfilename[512] = TMP_DIRECTORY "tamanoir.txt";

		QFileInfo fi(m_currentFile);
		//QString abspathname = fi.absPathName();
		QString ext = fi.completeSuffix();  // ext = ".jpg"

		sprintf(logfilename, "%s%s.txt", TMP_DIRECTORY, fi.baseName().toUtf8().data());

		logfile = fopen(logfilename, "w");
		if(!logfile) {
			g_debug_imgverbose = 0;
			logfile = stderr;
		} else {
			fprintf(stderr, "TamanoirApp::%s:%d : Logging messages in '%s'\n", __func__, __LINE__,
				logfilename);
		}
	{
		g_debug_imgverbose = 0;
	}
	g_debug_savetmp = 0;

	ui.overAllProgressBar->setValue(0);

	TMAPP_printf(TMLOG_INFO, "RUN AUTO MODE RUNNING")

	// cancel previous correction
	if(m_pProcThread) {
		m_pProcThread->setModeAuto(true);
		refreshTimer.start(TMAPP_TIMEOUT);
	}
	fflush(logfile);

	// Update little frame displays
	updateDisplay();

	TMAPP_printf(TMLOG_DEBUG, "AUTO MODE LAUNCHED !!!!!!!!!!!!!!!!!")
}






void TamanoirApp::on_typeComboBox_currentIndexChanged(int i) {
	fprintf(stderr, "TamanoirApp::%s:%d : film changed to type %d ...\n",
		__func__, __LINE__, i);
	statusBar()->showMessage( tr("Changed film type: please wait...") );
	statusBar()->update();

	TMAPP_printf(TMLOG_WARNING, "FIXME : fix cnditiong of clearing dusts")
	if(g_options.filmType != i) {
		skipped_list.clear();
		memset(&m_current_dust, 0, sizeof(t_correction));
	}

	g_options.filmType = i;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else {
		changeCurCommand (PROTH_NOTHING);
	}

	saveOptions();
}

void TamanoirApp::on_sensitivityHorizontalSlider_valueChanged(int val) {
	int threshold = val;
	fprintf(stderr, "TamanoirApp::%s:%d : sensitivity changed to type %d ...\n",
		__func__, __LINE__, threshold);
	if(!m_pImgProc) return;

	// Get image
	IplImage * diffImage = m_pImgProc->getDiffDisplayImage();
	if(!diffImage) {
		return;
	}

	IplImage * colorImg = tmCreateImage(cvGetSize(diffImage), IPL_DEPTH_8U, 4);
	cvCvtColor(diffImage, colorImg, CV_GRAY2BGR);
	TMAPP_printf(TMLOG_INFO, "Change level to %d for diffImage=%dx%d",
				 threshold, colorImg->width, colorImg->height)
	QImage greyDiff(colorImg->width, colorImg->height, QImage::Format_RGB32);
	TMAPP_printf(TMLOG_INFO, "copy colorImg= pitch:%d x height:%d",
				 colorImg->widthStep, colorImg->height)

	// Threshold value
	u8 thresh_u8 = threshold;
	for(int r = 0; r<diffImage->height; r++) {
		u32 * colorline = IPLLINE_32U(colorImg, r);
		u8 * diffline = IPLLINE_8U(diffImage, r);
		for(int c = 0; c<diffImage->width; c++) {
			if(diffline[c]>thresh_u8) {
				colorline[c] = 0xFF0000; // Red
			}
		}
	}
	memcpy(greyDiff.bits(), colorImg->imageData, colorImg->widthStep * colorImg->height);

	// Use fake colors to show the level
	// FIXME : CRASH on macOSX (with Qt4.6.0)
	/*if(greyDiff.depth() == 8) {
		greyDiff.setNumColors(256);

		for(int col = 0; col<threshold; col++ ) {
			greyDiff.setColor(col, qRgb(col,col,col));
		}
		for(int col = threshold; col<256; col++ ) {
			greyDiff.setColor(col, qRgb(255,0,0));
		}
	}
*/
	TMAPP_printf(TMLOG_INFO, "Convert to Pixmap=%dx%d x %dbit",
				 greyDiff.width(), greyDiff.height(), greyDiff.depth())
	QPixmap greyPixmap = QPixmap::fromImage(greyDiff);
	TMAPP_printf(TMLOG_INFO, "Display colorImg=%dx%d",
				 colorImg->width, colorImg->height)
	ui.mainPixmapLabel->setPixmap(greyPixmap);

	TMAPP_printf(TMLOG_INFO, "Release colorImg=%dx%d",
				 colorImg->width, colorImg->height)
	tmReleaseImage(&colorImg);
}

/// Display sensitivity in main display when moving the slider
void TamanoirApp::on_sensitivityHorizontalSlider_sliderReleased() {
	// Released => apply this sensitivity level
	int threshold = ui.sensitivityHorizontalSlider->value();

	fprintf(stderr, "TamanoirApp::%s:%d : sensitivity changed to type %d ...\n",
		__func__, __LINE__, threshold);

	statusBar()->showMessage( tr("Changed sensitivity: please wait...") );
	statusBar()->update();

	if(g_options.sensitivity < threshold) {
		skipped_list.clear(); // We changed sensitivity, clear the dust list
		memset(&m_current_dust, 0, sizeof(t_correction));
	}

	g_options.sensitivity = threshold;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else {
		changeCurCommand (PROTH_NOTHING);
	}

	updateMainDisplay();

	saveOptions();
}

void TamanoirApp::on_sensitivityComboBox_currentIndexChanged(int i) {
	fprintf(stderr, "TamanoirApp::%s:%d : sensitivity changed to type %d ...\n",
		__func__, __LINE__, i);
	statusBar()->showMessage( tr("Changed sensitivity: please wait...") );
	statusBar()->update();

	if(g_options.sensitivity != i) {
		skipped_list.clear();
		memset(&m_current_dust, 0, sizeof(t_correction));
	}

	g_options.sensitivity = i;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else {
		changeCurCommand (PROTH_NOTHING);
	}

	saveOptions();
}

void TamanoirApp::on_dpiComboBox_currentIndexChanged(QString str) {
	TMAPP_printf(TMLOG_INFO, "TamanoirApp::%s:%d : resolution changed to type '%s'' ...\n",
		__func__, __LINE__, str.toUtf8().data());
	statusBar()->showMessage( tr("Changed resolution: please wait...") );
	statusBar()->update();

	int dpi = 2400;
	if(sscanf(str.toUtf8().data(), "%d", &dpi) != 1) {
		g_options.dpi = 2400;
	} else {
		g_options.dpi = dpi;
	}

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else {
		changeCurCommand (PROTH_NOTHING);
	}

	saveOptions();
}

void TamanoirApp::on_trustCheckBox_toggled(bool on)
{
	statusBar()->showMessage( tr("Changed to 'trust' mode : ")
							  + (on?tr("ON"):tr("OFF")));

	g_options.trust = on;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else changeCurCommand (PROTH_NOTHING);

	saveOptions();
}

void TamanoirApp::on_emptyCheckBox_toggled(bool on) {
	statusBar()->showMessage( tr("Changed empty area filter: please wait...") );
	statusBar()->update();

	g_options.onlyEmpty = on;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else changeCurCommand (PROTH_NOTHING);

	saveOptions();
}


void TamanoirApp::on_hotPixelsCheckBox_toggled(bool on) {
	statusBar()->showMessage( tr("Changed hot pixels filter: please wait...") );
	statusBar()->update();
	g_options.hotPixels = on;

	if(m_pProcThread) {
		changeCurCommand (m_pProcThread->setOptions(g_options));
		refreshTimer.start(TMAPP_TIMEOUT);
	} else changeCurCommand (PROTH_NOTHING);

	saveOptions();
}


void TamanoirApp::on_cropPixmapLabel_customContextMenuRequested(QPoint p)
{
	fprintf(stderr, "TamanoirApp::%s:%d : clicked on %d,%d", __func__, __LINE__, p.x(), p.y());

}


static u32 * grayToBGR32 = NULL;
static u32 * grayToBGR32False = NULL;
static u32 * grayToBGR32Red = NULL;

static void init_grayToBGR32()
{
	if(grayToBGR32) {
		return;
	}

	grayToBGR32 = new u32 [256];
	grayToBGR32False = new u32 [256];
	grayToBGR32Red = new u32 [256];
	for(int c = 0; c<256; c++) {
		int Y = c;
		u32 B = Y;// FIXME
		u32 G = Y;
		u32 R = Y;
		grayToBGR32[c] = grayToBGR32Red[c] =
			grayToBGR32False[c] = (R << 16) | (G<<8) | (B<<0);
	}

	// Add false colors
	grayToBGR32[COLORMARK_CORRECTED] = // GREEN
		grayToBGR32False[COLORMARK_CORRECTED] = (255 << 8);
		//mainImage.setColor(COLORMARK_CORRECTED, qRgb(0,255,0));
	// YELLOW
	grayToBGR32False[COLORMARK_REFUSED] = (255 << 8) | (255 << 16);
				//mainImage.setColor(COLORMARK_REFUSED, qRgb(255,255,0));
	grayToBGR32False[COLORMARK_FAILED] =
			grayToBGR32Red[COLORMARK_FAILED] = (255 << 16);
				//mainImage.setColor(COLORMARK_FAILED, qRgb(255,0,0));
	grayToBGR32False[COLORMARK_CURRENT] = // BLUE
	//		grayToBGR32Red[COLORMARK_CURRENT] =
										(255 << 0);
				//mainImage.setColor(COLORMARK_CURRENT, qRgb(0,0,255));
}

QImage iplImageToQImage(IplImage * iplImage, bool false_colors, bool red_only )
{
    if(!iplImage)
    {
		return QImage();
	}

    // Create local image
    IplImage * displayImage = tmCreateImage( cvGetSize(iplImage), IPL_DEPTH_8U, 4);
    cvFillImage(displayImage, 255 );
    tmConvert( iplImage, displayImage );

    // Map image on QImage
    QImage qImage((uchar *)displayImage->imageData,
                  displayImage->width, displayImage->height, displayImage->widthStep,
                  QImage::Format_ARGB32);
    QImage retImage = qImage.copy();
    tmReleaseImage(&displayImage);
    return retImage;
}


void TamanoirApp::refreshMainDisplay() {
	if(!m_pImgProc) {
		return;
	}

	int scaled_width = m_main_display_rect.width()-2;
	int scaled_height = m_main_display_rect.height()-2;
	//fprintf(stderr, "TamanoirApp::%s:%d : original display = %d x %d\n",
	//		__func__, __LINE__, scaled_width, scaled_height);

	// image proc will only store this size at first call
	m_pImgProc->setDisplaySize(scaled_width, scaled_height);
}

void TamanoirApp::updateDisplay() {
	if(m_pImgProc) {
		updateMainDisplay(); // update the global display with grep rectangle on the area
		updateCroppedDisplay(); // update the cropped areas
		updateCroppedCursor(); // update the status of the vector display
	}
}

void TamanoirApp::updateMainDisplay() {

	if(m_pImgProc) {
	// After pre-processing, we can get the grayscale version of input image
		IplImage * displayImage = m_pImgProc->getDisplayImage();


		if(!displayImage) {
			refreshMainDisplay ();
			displayImage = m_pImgProc->getDisplayImage();
			if(!displayImage) return;
		}



		if(displayImage) {

			// Display in main frame
			int gray_width = displayImage->width;
			//int scaled_width = displayImage->width;
			//int scaled_height = displayImage->height;


			QImage mainImage(gray_width, displayImage->height, QImage::Format_RGB32); //8*displayImage->nChannels);
			mainImage = iplImageToQImage(displayImage, true, false).copy();

			QPixmap pixmap = QPixmap::fromImage( mainImage );
			/* fprintf(stderr, "TamanoirApp::%s:%d : orginal rectangle : maxSize=%dx%d\n",
									__func__, __LINE__,
									m_main_display_rect.width(),m_main_display_rect.height() );
				fprintf(stderr, "TamanoirApp::%s:%d : pixmap=%dx%d => Scaled=%dx%d\n", __func__, __LINE__,
									pixmap.width(), pixmap.height(),
									scaled_width, scaled_height);
			*/
			ui.mainPixmapLabel->setPixmap(pixmap);
		}
	}
}

void TamanoirApp::updateCroppedDisplay()
{
	if(m_pImgProc) {

		// Update cropped buffers
		if(m_pProcThread) {
			m_current_dust.crop_width = ui.cropPixmapLabel->size().width();
			m_current_dust.crop_height = ui.cropPixmapLabel->size().height();

			m_pImgProc->cropCorrectionImages(m_current_dust, m_draw_on);
		}


		static u8 old_debug = g_debug_imgverbose,
					old_correlation = g_debug_correlation;

		if(!ui.infoFrame->isHidden()) {
			// force debug
			g_debug_imgverbose = g_debug_savetmp = 1;

			//
			tm_dust_detection_props props =
					m_pImgProc->getDustProps();
			/*
u8 big_enough;					*! Size if big enough to be a dust (dpeend on dpi) *

float connect_area
float flood_area;				*! Area of region growing on gray level *
u8 is_fiber;					*! Fiber (surf of rect >> surf of diff region growing *
float mean_neighbour;			*! Mean value of neighbouring *
float mean_dust;				*! Mean value of dust *
float contrast;					*! Contrast between neighbouring and dust *
u8 visible_enough;				*! Different enough from neighbour *
u8 dilateDust ;					*! => Still a dust after function dilateDust() *

int searchBestCorrelation;		*! Result of search best correlation *

float correl_dust_src;			*! Correlation between dust (seed growing) and source proposal *
u8 src_connected_to_dest;		*! is source connected to dest ? e.g. same region, fiber, cable ... return of @see srcNotConnectedToDest(pcorrection); *

u8 diff_from_neighbour;			*! return of @see differentFromNeighbourHood(pcorrection, diffref); *
u8 diff_from_dest;				*! return of @see srcDifferentFromDest(pcorrection); *
u8 correctedBetterThanOriginal; *! return of @see correctedBetterThanOriginal(pcorrection); *
u8 neighbourhoodEmpty;			*! return of @see neighbourhoodEmpty(pcorrection); *
} tm_dust_detection_props;

			  */
			char proptxt[1024];
			sprintf(proptxt, "seed:%d,%d "
					"grown:%d,%d+%dx%d:%d "
						//"\n"
					"rel:%d,%d+%dx%d "
						//"\n"
					"%s %s flood=%g area=%g %s " // ... fiber
						//"\n"
					"mean{D=%g N=%g C=%.2g} "
					"=>vis=%c "
					"dilateDust=%c best=%g=>%g "
						//"\n"
					"bestCor=%d "
						//"\n"
					"corr/src=%g !conn(src/dst)=%c"
						//"\n"
					"diff/N=%c eqdif=%.2g "
					"diff/dest=%c=%g dx,y=%d,%d "
						"\n"
					"better/orig=%c Nempt=%c"
					 ,
					props.seed_x, props.seed_y,
					props.abs_grown_conn.rect.x, props.abs_grown_conn.rect.y,
						props.abs_grown_conn.rect.width, props.abs_grown_conn.rect.height,
						(int)props.abs_grown_conn.area,
					props.rel_grown_conn.rect.x, props.rel_grown_conn.rect.y,
						props.rel_grown_conn.rect.width, props.rel_grown_conn.rect.height,
					props.force_search ? "force":"",
					props.big_enough? "bigen":"",
					props.flood_area, props.connect_area,
						props.is_fiber ? "fiber":"",
					props.mean_dust, props.mean_neighbour,
						props.contrast, props.visible_enough ? 'T':'F',
					props.dilateDust ? 'T':'F',
						props.best_correl_dilate, props.best_correl_max,
					props.searchBestCorrelation,
					props.correl_dust_src,
						props.src_not_connected_to_dest ? 'T':'F',
					props.diff_from_neighbour? 'T':'F',
						m_current_dust.equivalent_diff,
					props.diff_from_dest? 'T':'F',
						m_current_dust.srcdest_correl,
					props.copy_dx, props.copy_dy,
					props.correctedBetterThanOriginal? 'T':'F',
					props.neighbourhoodEmpty? 'T':'F'
					);

			ui.dustInfoLabel->setText(proptxt);
		} else {
			g_debug_imgverbose = old_debug;
			g_debug_correlation = old_correlation;
		}


//unused #define LABELWIDTH_MARGIN	2
		IplImage * curImage;

		// Top-left : Display cropped / detailled images -- ORIGINAL
		curImage = m_pImgProc->getCropArrow();

		if(curImage) {
			QImageDisplay * pLabel = ui.cropPixmapLabel;
			//unused int label_width = pLabel->width()-LABELWIDTH_MARGIN;
			if(g_debug_displaylabel) {
				QString propStr;
				propStr.sprintf("Dist=%g BgDif=%g %.1f%% smD=%d s/d={d=%g max=%g}",
								m_current_dust.proposal_diff, m_current_dust.bg_diff,
								m_current_dust.equivalent_diff*100.f,
								m_current_dust.smooth_diff,
								m_current_dust.srcdest_correl, m_current_dust.srcdest_maxdiff
								);
				ui.proposalLabel->setText( propStr );
			}

			// Display in frame
			QImage grayQImage = iplImageToQImage(curImage,
												 false); //.scaledToWidth(label_width);
			if(grayQImage.depth() == 8) {
				grayQImage.setNumColors(256);
				for(int c=0; c<255; c++) {
					grayQImage.setColor(c, qRgb(c,c,c));
				}

				grayQImage.setColor(255, qRgb(0,255,0));
			}

			QPixmap pixmap = QPixmap::fromImage(
						grayQImage);
			pLabel->setPixmap(pixmap);
			pLabel->setCorrection(m_current_dust);
			pLabel->repaint();
		}

		// Try inpainting
		IplImage * grownImage = m_pImgProc->getMask();//getDiffCrop();
		if(grownImage) {

		}

		// Bottom-left : Proposed correction -- CORRECTED
		curImage = m_pImgProc->getCorrectedCrop();
		if(curImage) {
			QLabel * pLabel = ui.correctPixmapLabel;
			//unused int label_width = pLabel->width()-LABELWIDTH_MARGIN;

			// If wa are over the correction pixmap, let's display debug information
			if(m_overCorrected) {
				IplImage * growImage = m_pImgProc->getMask();//getDiffCrop();
				//IplImage * growImage = m_pImgProc->getDiffCrop();

				// Mix images
				//float coef = 0.5f;
				//float coef_1 = 1.f - coef;
#define DUST_MARK_R		255
#define DUST_MARK_G		0
#define DUST_MARK_B		0
				int RGB24[4] = { DUST_MARK_B, DUST_MARK_G,DUST_MARK_R, 0 };
				int BGR32[4] = { DUST_MARK_R, DUST_MARK_G, DUST_MARK_B, 0 };

				if(growImage) {
					for(int r = 0; r<growImage->height; r++) {
						u8 * correctLine = (u8 *)(curImage->imageData + r * curImage->widthStep);
						u8 * growLine = (u8 *)(growImage->imageData + r * growImage->widthStep);
						switch(curImage->nChannels)  {
						case 1:
							for(int c = 0; c<growImage->width; c++) {
								if(growLine[c]>0) {
									correctLine[c] = COLORMARK_FAILED;
								}
							}
							break;
						case 3: {
							int kc = 0;
							for(int c = 0; c<growImage->width; c++, kc+=curImage->nChannels) {
								if(growLine[c]>30) {
									/*int col = growLine[c];

									// False colors
									if(col<128) RGB[2] = (255-2*col); else RGB[2] = 0;
									if(col>128) RGB[0] = 2*(col-128); else RGB[0] = 0;
									RGB[2] = 256 - abs(128-col)*2; if(RGB[1]>255) RGB[1] = 255;
									*/

									for(int k=0; k<curImage->nChannels; k++) {
										//correctLine[kc+k] = (u8) (RGB[k] * (int)correctLine[kc+k] / 255);
										correctLine[kc+k] = (u8) (RGB24[k]);
									}
								}
							}
							}
							break;
						case 4: {
							int kc = 0;
							for(int c = 0; c<growImage->width; c++, kc+=curImage->nChannels) {
								if(growLine[c]>30) {
									/*int col = growLine[c];

									// False colors
									if(col<128) RGB[2] = (255-2*col); else RGB[2] = 0;
									if(col>128) RGB[0] = 2*(col-128); else RGB[0] = 0;
									RGB[2] = 256 - abs(128-col)*2; if(RGB[1]>255) RGB[1] = 255;
									*/

									for(int k=0; k<curImage->nChannels; k++) {
										//correctLine[kc+k] = (u8) (RGB[k] * (int)correctLine[kc+k] / 255);
										correctLine[kc+k] = (u8) (BGR32[k]);
									}
								}
							}
							}break;
						}
					}
				}
			}


			// Display in frame
			QImage grayQImage = iplImageToQImage(curImage,
												 true, // false colors
												 true // with only red as false color
												 ); //.scaledToWidth(label_width);

			QPixmap pixmap = QPixmap::fromImage(
				grayQImage //.scaledToWidth(pLabel->width()),
				);
			pLabel->setPixmap(pixmap);
			pLabel->repaint();
		}


		// Mask image = dust in white on black background
		if(!ui.growPixmapLabel->isHidden()) {
			curImage = m_pImgProc->getMask();
			if(curImage) {
				QLabel * pLabel = ui.growPixmapLabel;

				// Display in frame
				QImage grayQImage = iplImageToQImage(curImage).scaledToWidth(pLabel->width());
				if(grayQImage.depth() == 8) {
					grayQImage.setNumColors(256);
					for(int c=0; c<255; c++)
						grayQImage.setColor(c, qRgb(c,c,c));

					grayQImage.setColor(255, qRgb(255,0,0));
				}

				QPixmap pixmap = QPixmap::fromImage(
						grayQImage//.smoothScale(pLabel->width(),pLabel->height()),
						);
				pLabel->setPixmap(pixmap);
				pLabel->repaint();
			}
		}


		// Top-right : Display dust info
		if(g_debug_TamanoirApp >= TMLOG_DEBUG) {
			float width_mm = m_current_dust.width_mm;
			float height_mm = m_current_dust.height_mm;

			QString strinfo;
			strinfo.sprintf( "%d pix/%.1gx%.1g mm",
				m_current_dust.area,
				width_mm, height_mm);
			QString str = tr("Corrected: dust: ") + strinfo ;
			ui.dustGroupBoxLabel->setText(str);
		}

		// Bottom-right : Display diff image (neighbouring)
		if(!ui.diffPixmapLabel->isHidden()) {
			curImage = m_pImgProc->getDiffCrop();

			if(g_debug_correlation) {
				curImage = getCorrelationImage();
			}

			if(curImage) {
				QLabel * pLabel = ui.diffPixmapLabel;

				// Display in frame
				QImage grayQImage = iplImageToQImage(curImage);
				if(grayQImage.depth() == 8) {
					grayQImage.setNumColors(256);
					for(int c=0; c<256; c++) {
						int R=c, G=c, B=c;
						// False colors
						if(c<128) B = (255-2*c); else B = 0;
						if(c>128) R = 2*(c-128); else R = 0;
						G = 256 - abs(128-c)*2; if(G>255) G = 255;

						grayQImage.setColor(c, qRgb(R,G,B));
					//	grayQImage.setColor(c, qRgb(c,c,c));
					}
				}

				QPixmap pixmap = QPixmap::fromImage(
						grayQImage);
				pLabel->setPixmap(pixmap);
				pLabel->repaint();
			}
		}

		ui.overAllProgressBar->setValue(m_pImgProc->getProgress());

	} else {
		ui.overAllProgressBar->setValue(0);
	}
}





TamanoirThread::TamanoirThread(TamanoirImgProc * p_pImgProc) {
	m_pImgProc = p_pImgProc;
	memset(&m_options, 0, sizeof(tm_options));

	m_req_command = m_current_command = PROTH_NOTHING;
	m_no_more_dusts = false;
	m_run = m_running = false;

	start();
}

int TamanoirThread::getCommand() {
	if(m_options.mode_auto ) {
		if(!m_no_more_dusts) {
			TMTHR_printf(TMLOG_DEBUG, "Auto && !no_more_dusts -> pretend to be searching ! ")
			return PROTH_SEARCH;
		}
	}


	if(m_req_command != PROTH_NOTHING) {
		return m_req_command;
	}

	return m_current_command;
}

int TamanoirThread::runPreProcessing() {
	TMAPP_printf(TMLOG_INFO, " PREPROC "
			 "m_reqCommand=%d PROTH_PREPROC\n",
			 m_req_command);

	m_req_command = PROTH_PREPROC;
	m_no_more_dusts = false;

	m_dust_list.clear();

	// Unlock thread
	mutex.lock();
	waitCond.wakeAll();
	mutex.unlock();

	return 0;
}


/* set auto mode flag */
void TamanoirThread::setModeAuto(bool on) {
	if(on) {
		if(!m_pImgProc)
			return;
		if(!m_run)
			start();

		TMTHR_printf(TMLOG_INFO, "Starting auto mode")
		m_req_command = PROTH_SEARCH;
		m_options = m_pImgProc->getOptions();

		m_options.mode_auto = true;
		m_no_more_dusts = false;

		m_dust_list.clear();
		setOptions(m_options);

		// Unlock thread
		mutex.lock();
		waitCond.wakeAll();
		mutex.unlock();

	} else {
		TMTHR_printf(TMLOG_INFO, "Stopping auto mode")
		m_options.mode_auto = false;
	}
}

int TamanoirThread::setOptions(tm_options options) {
	if(!m_pImgProc) {
		return 0;
	}
	if(!m_run) {
		start();
	}

	bool restart_from_changes = false;

	TMTHR_printf(TMLOG_WARNING, "FIXME : fix conditions of dust list clearing on option changes")
	// If something2388  changed, clear already known dusts
	if(m_options.filmType != options.filmType) {
		TMTHR_printf(TMLOG_INFO, "Film type changed = %d != old=%d "
					 "=> restart from topleft",
					 options.filmType, m_options.filmType);
	}

	if(m_options.trust != options.trust) {
		TMTHR_printf(TMLOG_INFO, "Trust mode changed = %c != old=%c "
					 "=> restart from topleft",
					 options.trust?'T':'F',
					 m_options.trust?'T':'F');
		m_dust_list.clear();
	}

	if(m_options.dpi != options.dpi) {
		TMTHR_printf(TMLOG_INFO, "Resolution changed = %d != old=%d "
					 "=> restart from topleft",
					 options.dpi, m_options.dpi);
		m_dust_list.clear();
	}

	// new is more sensitive => need to change
	if(m_options.sensitivity < options.sensitivity) {
		TMTHR_printf(TMLOG_INFO, "New sensitivity =%d > old=%d => restart ftop topleft",
					 options.sensitivity, m_options.sensitivity);
		m_dust_list.clear();
	}

	if(m_options.onlyEmpty != options.onlyEmpty) {
		TMTHR_printf(TMLOG_INFO, "Only empty option changed = %c != old=%c "
					 "=> restart from topleft",
					 options.onlyEmpty?'T':'F',
					 m_options.onlyEmpty?'T':'F');
		m_dust_list.clear();
	}

	int ret = m_req_command = PROTH_OPTIONS;
	if(memcmp(&m_options, &options, sizeof(options)) != 0) {
		TMTHR_printf(TMLOG_INFO, "Options changed")
		ret = m_req_command = PROTH_OPTIONS;
	}
	m_options = options;


	if(restart_from_changes) {
		// The user may have changed the options while loading
		m_pImgProc->abortLoading(true);

		TMTHR_printf(TMLOG_INFO, "Apply changes, rewind to start")
		m_dust_list.clear();
		m_pImgProc->firstDust();
	}

	// Unlock thread
	mutex.lock();
	waitCond.wakeAll();
	mutex.unlock();

	return ret;
}


int TamanoirThread::loadFile(QString s) {
	m_filename = s;
	m_no_more_dusts = false;
	if(!m_run) {
		start();
		// Wait for thread to start
        while(!m_run) {
            usleep(200000);
        }
	}

	// Clear dust list
	m_dust_list.clear();


	QFileInfo fi(s);
	if(!fi.exists()) {
		fprintf(stderr, "TmThread::%s:%d : file '%s' does not exists\n", __func__, __LINE__, s.toUtf8().data());
		return -1;
	}

	int ret = m_req_command = PROTH_LOAD_FILE;
	fprintf(stderr, "TmThread::%s:%d : request load file '%s' \n",
			__func__, __LINE__, s.toUtf8().data());

	mutex.lock();
	// Unlock thread
	waitCond.wakeAll();
	mutex.unlock();

	return ret;
}

TamanoirThread::~TamanoirThread() {
	m_run = false;
	while(m_running) {
		mutex.lock();
		m_req_command = PROTH_NOTHING;
		waitCond.wakeAll();
		mutex.unlock();

		fprintf(stderr, "TmThread::%s:%d : waiting for thread to stop\n",
				__func__, __LINE__);
		if(m_running) {
			sleep(1);
		}
	}
}

int TamanoirThread::saveFile(QString s) {
	m_filename = s;

	if(!m_run) { // start background thread
		start();
	}

	mutex.lock();
	int ret = m_req_command = PROTH_SAVE_FILE;
	waitCond.wakeAll();
	mutex.unlock();

	// Unlock thread
	return ret;
}

/* Get last detected dust correction */
t_correction TamanoirThread::getCorrection() {
	t_correction current_dust;

	if(m_dust_list.isEmpty()) {
		memset(&current_dust, 0, sizeof(t_correction));
	} else {
		current_dust = m_dust_list.takeFirst();
	}


	if(g_debug_TmThread || g_debug_list) {
		fprintf(stderr, "TMThread::%s:%d : takeFirst => dust=%d,%d +%dx%d\n",
				__func__, __LINE__,
				current_dust.crop_x + current_dust.rel_dest_x,
				current_dust.crop_y + current_dust.rel_dest_y,
				current_dust.copy_width, current_dust.copy_height);
	}

	return current_dust;
}

void TamanoirThread::insertCorrection(t_correction new_dust) {
	if(new_dust.copy_width <= 0)
		return;

	m_dust_list.prepend(new_dust);

	if(g_debug_TmThread || g_debug_list) {
		fprintf(stderr, "TMThread::%s:%d : prepend dust=%d,%d +%dx%d\n",
			__func__, __LINE__,
			new_dust.crop_x + new_dust.rel_dest_x,
			new_dust.crop_y + new_dust.rel_dest_y,
			new_dust.copy_width, new_dust.copy_height);
	}
}

int TamanoirThread::firstDust() {
	if(!m_dust_list.isEmpty()) {
		TMTHR_printf(TMLOG_DEBUG, "dust list is not empty (%d elts) => clear",
				m_dust_list.count()	)
		m_dust_list.clear();
	}

	if(!m_pImgProc) {
		return -1;
	}
	m_pImgProc->firstDust();

	mutex.lock();
	m_req_command = PROTH_SEARCH;
	waitCond.wakeAll();
	mutex.unlock();

	return 1;
}

int TamanoirThread::nextDust() {
	if(!m_pImgProc)
		return -1;

	int ret = 1;

	if(m_dust_list.isEmpty() && m_no_more_dusts) {
		ret = 0;
	}

	mutex.lock();
	m_req_command = PROTH_SEARCH;
	waitCond.wakeAll();
	mutex.unlock();

	return ret;
}

int TamanoirThread::getProgress() {
	if(m_current_command == PROTH_NOTHING) {
		TMTHR_printf(TMLOG_DEBUG, "nothing to process => return 100 %%")
		return 100;
	}
	if(!m_pImgProc) {
		TMTHR_printf(TMLOG_DEBUG, "no img process => return 0 %%")
		return 0;
	}

	TMTHR_printf(TMLOG_DEBUG, "return img processing cmd=%d progress= %d %%",
				 m_current_command, m_pImgProc->getProgress())
	return m_pImgProc->getProgress();
}


/*
 * Background processing thread
 */
void TamanoirThread::run() {
	m_running = true;
	m_run = true;

	m_no_more_dusts = false;
	while(m_run) {
		int wait_ms = 20;
		if(m_options.mode_auto
		   && !m_no_more_dusts) {
			wait_ms = 1; // go fastest
		}
		mutex.lock();
		waitCond.wait(&mutex, wait_ms);
		mutex.unlock();

		if(m_req_command != PROTH_NOTHING && g_debug_TmThread) {
			TMTHR_printf(TMLOG_TRACE, "run command = %d",m_req_command)
			// clear abort flag because we're back to processing thread
			m_pImgProc->abortLoading(false);
		}

		// Copy requested action in current action
		m_current_command = m_req_command;
		// and clear next action requested
		m_req_command = PROTH_NOTHING;

		int ret;
		bool do_search = false;
		switch(m_current_command) {
		default:
		case PROTH_NOTHING:
			//fprintf(stderr, "TmThread::%s:%d : do NOTHING ???\n", __func__, __LINE__);
			if(!m_no_more_dusts
			   && m_pImgProc->getPreProcessedDone()
			   ) {
				// If there are not too much dusts in list, continue to search
				if(m_dust_list.count() < 100) {
					//m_req_command = PROTH_SEARCH;
					do_search = true;
					TMTHR_printf(TMLOG_TRACE, "nothing => PROTH_SEARCH !!!")
				}
			}

			break;
		case PROTH_SAVE_FILE:
			TMTHR_printf(TMLOG_INFO, "save file '%s'\n", m_filename.toUtf8().data())
			m_pImgProc->saveFile(m_filename.toUtf8().data());
			break;

		case PROTH_LOAD_FILE:
			TMTHR_printf(TMLOG_INFO, "LOADING FILE '%s'\n", m_filename.toUtf8().data())
			if(m_options.mode_auto) {
				TMTHR_printf(TMLOG_INFO, "Stopping auto mode")

				m_options.mode_auto = false;
			}

			ret = m_pImgProc->loadFile(m_filename.toUtf8().data());
			m_no_more_dusts = false;

			TMTHR_printf(TMLOG_INFO, "file '%s' LOADED", m_filename.toUtf8().data())

			break;

		case PROTH_PREPROC:
			TMTHR_printf(TMLOG_INFO, "pre-processing for file '%s'...", m_filename.toUtf8().data())
			ret = m_pImgProc->preProcessImage();
			TMTHR_printf(TMLOG_INFO, "PRE-PROCESSING for file '%s' : DONE. Next command is PROTH_SEARCH", m_filename.toUtf8().data())
			m_req_command = PROTH_SEARCH;

			break;

		case PROTH_SEARCH:
			TMTHR_printf(TMLOG_TRACE, "searching for next dust (while main frame is displaying)")
			do_search = true;

			break;

		case PROTH_OPTIONS:
			TMTHR_printf(TMLOG_INFO, "process options changes....")
			if(g_debug_list) {
				//
				fprintf(stderr, "TmThread::%s:%d : PROTH_OPTIONS => "
						"dust_list.clear()\n",
						__func__, __LINE__);
			}

			bool need_preproc =
				m_pImgProc->setOptions(m_options);

			// There will be new dusts
			m_no_more_dusts = false;

			// Pre-process input file
			if(need_preproc) {
				// Clear dust list
				m_dust_list.clear();
				TMTHR_printf(TMLOG_DEBUG, "process options change done => next step is PROTH_PREPROC....")
				m_req_command = PROTH_PREPROC;
			}

			break;
		}

		// If we have nothing to do, we can search for next dust
		if(do_search) {
			ret = m_pImgProc->nextDust();
			if(ret > 0) {
				// Add to list
				t_correction l_dust = m_pImgProc->getCorrection();
				m_no_more_dusts = false;
				if(!m_options.mode_auto) {
					if(g_debug_list) {
						//
						fprintf(stderr, "TmThread::%s:%d : !m_options.mode_auto => "
								"dust_list.append(%d,%d)\n",
								__func__, __LINE__,
								l_dust.crop_x + l_dust.rel_seed_x,
								l_dust.crop_y + l_dust.rel_seed_y);
					}

					m_dust_list.append(l_dust);
				} else {
					TMAPP_printf(TMLOG_DEBUG, "mode AUTO : apply correction in %d,%d",
								 l_dust.crop_x+l_dust.rel_seed_x,
								 l_dust.crop_y+l_dust.rel_seed_y)

					m_pImgProc->applyCorrection(l_dust);

					// Fasten next search
					//req_command = PROTH_SEARCH;
				}
			} else { // no more dust

				if(ret == 0) {
					m_no_more_dusts = true;
					TMTHR_printf(TMLOG_INFO, "no more dust (ret=%d)", ret);

					if(m_options.mode_auto) {
						TMTHR_printf(TMLOG_INFO, "no more dust (ret=%d) "
									 "=> Stopping auto mode", ret);

						// Stop auto mode
						m_options.mode_auto = false;
					}
				}
			}
			TMTHR_printf(TMLOG_TRACE, "    => next dust ret=%d", ret)
		}


		m_current_command = PROTH_NOTHING;
	}

	m_running = false;
}



