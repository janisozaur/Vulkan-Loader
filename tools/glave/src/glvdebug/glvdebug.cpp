/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#include <assert.h>
#include <QFileDialog>
#include <QPalette>
#include <QProcess>
#include <QToolButton>
#include <QStandardPaths>
#include <QMessageBox>
#include <QCoreApplication>
#include <QGraphicsBlurEffect>
#include <qwindow.h>

#include "ui_glvdebug.h"
#include "glvdebug.h"
#include "glvdebug_settings.h"
#include "glvdebug_output.h"

#include "glvdebug_controller_factory.h"
#include "glvdebug_qgeneratetracedialog.h"

//----------------------------------------------------------------------------------------------------------------------
// globals
//----------------------------------------------------------------------------------------------------------------------
static QString g_PROJECT_NAME = "GLV Debugger";

glvdebug::glvdebug(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::glvdebug),
      m_pTraceFileModel(NULL),
      m_pController(NULL),
      m_pGenerateTraceButton(NULL),
      m_pTimeline(NULL),
      m_pGenerateTraceDialog(NULL),
      m_bDelayUpdateUIForContext(false)
{
    ui->setupUi(this);

    // load the settings file. This will only succeed if the file already exists
//    g_settings.load(g_SETTINGS_FILE);

    // always save/resave so the file will either be created or so that new settings will be added
//    g_settings.save(g_SETTINGS_FILE);

    memset(&m_traceFileInfo, 0, sizeof(glvdebug_trace_file_info));

    this->move(g_settings.window_position_left, g_settings.window_position_top);
    this->resize(g_settings.window_size_width, g_settings.window_size_height);

    glvdebug_output_init(ui->outputTextEdit);
    glvdebug_output_message("Welcome to GLVDebug!");

    // cache the original background color of the search text box
    m_searchTextboxBackgroundColor = ui->searchTextBox->palette().base().color();
    
    // add buttons to toolbar
    m_pGenerateTraceButton = new QToolButton(ui->mainToolBar);
    m_pGenerateTraceButton->setText("Generate Trace");
    m_pGenerateTraceButton->setEnabled(true);
    connect(m_pGenerateTraceButton, SIGNAL(clicked()), this, SLOT(prompt_generate_trace()));

    ui->mainToolBar->addWidget(m_pGenerateTraceButton);

    ui->treeView->setModel(NULL);

    // setup timeline
    m_pTimeline = new glvdebug_QTimelineView();
    m_pTimeline->setMinimumHeight(100);
    ui->timelineLayout->addWidget(m_pTimeline);
    ui->timelineLayout->removeWidget(ui->timelineViewPlaceholder);
    delete ui->timelineViewPlaceholder;
    ui->timelineViewPlaceholder = NULL;

    m_pGenerateTraceDialog = new glvdebug_QGenerateTraceDialog(this);
    connect(m_pGenerateTraceDialog, SIGNAL(output_message(QString)), this, SLOT(on_message(QString)));
    connect(m_pGenerateTraceDialog, SIGNAL(output_error(QString)), this, SLOT(on_error(QString)));

    reset_tracefile_ui();
}

glvdebug::~glvdebug()
{
    // update any settings and save the settings file
    g_settings.window_position_left = this->x();
    g_settings.window_position_top = this->y();
    g_settings.window_size_width = this->width();
    g_settings.window_size_height = this->height();
    //g_settings.save(g_SETTINGS_FILE);

    close_trace_file();

    reset_view();

    delete ui;
    glvdebug_output_deinit();
}

int glvdebug::add_custom_state_viewer(QWidget* pWidget, const QString& title, bool bBringToFront)
{
    // TODO: Might need to create / add a layout, add widget to layout, then set layout of the tab
    int tabIndex = ui->stateTabWidget->addTab(pWidget, title);

    if (bBringToFront)
    {
        ui->stateTabWidget->setCurrentWidget(pWidget);
    }

    return tabIndex;
}

QToolButton* glvdebug::add_toolbar_button(const QString& title, bool bEnabled)
{
    QToolButton* pButton = new QToolButton(ui->mainToolBar);
    pButton->setText(title);
    pButton->setEnabled(bEnabled);
    ui->mainToolBar->addWidget(pButton);
    return pButton;
}

