/**
 * \file
 *
 * \author Valentin Bruder
 *
 * \copyright Copyright (C) 2018 Valentin Bruder
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "src/qt/mainwindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QString>
#include <QUrl>
#include <QFuture>
#include <QtConcurrentRun>
#include <QThread>
#include <QColorDialog>
#include <QtGlobal>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QInputDialog>

/**
 * @brief MainWindow::MainWindow
 * @param parent
 */
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , _fileName("No volume data loaded yet.")
{
    QCoreApplication::setOrganizationName("VISUS");
    QCoreApplication::setOrganizationDomain("www.visus.uni-stuttgart.de");
    QCoreApplication::setApplicationName("VolumeRaycasterCL");
    _settings = new QSettings();

    setAcceptDrops( true );
    ui->setupUi(this);
    ui->gbTimeSeries->setVisible(false);
    connect(ui->volumeRenderWidget, &VolumeRenderWidget::timeSeriesLoaded,
            ui->gbTimeSeries, &QGroupBox::setVisible);
    connect(ui->volumeRenderWidget, &VolumeRenderWidget::timeSeriesLoaded,
            ui->sbTimeStep, &QSpinBox::setMaximum);
    connect(ui->volumeRenderWidget, &VolumeRenderWidget::timeSeriesLoaded,
            ui->sldTimeStep, &QSlider::setMaximum);
    connect(ui->sldTimeStep, &QSlider::valueChanged,
            ui->volumeRenderWidget, &VolumeRenderWidget::setTimeStep);
    connect(ui->pbPlay, &QPushButton::released, this, &MainWindow::setLoopTimesteps);
    connect(ui->sbSpeed, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &MainWindow::setPlaybackSpeed);

    // menu bar actions
    // menu file
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openVolumeFile);
    connect(ui->actionSaveCpTff, &QAction::triggered, this, &MainWindow::saveTff);
    connect(ui->actionSaveRawTff_2, &QAction::triggered, this, &MainWindow::saveRawTff);
    connect(ui->actionLoadCpTff, &QAction::triggered, this, &MainWindow::loadTff);
    connect(ui->actionLoadRawTff, &QAction::triggered, this, &MainWindow::loadRawTff);
    connect(ui->actionSaveState, &QAction::triggered, this, &MainWindow::saveCamState);
    connect(ui->actionLoadState, &QAction::triggered, this, &MainWindow::loadCamState);
    // menu - edit
    connect(ui->actionGenerateLowResVo, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::generateLowResVolume);
    connect(ui->actionSelectOpenCL, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::showSelectOpenCL);
    connect(ui->actionRealoadKernel, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::reloadKernels);
    connect(ui->actionRealoadKernel, &QAction::triggered,
            this, &MainWindow::updateTransferFunctionFromGradientStops);
    // menu - record / play
    connect(ui->actionScreenshot, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::saveFrame);
    connect(ui->actionRecord, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::toggleVideoRecording);
    connect(ui->actionRecordCamera, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::toggleViewRecording);
	connect(ui->actionLogInteraction, &QAction::triggered,
			ui->volumeRenderWidget, &VolumeRenderWidget::toggleInteractionLogging);
    connect(ui->actionPlay_interaction_sequence, &QAction::triggered,
            this, &MainWindow::playInteractionSequence);
    // menu - view
    connect(ui->actionResetCam, &QAction::triggered,
            ui->volumeRenderWidget, &VolumeRenderWidget::resetCam);
    connect(ui->actionShowOverlay, &QAction::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setShowOverlay);