void glvdebug::set_calltree_model(glvdebug_QTraceFileModel* pModel)
{
    ui->treeView->setModel(pModel);
    m_pTraceFileModel = pModel;
}

void glvdebug::select_call_at_packet_index(unsigned long long packetIndex)
{
    QModelIndex start = m_pTraceFileModel->index(0, glvdebug_QTraceFileModel::Column_PacketIndex);
    QModelIndexList matches = m_pTraceFileModel->match(start, Qt::DisplayRole, QVariant(packetIndex), 1);
    if (matches.count() > 0)
    {
        ui->treeView->setCurrentIndex(matches[0]);
        ui->treeView->setFocus();
    }
}

void glvdebug::reset_view()
{
    while (ui->stateTabWidget->count() > 0)
    {
        delete ui->stateTabWidget->widget(0);
    }
}

void glvdebug::output_message(QString message, bool bRefresh)
{
    glvdebug_output_message(message.toStdString().c_str(), bRefresh);
}

void glvdebug::output_warning(QString message, bool bRefresh)
{
    glvdebug_output_warning(message.toStdString().c_str(), bRefresh);
}

void glvdebug::output_error(QString message, bool bRefresh)
{
    glvdebug_output_error(message.toStdString().c_str(), bRefresh);
}

glvdebug::Prompt_Result glvdebug::prompt_load_new_trace(const char *tracefile)
{
    int ret = QMessageBox::warning(this, tr(g_PROJECT_NAME.toStdString().c_str()), tr("Would you like to load the new trace file?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    Prompt_Result result = glvdebug_prompt_success;

    if (ret != QMessageBox::Yes)
    {
        // user chose not to open the new trace
        result = glvdebug_prompt_cancelled;
    }
    else
    {
    //    // save current session if there is one
    //    if (m_openFilename.size() > 0 && m_pTraceReader != NULL && m_pApiCallTreeModel != NULL)
    //    {
    //        save_session_to_disk(get_sessionfile_path(m_openFilename, *m_pTraceReader), m_openFilename, m_pTraceReader, m_pApiCallTreeModel);
    //    }

        // close any existing trace
        close_trace_file();

        // try to open the new file
        if (pre_open_trace_file(tracefile) == false)
        {
            glvdebug_output_error("Could not open trace file.");
            QMessageBox::critical(this, tr("Error"), tr("Could not open trace file."));
            result = glvdebug_prompt_error;
        }
    }

    return result;
}

void glvdebug::on_actionE_xit_triggered()
{
    close_trace_file();
    qApp->quit();
}

void glvdebug::on_action_Open_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), QString(),
                                                    tr("GLV Binary Files (*.glv)"));

    if (!fileName.isEmpty())
    {
        if (pre_open_trace_file(fileName) == false)
        {
            QMessageBox::critical(this, tr("Error"), tr("Could not open trace file."));
            return;
        }
    }
}

void glvdebug::on_action_Close_triggered()
{
    close_trace_file();
}

void glvdebug::close_trace_file()
{
    if (m_pController != NULL)
    {
        m_pController->UnloadTraceFile();
        glvdebug_controller_factory::Unload(&m_pController);
    }

    if (m_pTraceFileModel != NULL)
    {
        delete m_pTraceFileModel;
        m_pTraceFileModel = NULL;
    }

    if (m_traceFileInfo.packetCount > 0)
    {
        for (unsigned int i = 0; i < m_traceFileInfo.packetCount; i++)
        {
            if (m_traceFileInfo.pPacketOffsets[i].pHeader != NULL)
            {
                glv_free(m_traceFileInfo.pPacketOffsets[i].pHeader);
                m_traceFileInfo.pPacketOffsets[i].pHeader = NULL;
            }
        }

        GLV_DELETE(m_traceFileInfo.pPacketOffsets);
        m_traceFileInfo.pPacketOffsets = NULL;
        m_traceFileInfo.packetCount = 0;
    }

    if (m_traceFileInfo.pFile != NULL)
    {
        fclose(m_traceFileInfo.pFile);
        m_traceFileInfo.pFile = NULL;
        glv_free(m_traceFileInfo.filename);
        m_traceFileInfo.filename = NULL;
    }

    glvdebug_output_message("Closing trace file.");
    glvdebug_output_message("-------------------");

    setWindowTitle(g_PROJECT_NAME);

    reset_tracefile_ui();

    m_pTimeline->setModel(NULL);
    m_pTimeline->repaint();
}