//    connect(ui->actionViewClipping, &QAction::toggled, );
    // menu - rendering
    connect(ui->actionLoad_environment_map, &QAction::triggered,
            this, &MainWindow::loadEnvironmentMap);
    connect(ui->actionSet_background_color, &QAction::triggered,
            this, &MainWindow::chooseBackgroundColor);
    connect(ui->actionInterpolation, &QAction::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setLinearInterpolation);
    connect(ui->actionObjectESS, &QAction::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setObjEss);
    connect(ui->actionImageESS, &QAction::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setImgEss);
    connect(ui->actionShow_skipped, &QAction::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setShowEss);
    // menu - about
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);

    // future watcher for concurrent data loading
    _watcher = new QFutureWatcher<void>(this);
    connect(_watcher, &QFutureWatcher<void>::finished, this, &MainWindow::finishedLoading);
    // loading progress bar
    _progBar.setRange(0, 0);
    _progBar.setTextVisible(true);
    _progBar.setAlignment(Qt::AlignCenter);
    //connect(&_timer, &QTimer::timeout, this, &MainWindow::addProgress);

    // settings UI
    connect(ui->dsbSamplingRate, 
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            ui->volumeRenderWidget, &VolumeRenderWidget::updateSamplingRate);
    connect(ui->dsbImgSampling, 
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            ui->volumeRenderWidget, &VolumeRenderWidget::setImageSamplingRate);
    connect(ui->cbIllum, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            ui->volumeRenderWidget, &VolumeRenderWidget::setIllumination);
    // technique selection
    connect(ui->rbRaycast, &QRadioButton::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::enableRaycast);
    connect(ui->rbPathtrace, &QRadioButton::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::enablePathtrace);
    connect(ui->rbRaycast, &QRadioButton::toggled, this, &MainWindow::showRaycastControls);
    connect(ui->rbPathtrace, &QRadioButton::toggled, this, &MainWindow::showPathtraceControls);
    // render parameters
    connect(ui->chbAmbientOcclusion, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setAmbientOcclusion);
    connect(ui->chbContours, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setContours);
    connect(ui->chbAerial, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setAerial);
    connect(ui->chbOrtho, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setCamOrtho);
    connect(ui->chbContRendering, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setContRendering);
    connect(ui->chbGradient, &QCheckBox::toggled,
            ui->volumeRenderWidget, &VolumeRenderWidget::setUseGradient);
    connect(ui->dsbExtinction,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            ui->volumeRenderWidget, &VolumeRenderWidget::setExtinction);
    // tff editor
    connect(ui->transferFunctionEditor->getEditor(), &TransferFunctionEditor::gradientStopsChanged,
            ui->volumeRenderWidget, &VolumeRenderWidget::updateTransferFunction);
    connect(ui->pbResetTff, &QPushButton::clicked,
            ui->transferFunctionEditor, &TransferFunctionWidget::resetTransferFunction);
    connect(ui->cbTffInterpolation, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::setInterpolation);
    connect(ui->transferFunctionEditor->getEditor(), &TransferFunctionEditor::selectedPointChanged,
            ui->colorWheel, &colorwidgets::ColorWheel::setColor);
    connect(ui->colorWheel, &colorwidgets::ColorWheel::colorChanged,
            ui->transferFunctionEditor, &TransferFunctionWidget::setColorSelected);
    connect(ui->cbLog, &QCheckBox::toggled, this, &MainWindow::updateHistogram);
    connect(ui->sldTimeStep, &QSlider::valueChanged, this, &MainWindow::updateHistogram);
    // clipping sliders
    connect(ui->sldClipBack, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->sldClipBottom, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->sldClipFront, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->sldClipLeft, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->sldClipRight, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->sldClipTop, &QSlider::valueChanged, this, &MainWindow::updateBBox);
    connect(ui->pbResetClipping, &QPushButton::pressed, this, &MainWindow::resetBBox);
    connect(ui->chbClipping, &QCheckBox::toggled, this, &MainWindow::enableClipping);
    ui->dockClipping->setVisible(false);

    ui->statusBar->addPermanentWidget(&_statusLabel);
    connect(ui->volumeRenderWidget, &VolumeRenderWidget::frameSizeChanged,
            this, &MainWindow::setStatusText);

    connect(&_loopTimer, &QTimer::timeout, this, &MainWindow::nextTimestep);

    showRaycastControls();
    // restore settings
    readSettings();
}


/**
 * @brief MainWindow::~MainWindow
 */
MainWindow::~MainWindow()
{
    delete _watcher;
    delete _settings;
    delete ui;
}


/**
 * @brief MainWindow::closeEvent
 * @param event
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    float factor = 0.01f;
    if (event->modifiers() & Qt::ShiftModifier)
        factor = 0.001f;

    switch (event->key())
    {
        case Qt::Key_Up:
        case Qt::Key_W: ui->volumeRenderWidget->updateView(+0.000f,-factor); break;
        case Qt::Key_Left:
        case Qt::Key_A: ui->volumeRenderWidget->updateView(-factor, 0.000f); break;
        case Qt::Key_Down:
        case Qt::Key_S: ui->volumeRenderWidget->updateView(+0.000f,+factor); break;
        case Qt::Key_Right:
        case Qt::Key_D: ui->volumeRenderWidget->updateView(+factor, 0.000f); break;
        // TODO: zoom
    }
    event->accept();
}


/**
 * @brief MainWindow::showEvent
 * @param event
 */
void MainWindow::showEvent(QShowEvent *event)
{
    ui->transferFunctionEditor->resetTransferFunction();
    event->accept();
}


/**
 * @brief MainWindow::writeSettings
 */
void MainWindow::writeSettings()
{
    _settings->beginGroup("MainWindow");
    _settings->setValue("geometry", saveGeometry());
    _settings->setValue("windowState", saveState());
    _settings->endGroup();

    _settings->beginGroup("Settings");
    // TODO
    _settings->endGroup();
}


/**
 * @brief MainWindow::readSettings
 */
void MainWindow::readSettings()
{
    _settings->beginGroup("MainWindow");
    restoreGeometry(_settings->value("geometry").toByteArray());
    restoreState(_settings->value("windowState").toByteArray());
    _settings->endGroup();

    _settings->beginGroup("Settings");
    // todo
    _settings->endGroup();
}


/**
 * @brief MainWindow::setVolumeData
 * @param fileName
 */
void MainWindow::setVolumeData(const DatRawReader::Properties volumeFileProps)
{
    ui->volumeRenderWidget->setVolumeData(volumeFileProps);
    ui->volumeRenderWidget->updateView();
}


/**
 * @brief MainWindow::updateTransferFunction
 */
void MainWindow::updateTransferFunctionFromGradientStops()
{
    ui->volumeRenderWidget->updateTransferFunction(
                ui->transferFunctionEditor->getEditor()->getGradientStops());
}

/**
 * @brief MainWindow::setLoopTimesteps
 */
void MainWindow::setLoopTimesteps()
{
    if (!_loopTimer.isActive())
    {
        _loopTimer.start(ui->sbSpeed->value());
        ui->pbPlay->setIcon(QIcon::fromTheme("media-playback-pause"));
    }
    else
    {
        _loopTimer.stop();
        ui->pbPlay->setIcon(QIcon::fromTheme("media-playback-start"));
    }
}

/**
 * @brief MainWindow::setPlaybackSpeed
 * @param speed
 */
void MainWindow::setPlaybackSpeed(int speed)
{
    _loopTimer.setInterval(speed);
}

/**
 * @brief MainWindow::nextTimestep
 */
void MainWindow::nextTimestep()
{
    int val = ui->sbTimeStep->value() + 1;
    if (val > ui->sbTimeStep->maximum() && ui->chbLoop->isChecked())
        val = 0;
    else if (val > ui->sbTimeStep->maximum())
    {
        _loopTimer.stop();
        val = ui->sbTimeStep->maximum();
    }
    ui->sldTimeStep->setValue(val);
    ui->sbTimeStep->setValue(val);
}


/**
 * @brief MainWindow::loadEnvironmentMap
 */
void MainWindow::loadEnvironmentMap()
{
    QFileDialog dialog;
    QString defaultPath = _settings->value( "LastEnvironmentFile" ).toString();
    QString pickedFile = dialog.getOpenFileName(this, tr("Load environment map file"),
                                                defaultPath, tr("HDR files (*.hdr)"));
    if (pickedFile.isEmpty())
        return;
    _settings->setValue( "LastEnvironmentFile", pickedFile );

    ui->volumeRenderWidget->setEnvironmentMap(pickedFile);
//    QFile loadFile(pickedFile);
//    if (!loadFile.open(QIODevice::ReadOnly))
//    {
//        qWarning() << "Couldn't open environment map" << pickedFile;
//        return;
//    }
}

/**
 * @brief MainWindow::loadCamState
 */