void glvdebug::on_actionExport_API_Calls_triggered()
{
    QString suggestedName(m_traceFileInfo.filename);

    int lastIndex = suggestedName.lastIndexOf('-');
    if (lastIndex != -1)
    {
        suggestedName = suggestedName.remove(lastIndex, suggestedName.size() - lastIndex);
    }
    suggestedName += "-ApiCalls.txt";

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export API Calls"), suggestedName, tr("Text (*.txt)"));
}

void glvdebug::on_actionEdit_triggered()
{
    //glvdebug_QSettingsDialog dialog(this);
    //int code = dialog.exec();

    //if (code == QDialog::Accepted)
    //{
    //    dialog.save(g_SETTINGS_FILE);
    //}
}

bool glvdebug::pre_open_trace_file(const QString& filename)
{
    if (open_trace_file(filename.toStdString()))
    {
        //// trace file was loaded, now attempt to open additional session data
        //if (load_or_create_session(filename.c_str(), m_pTraceReader) == false)
        //{
        //    // failing to load session data is not critical, but may result in unexpected behavior at times.
        //    glvdebug_output_error("GLVDebug was unable to create a session folder to save debugging information. Functionality may be limited.");
        //}

        return true;
    }

    return false;
}

bool glvdebug::open_trace_file(const std::string &filename)
{
    glvdebug_output_message("*********************");
    glvdebug_output_message("Opening trace file...");
    glvdebug_output_message(filename.c_str());

    QCursor origCursor = this->cursor();
    this->setCursor(Qt::WaitCursor);

    // open trace file and read in header
    memset(&m_traceFileInfo, 0, sizeof(glvdebug_trace_file_info));
    m_traceFileInfo.pFile = fopen(filename.c_str(), "rb");

    bool bOpened = (m_traceFileInfo.pFile != NULL);
    if (bOpened)
    {
        m_traceFileInfo.filename = glv_allocate_and_copy(filename.c_str());
        if (glvdebug_populate_trace_file_info(&m_traceFileInfo) == FALSE)
        {
            glvdebug_output_error("Unable to populate trace file info from file.\n");
            bOpened = false;
        }
        else
        {
            // Make sure trace file version is supported
            if (m_traceFileInfo.header.trace_file_version < GLV_TRACE_FILE_VERSION_MINIMUM_COMPATIBLE)
            {
                glv_LogError("Trace file version %su is older than minimum compatible version (%su).\nYou'll need to make a new trace file, or use an older replayer.\n", m_traceFileInfo.header.trace_file_version, GLV_TRACE_FILE_VERSION_MINIMUM_COMPATIBLE);
            }

            if (!load_controllers(&m_traceFileInfo))
            {
                glvdebug_output_error("Failed to load necessary debug controllers.");
                bOpened = false;
            }
            else
            {
                // interpret the trace file packets
                for (unsigned int i = 0; i < m_traceFileInfo.packetCount; i++)
                {
                    glvdebug_trace_file_packet_offsets* pOffsets = &m_traceFileInfo.pPacketOffsets[i];
                    switch (pOffsets->pHeader->packet_id) {
                        case GLV_TPI_MESSAGE:
                            m_traceFileInfo.pPacketOffsets[i].pHeader = glv_interpret_body_as_trace_packet_message(pOffsets->pHeader)->pHeader;
                            break;
                        case GLV_TPI_MARKER_CHECKPOINT:
                            break;
                        case GLV_TPI_MARKER_API_BOUNDARY:
                            break;
                        case GLV_TPI_MARKER_API_GROUP_BEGIN:
                            break;
                        case GLV_TPI_MARKER_API_GROUP_END:
                            break;
                        case GLV_TPI_MARKER_TERMINATE_PROCESS:
                            break;
                        //TODO processing code for all the above cases
                        default:
                        {
                            glv_trace_packet_header* pHeader = m_pController->InterpretTracePacket(m_traceFileInfo.pPacketOffsets[i].pHeader);
                            assert(pHeader != NULL);
                            m_traceFileInfo.pPacketOffsets[i].pHeader = pHeader;
                        }
                    }
                }
            }

            // populate the UI based on trace file info
            if (bOpened)
            {
                bOpened = m_pController->LoadTraceFile(&m_traceFileInfo, this);
            }
        }


        // TODO: We don't really want to close the trace file yet.
        // I think we want to keep it open so that we can dynamically read from it. 
        // BUT we definitely don't want it to get locked open, so we need a smart
        // way to open / close from it when reading.
        fclose(m_traceFileInfo.pFile);
        m_traceFileInfo.pFile = NULL;
    }

    if (!bOpened)
    {
        glvdebug_output_message("...FAILED!");
        close_trace_file();
    }
    else
    {
        setWindowTitle(QString(filename.c_str()) + " - " + g_PROJECT_NAME);
        glvdebug_output_message("...success!");

        // update toolbar
        //ui->searchTextBox->setEnabled(true);
        //ui->searchPrevButton->setEnabled(true);
        //ui->searchNextButton->setEnabled(true);

        ui->action_Close->setEnabled(true);
        //ui->actionExport_API_Calls->setEnabled(true);

        //ui->prevSnapshotButton->setEnabled(true);
        //ui->nextSnapshotButton->setEnabled(true);
        //ui->prevDrawcallButton->setEnabled(true);
        //ui->nextDrawcallButton->setEnabled(true);
    }

    this->setCursor(origCursor);

    return bOpened;
}