void MainWindow::loadCamState()
{
    QFileDialog dialog;
    QString defaultPath = _settings->value( "LastStateFile" ).toString();
    QString pickedFile = dialog.getOpenFileName(this, tr("Load state"),
                                                defaultPath, tr("JSON files (*.json)"));
    if (pickedFile.isEmpty())
        return;
    _settings->setValue( "LastStateFile", pickedFile );

    QFile loadFile(pickedFile);
    if (!loadFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "Couldn't open state file" << pickedFile;
        return;
    }

    QByteArray saveData = loadFile.readAll();
    QJsonDocument loadDoc(QJsonDocument::fromJson(saveData));
    QJsonObject json = loadDoc.object();

    if (json.contains("imgResFactor") && json["imgResFactor"].isDouble())
            ui->dsbImgSampling->setValue(json["imgResFactor"].toDouble());
    if (json.contains("rayStepSize") && json["rayStepSize"].isDouble())
            ui->dsbSamplingRate->setValue(json["rayStepSize"].toDouble());

    if (json.contains("useLerp") && json["useLerp"].isBool())
            ui->actionInterpolation->setChecked(json["useLerp"].toBool());
    if (json.contains("useAO") && json["useAO"].isBool())
            ui->chbAmbientOcclusion->setChecked(json["useAO"].toBool());
    if (json.contains("showContours") && json["showContours"].isBool())
            ui->chbContours->setChecked(json["showContours"].toBool());
    if (json.contains("useAerial") && json["useAerial"].isBool())
            ui->chbAerial->setChecked(json["useAerial"].toBool());
    if (json.contains("showBox") && json["showBox"].isBool())
            ui->actionShow_skipped->setChecked(json["showBox"].toBool());
    if (json.contains("useOrtho") && json["useOrtho"].isBool())
            ui->chbOrtho->setChecked(json["useOrtho"].toBool());
    // camera paramters
    ui->volumeRenderWidget->read(json);
}

/**
 * @brief MainWindow::saveCamState
 */
void MainWindow::saveCamState()
{
    QFileDialog dialog;
    QString defaultPath = _settings->value( "LastStateFile" ).toString();
    QString pickedFile = dialog.getSaveFileName(this, tr("Save State"),
                                                defaultPath, tr("JSON files (*.json)"));
    if (pickedFile.isEmpty())
        return;
    _settings->setValue( "LastStateFile", pickedFile );

    QFile saveFile(pickedFile);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        qWarning() << "Couldn't open save file" << pickedFile;
        return;
    }

    QJsonObject stateObject;
    // resolution
    stateObject["imgResFactor"] = ui->dsbImgSampling->value();
    stateObject["rayStepSize"] = ui->dsbSamplingRate->value();
    // rendering flags
    stateObject["useLerp"] = ui->actionInterpolation->isChecked();
    stateObject["useAO"] = ui->chbAmbientOcclusion->isChecked();
    stateObject["showContours"] = ui->chbContours->isChecked();
    stateObject["useAerial"] = ui->chbAerial->isChecked();
    stateObject["showBox"] = ui->actionShow_skipped->isChecked();
    stateObject["useOrtho"] = ui->chbOrtho->isChecked();
    // camera parameters
    ui->volumeRenderWidget->write(stateObject);

    QJsonDocument saveDoc(stateObject);
    saveFile.write(saveDoc.toJson());
}

/**
 * @brief MainWindow::showAboutDialog
 */
void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About VolumeRendererCL",
    "<b>OpenCL Volume Renderer</b><br><br>\
    Check out the \
    <a href='https://github.com/vbruder/VolumeRendererCL'>GitHub repository</a> \
    for more information.<br><br>\
    Copyright 2017-2019 Valentin Bruder. All rights reserved. <br><br>\
    The program is provided AS IS with NO WARRANTY OF ANY KIND, \
    INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS \
    FOR A PARTICULAR PURPOSE.");
}


/**
 * @brief Try to infer volume resolution by using the cube root.
 * @param file_size File size in bit.
 * @param format Data precision (UCHAR, USHORT or FLOAT).
 * @return The inferred value.
 */
int infer_volume_resolution(qint64 &file_size, const DatRawReader::data_format &format)
{
    if (format == DatRawReader::data_format::UCHAR)
        file_size /= sizeof(unsigned char);
    else if (format == DatRawReader::data_format::USHORT)
        file_size /= sizeof(unsigned short);
    else if (format == DatRawReader::data_format::FLOAT)
        file_size /= sizeof(float);
    else if (format == DatRawReader::data_format::DOUBLE)
        file_size /= sizeof(double);
    else // (format == "UCHAR")
        file_size /= sizeof(unsigned char); // default

    return static_cast<int>(std::cbrt(file_size));
}

/**
 * @brief MainWindow::showVolumePropertyDialog
 * @param fileName Name of the selected file.
 */
DatRawReader::Properties MainWindow::showVolumePropertyDialog(const QString &fileName)
{
    DatRawReader::Properties p;

    bool ok;
    QStringList items;
    items << tr("UCHAR") << tr("USHORT") << tr("FLOAT");
    QString format = QInputDialog::getItem(this, tr("QInputDialog::getItem()"),
                                     tr("Format:"), items, 0, false, &ok);
    p.format = static_cast<DatRawReader::data_format>(items.indexOf(format));

    items.clear();
    items << tr("Little") << tr("Big");
    QString endianness = QInputDialog::getItem(this, tr("QInputDialog::getItem()"),
                                               tr("Endianness:"), items, 0, false, &ok);
    p.endianness = static_cast<DatRawReader::data_endianness>(items.indexOf(endianness));

    qint64 fileSize = QFile(fileName).size();
    int inferredValue = infer_volume_resolution(fileSize, p.format);
    int max3DimageSize = 16384; // TODO: query this value from the used OpenCL device
    p.volume_res.at(0) = uint(QInputDialog::getInt(this, tr("Volume resolution in x direction"),
                              tr("Resolution in X:"), 1, inferredValue, max3DimageSize, 1, &ok));
    p.volume_res.at(1) = uint(QInputDialog::getInt(this, tr("Volume resolution in y direction"),
                              tr("Resolution in Y:"), 1, int(p.volume_res.at(0)), max3DimageSize, 1, &ok));
    p.volume_res.at(2) = uint(QInputDialog::getInt(this, tr("Volume resolution in z direction"),
                              tr("Resolution in Z:"), 1,
                              int(fileSize/p.volume_res.at(0)/p.volume_res.at(1)), max3DimageSize, 1, &ok));

    p.slice_thickness.at(0) = QInputDialog::getDouble(this, tr("Slice thickness in x direction"),
                                    tr("Slice thickness in X:"), 1.0, 0.0, 100.0, 6, &ok);
    p.slice_thickness.at(1) = QInputDialog::getDouble(this, tr("Slice thickness in y direction"),
                                 tr("Slice thickness in Y:"), p.slice_thickness.at(0), 0.0, 100.0, 6, &ok);
    p.slice_thickness.at(2) = QInputDialog::getDouble(this, tr("Slice thickness in z direction"),
                                 tr("Slice thickness in Z:"), p.slice_thickness.at(0), 0.0, 100.0, 6, &ok);
    return p;
}

/**
 * @brief MainWindow::readVolumeFile
 * @param fileName
 * @return
 */
bool MainWindow::readVolumeFile(const QUrl &url)
{
    QFileInfo finf(url.fileName());
    QString fileName = url.path();
#ifdef _WIN32
    // remove leading / if present (windows)
    if (fileName.startsWith('/'))
        fileName.remove(0, 1);
#endif
    if (fileName.isEmpty())
    {
        _progBar.deleteLater();
        throw std::invalid_argument("Invalid volume data file name.");
    }
    qInfo() << "Loading volume data file" << fileName;

    DatRawReader::Properties volumeFileProps;
    if (finf.suffix() == "raw")
    {
        volumeFileProps = showVolumePropertyDialog(fileName);
        volumeFileProps.raw_file_names.push_back(fileName.toStdString());
    }
    else
    {
        volumeFileProps.dat_file_name = fileName.toStdString();
    }

    ui->volumeRenderWidget->setLoadingFinished(false);
    _progBar.setFormat("Loading volume file: " + fileName);
   // _progBar.setValue(1);
    _progBar.show();
    ui->statusBar->addPermanentWidget(&_progBar, 2);
    ui->statusBar->updateGeometry();
    QApplication::processEvents();

    _fileName = fileName;
    QFuture<void> future = QtConcurrent::run(this, &MainWindow::setVolumeData, volumeFileProps);
    _watcher->setFuture(future);
    _timer.start(100);

    return true;
}


/**
 * @brief MainWindow::readTff
 * @param fileName
 * @return
 */
void MainWindow::readTff(const QString &fileName)
{
    if (fileName.isEmpty())
    {
        throw std::invalid_argument("Invalid trtansfer function file name.");
    }
    else
    {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            throw std::invalid_argument("Could not open transfer function file "
                                        + fileName.toStdString());
        }
        else
        {
            QTextStream in(&file);
            QGradientStops stops;
            while (!in.atEnd())
            {
                QStringList line = in.readLine().split(QRegExp("\\s"));
                if (line.size() < 5)
                    continue;
                QGradientStop stop(line.at(0).toDouble(),
                                   QColor(line.at(1).toInt(), line.at(2).toInt(),
                                          line.at(3).toInt(), line.at(4).toInt()));
                stops.push_back(stop);
            }
            if (!stops.isEmpty())
            {
                ui->transferFunctionEditor->getEditor()->setGradientStops(stops);
                ui->transferFunctionEditor->getEditor()->pointsUpdated();
            }
            else
                qCritical() << "Empty transfer function file.";
            file.close();
        }
    }
}