bool glvdebug::load_controllers(glvdebug_trace_file_info* pTraceFileInfo)
{
    if (pTraceFileInfo->header.tracer_count == 0)
    {
        glv_LogError("No API specified in tracefile for replaying.\n");
        return false;
    }

    for (int i = 0; i < pTraceFileInfo->header.tracer_count; i++)
    {
        uint8_t tracerId = pTraceFileInfo->header.tracer_id_array[i];

        const GLV_TRACER_REPLAYER_INFO* pReplayerInfo = &(gs_tracerReplayerInfo[tracerId]);

        if (pReplayerInfo->tracerId != tracerId)
        {
            glv_LogError("Replayer info for TracerId (%d) failed consistency check.\n", tracerId);
            assert(!"TracerId in GLV_TRACER_REPLAYER_INFO does not match the requested tracerId. The array needs to be corrected.");
        }
        else if (strlen(pReplayerInfo->debuggerLibraryname) != 0)
        {
            // Have our factory create the necessary controller
            m_pController = glvdebug_controller_factory::Load(pReplayerInfo->debuggerLibraryname);

            if (m_pController != NULL)
            {
                // Only one controller needs to be loaded, so break from loop
                break;
            }
            else
            {
                // controller failed to be created
                glv_LogError("Couldn't create controller for TracerId %d.\n", tracerId);
            }
        }
    }

    return m_pController != NULL;
}

void glvdebug::reset_tracefile_ui()
{
    ui->action_Close->setEnabled(false);
    //ui->actionExport_API_Calls->setEnabled(false);

    //ui->prevSnapshotButton->setEnabled(false);
    //ui->nextSnapshotButton->setEnabled(false);
    //ui->prevDrawcallButton->setEnabled(false);
    //ui->nextDrawcallButton->setEnabled(false);
    ui->searchTextBox->clear();
    ui->searchTextBox->setEnabled(false);
    ui->searchPrevButton->setEnabled(false);
    ui->searchNextButton->setEnabled(false);

    //GLVDEBUG_DISABLE_BOTTOM_TAB(ui->machineInfoTab);
    //GLVDEBUG_DISABLE_BOTTOM_TAB(ui->callStackTab);
}

void glvdebug::on_stateTreeView_clicked(const QModelIndex &index)
{
}

void glvdebug::slot_treeView_currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    onApiCallSelected(current, false);
}

void glvdebug::on_treeView_clicked(const QModelIndex &index)
{
    onApiCallSelected(index, false);
}

void glvdebug::onApiCallSelected(const QModelIndex &index, bool bAllowStateSnapshot)
{
}