/**
 * @brief MainWindow::saveTff
 * @param fileName
 */
void MainWindow::saveTff()
{
    QFileDialog dia;
    QString defaultPath = _settings->value( "LastTffFile" ).toString();
    QString pickedFile = dia.getSaveFileName(
                this, tr("Save Transfer Function"),
                defaultPath, tr("Transfer function files (*.tff)"));

    if (!pickedFile.isEmpty())
    {
        QFile file(pickedFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            throw std::invalid_argument("Could not open file "
                                        + pickedFile.toStdString());
        }
        else
        {
            QTextStream out(&file);
            const QGradientStops stops =
                    ui->transferFunctionEditor->getEditor()->getGradientStops();
            foreach (QGradientStop s, stops)
            {
                out << s.first << " " << s.second.red() << " " << s.second.green()
                    << " " << s.second.blue() << " " << s.second.alpha() << "\n";
            }
            file.close();
        }
    }
}

/**
 * @brief MainWindow::saveRawTff
 */
void MainWindow::saveRawTff()
{
    QFileDialog dia;
    QString defaultPath = _settings->value( "LastRawTffFile" ).toString();
    QString pickedFile = dia.getSaveFileName(
                this, tr("Save Transfer Function"),
                defaultPath, tr("Transfer function files (*.tff)"));

    if (!pickedFile.isEmpty())
    {
        QFile file(pickedFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            throw std::invalid_argument("Could not open file "
                                        + pickedFile.toStdString());
        }
        else
        {
            QTextStream out(&file);
            const QGradientStops stops =
                    ui->transferFunctionEditor->getEditor()->getGradientStops();
            const std::vector<unsigned char> tff = ui->volumeRenderWidget->getRawTransferFunction(stops);
            foreach (unsigned char c, tff)
            {
                out << int(c) << " ";
            }
            file.close();
        }
    }
}


/**
 * @brief MainWindow::saveRawTff
 */
void MainWindow::loadRawTff()
{
    QFileDialog dia;
    QString defaultPath = _settings->value( "LastRawTffFile" ).toString();
    QString pickedFile = dia.getOpenFileName(
                this, tr("Open Transfer Function"),
                defaultPath, tr("Transfer function files (*.tff)"));
    if (!pickedFile.isEmpty())
    {
        qDebug() << "Loading transfer funtion data defined in" << pickedFile;
        std::ifstream tff_file(pickedFile.toStdString(), std::ios::in);
        float value = 0;
        std::vector<unsigned char> values;

        // read lines from file and split on whitespace
        if (tff_file.is_open())
        {
            while (tff_file >> value)
            {
                values.push_back(static_cast<unsigned char>(value));
            }
            tff_file.close();
            ui->volumeRenderWidget->setRawTransferFunction(values);
        }
        else
        {
            qDebug() << "Could not open transfer function file " + pickedFile;
        }
        _settings->setValue( "LastRawTffFile", pickedFile );
    }
}


/**
 * @brief MainWindow::getStatus
 * @return
 */
void MainWindow::setStatusText()
{
    QString status = "No data loaded yet.";
    if (ui->volumeRenderWidget->hasData())
    {
        status = "File: ";
        status += _fileName;
        status += " | Volume: ";
        status += QString::number(double(ui->volumeRenderWidget->getVolumeResolution().x()));
        status += "x";
        status += QString::number(double(ui->volumeRenderWidget->getVolumeResolution().y()));
        status += "x";
        status += QString::number(double(ui->volumeRenderWidget->getVolumeResolution().z()));
        status += "x";
        status += QString::number(double(ui->volumeRenderWidget->getVolumeResolution().w()));
        status += " | Frame: ";
        status += QString::number(ui->volumeRenderWidget->size().width());
        status += "x";
        status += QString::number(ui->volumeRenderWidget->size().height());
        status += " ";
    }
    _statusLabel.setText(status);
}

/**
 * @brief MainWindow::updateHistogram
 */
void MainWindow::updateHistogram()
{
    if (!ui->volumeRenderWidget->hasData())
        return;
    unsigned int t = 0u;
    if (ui->volumeRenderWidget->getVolumeResolution().w() > 1)
            t = static_cast<unsigned int>(ui->sbTimeStep->value());
    std::array<double, 256> histo = ui->volumeRenderWidget->getHistogram(t);
    double maxVal = *std::max_element(histo.begin() + 1, histo.end());
    double minVal = *std::min_element(histo.begin() + 1, histo.end());
    QVector<qreal> qhisto;
    for (auto &a : histo)
    {
        if (ui->cbLog->isChecked())
            qhisto.push_back(log(a - minVal) / log(maxVal));
        else
            qhisto.push_back(a / maxVal);
    }
    ui->transferFunctionEditor->setHistogram(qhisto); // normalized to range [0,1]
}

/**
 * @brief MainWindow::finishedLoading
 */
void MainWindow::finishedLoading()
{
    //_progBar.setValue(100);
    _progBar.hide();
    _timer.stop();
    this->setStatusText();
    ui->volumeRenderWidget->setLoadingFinished(true);
    ui->volumeRenderWidget->updateView();
    updateHistogram();
    updateClippingSliders();
}

/**
 * @brief MainWindow::addProgress
 */
void MainWindow::addProgress()
{
    if (_progBar.value() < _progBar.maximum() - 5)
        _progBar.setValue(_progBar.value() + 1);
}

/**
 * @brief MainWindow::updateClippingSliders
 */
void MainWindow::updateClippingSliders()
{
    QVector4D volRes = ui->volumeRenderWidget->getVolumeResolution() - QVector4D(1,1,1,0);

    ui->sldClipRight->setMaximum(int(volRes.x()));
    ui->sbClipRight->setMaximum(int(volRes.x()));
    ui->sldClipLeft->setMaximum(int(volRes.x()));
    ui->sbClipLeft->setMaximum(int(volRes.x()));
    ui->sldClipFront->setMaximum(int(volRes.z()));
    ui->sbClipFront->setMaximum(int(volRes.z()));
    ui->sldClipBack->setMaximum(int(volRes.z()));
    ui->sbClipBack->setMaximum(int(volRes.z()));
    ui->sldClipBottom->setMaximum(int(volRes.y()));
    ui->sbClipBottom->setMaximum(int(volRes.y()));
    ui->sldClipTop->setMaximum(int(volRes.y()));
    ui->sbClipTop->setMaximum(int(volRes.y()));

    ui->sldClipRight->setValue(ui->sldClipRight->maximum());
    ui->sldClipBack->setValue(ui->sldClipBack->maximum());
    ui->sldClipTop->setValue(ui->sldClipTop->maximum());
}

/**
 * @brief MainWindow::loadTff
 */
void MainWindow::loadTff()
{
    QFileDialog dia;
    QString defaultPath = _settings->value( "LastTffFile" ).toString();
    QString pickedFile = dia.getOpenFileName(
                this, tr("Open Transfer Function"),
                defaultPath, tr("Transfer function files (*.tff)"));
    if (!pickedFile.isEmpty())
    {
        readTff(pickedFile);
        _settings->setValue( "LastTffFile", pickedFile );
    }
}


/**
 * @brief MainWindow::openVolumeFile
 */
void MainWindow::openVolumeFile()
{
    QFileDialog dialog;
    QString defaultPath = _settings->value( "LastVolumeFile" ).toString();
    QString pickedFile = dialog.getOpenFileName(
                this, tr("Open Volume Data"), defaultPath,
                tr("Volume data files (*.dat); Volume raw files (*.raw); All files (*)"));
    if (!pickedFile.isEmpty())
    {
        if (!readVolumeFile(pickedFile))
        {
            QMessageBox msgBox;
            msgBox.setIcon( QMessageBox::Critical );
            msgBox.setText( "Error while trying to create OpenCL memory objects." );
            msgBox.exec();
        }
        else
        {
            _settings->setValue( "LastVolumeFile", pickedFile );
        }
    }
}


/**
 * @brief MainWindow::dragEnterEvent
 * @param ev
 */
void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls())
    {
        bool valid = false;
        foreach(QUrl url, ev->mimeData()->urls())
        {
            if (!url.fileName().isEmpty())
            {
                QFileInfo finf(url.fileName());
                if (finf.suffix() == "dat" || finf.suffix() == "raw" )
                    valid = true;
            }
        }
        if (valid)
        {
            ev->acceptProposedAction();
        }
    }
}

/**
 * @brief MainWindow::dropEvent
 * @param ev
 */