void glvdebug::selectApicallModelIndex(QModelIndex index, bool scrollTo, bool select)
{
    // make sure the index is visible
    QModelIndex parentIndex = index.parent();
    while (parentIndex.isValid())
    {
        if (ui->treeView->isExpanded(parentIndex) == false)
        {
            ui->treeView->expand(parentIndex);
        }
        parentIndex = parentIndex.parent();
    }

    // scroll to the index
    if (scrollTo)
    {
        ui->treeView->scrollTo(index);
    }

    // select the index
    if (select)
    {
        ui->treeView->setCurrentIndex(index);
    }
}

void glvdebug::on_searchTextBox_textChanged(const QString &searchText)
{
    //QPalette palette(ui->searchTextBox->palette());
    //palette.setColor(QPalette::Base, m_searchTextboxBackgroundColor);
    //ui->searchTextBox->setPalette(palette);

    //if (m_pApiCallTreeModel != NULL)
    //{
    //    m_pApiCallTreeModel->set_highlight_search_string(searchText);
    //}

    //// need to briefly give the treeview focus so that it properly redraws and highlights the matching rows
    //// then return focus to the search textbox so that typed keys are not lost
    //ui->treeView->setFocus();
    //ui->searchTextBox->setFocus();
}

void glvdebug::on_searchNextButton_clicked()
{
    //if (m_pApiCallTreeModel != NULL)
    //{
    //    QModelIndex index = m_pApiCallTreeModel->find_next_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
    //    if (index.isValid())
    //    {
    //        selectApicallModelIndex(index, true, true);
    //        ui->treeView->setFocus();
    //    }
    //}
}

void glvdebug::on_searchPrevButton_clicked()
{
    //if (m_pApiCallTreeModel != NULL)
    //{
    //    QModelIndex index = m_pApiCallTreeModel->find_prev_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
    //    if (index.isValid())
    //    {
    //        selectApicallModelIndex(index, true, true);
    //        ui->treeView->setFocus();
    //    }
    //}
}

void glvdebug::on_prevSnapshotButton_clicked()
{
}

void glvdebug::on_nextSnapshotButton_clicked()
{
}

void glvdebug::on_prevDrawcallButton_clicked()
{
}

void glvdebug::on_nextDrawcallButton_clicked()
{
}

void glvdebug::on_searchTextBox_returnPressed()
{
    //if (m_pApiCallTreeModel != NULL)
    //{
    //    QModelIndex index = m_pApiCallTreeModel->find_next_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
    //    if (index.isValid())
    //    {
    //        // a valid item was found, scroll to it and select it
    //        selectApicallModelIndex(index, true, true);
    //    }
    //    else
    //    {
    //        // no items were found, so set the textbox background to red (it will get cleared to the original color if the user edits the search text)
    //        QPalette palette(ui->searchTextBox->palette());
    //        palette.setColor(QPalette::Base, Qt::red);
    //        ui->searchTextBox->setPalette(palette);
    //    }
    //}
}

void glvdebug::on_contextComboBox_currentIndexChanged(int index)
{
}

void glvdebug::on_treeView_activated(const QModelIndex &index)
{
    onApiCallSelected(index, true);
}

void glvdebug::prompt_generate_trace()
{
    bool bShowDialog = true;
    while (bShowDialog)
    {
        int code = m_pGenerateTraceDialog->exec();
        if (code != glvdebug_QGenerateTraceDialog::Succeeded)
        {
            bShowDialog = false;
        }
        else
        {
            QFileInfo fileInfo(m_pGenerateTraceDialog->get_trace_file_path());
            if (code == glvdebug_QGenerateTraceDialog::Succeeded &&
                fileInfo.exists())
            {
                Prompt_Result result = prompt_load_new_trace(fileInfo.canonicalFilePath().toStdString().c_str());
                if (result == glvdebug_prompt_success ||
                        result == glvdebug_prompt_cancelled)
                {
                    bShowDialog = false;
                }
            }
            else
            {
                glvdebug_output_error("Failed to trace the application.");
                QMessageBox::critical(this, "Error", "Failed to trace application.");
            }
        }
    }
}

void glvdebug::on_message(QString message)
{
    glvdebug_output_message(message.toStdString().c_str());
}

void glvdebug::on_error(QString error)
{
    glvdebug_output_error(error.toStdString().c_str());
}