void MainWindow::dropEvent(QDropEvent *ev)
{
    foreach(QUrl url, ev->mimeData()->urls())
        readVolumeFile(url);
}

/**
 * @brief MainWindow::chooseBackgroundColor
 */
void MainWindow::chooseBackgroundColor()
{
    QColorDialog dia;
    QColor col = dia.getColor();
    if (col.isValid())
        ui->volumeRenderWidget->setBackgroundColor(col);
}

/**
 * @brief MainWindow::playInteractionSequence
 */
void MainWindow::playInteractionSequence()
{
    QFileDialog dia;
    QString defaultPath = _settings->value( "LastInteractionSequence" ).toString();
    QString pickedFile = dia.getOpenFileName(
                this, tr("Open Interaction Sequence"),
                defaultPath, tr("Interaction sequence files (*.csv)"));
    if (!pickedFile.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setText("Do you wish to record the frames from the interaction sequence?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        int ret = msgBox.exec();
        ui->volumeRenderWidget->playInteractionSequence(pickedFile, ret == QMessageBox::Yes);
        _settings->setValue( "LastInteractionSequence", pickedFile );
    }
}

/**
 * @brief MainWindow::setInterpolation
 */
void MainWindow::setInterpolation(const int index)
{
    QEasingCurve::Type interpolation = QEasingCurve::Linear;
    switch (index)
    {
        case 0: interpolation = QEasingCurve::Linear; break;
        case 1: interpolation = QEasingCurve::InOutQuad; break;
        case 2: interpolation = QEasingCurve::InOutCubic; break;
        default: break;
    }
    ui->volumeRenderWidget->setTffInterpolation(interpolation);
    ui->transferFunctionEditor->setInterpolation(interpolation);
}

/**
 * @brief MainWindow::showRaycastControls
 */
void MainWindow::showRaycastControls()
{
    ui->chbContours->setVisible(true);
    ui->chbAmbientOcclusion->setVisible(true);
    ui->chbAerial->setVisible(true);
    ui->lblSamplingRate->setVisible(true);
    ui->lblImgSampling->setVisible(true);
    ui->dsbImgSampling->setVisible(true);
    ui->lblRaySampling->setVisible(true);
    ui->dsbSamplingRate->setVisible(true);
    ui->cbIllum->setVisible(true);

    ui->dsbExtinction->setVisible(false);
    ui->lblExtinction->setVisible(false);
}

/**
 * @brief MainWindow::showPathtraceControls
 */
void MainWindow::showPathtraceControls()
{
    ui->chbContours->setVisible(false);
    ui->chbAmbientOcclusion->setVisible(false);
    ui->chbAerial->setVisible(false);
    ui->lblSamplingRate->setVisible(false);
    ui->lblImgSampling->setVisible(false);
    ui->dsbImgSampling->setVisible(false);
    ui->lblRaySampling->setVisible(false);
    ui->dsbSamplingRate->setVisible(false);
    ui->cbIllum->setVisible(false);

    ui->dsbExtinction->setVisible(true);
    ui->lblExtinction->setVisible(true);
}

/**
 * @brief MainWindow::updateBBox
 */
void MainWindow::updateBBox()
{
    QVector3D botLeft(ui->sbClipLeft->value(), ui->sbClipBottom->value(),
                      ui->sbClipFront->value());
    QVector3D topRight(ui->sbClipRight->value(), ui->sbClipTop->value(),
                       ui->sbClipBack->value());
    if (ui->chbClipping->isChecked())
        ui->volumeRenderWidget->setBBox(botLeft, topRight);
}

/**
 * @brief MainWindow::resetBBox
 */
void MainWindow::resetBBox()
{
    ui->sldClipLeft->setValue(0);
    ui->sldClipFront->setValue(0);
    ui->sldClipBottom->setValue(0);
    ui->sldClipRight->setValue(ui->sldClipRight->maximum());
    ui->sldClipTop->setValue(ui->sldClipTop->maximum());
    ui->sldClipBack->setValue(ui->sldClipBack->maximum());
    updateBBox();
}

/**
 * @brief MainWindow::enableClipping
 * @param checked
 */
void MainWindow::enableClipping(bool checked)
{
    if (!checked)
    {
        QVector3D maxRes(ui->sldClipRight->maximum(),
                         ui->sldClipTop->maximum(),
                         ui->sldClipBack->maximum());
        ui->volumeRenderWidget->setBBox(QVector3D(0,0,0), maxRes);
    }
    else
    {
        updateBBox();
    }
}
