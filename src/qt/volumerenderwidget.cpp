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

#include "src/qt/volumerenderwidget.h"

#include <QPainter>
#include <QGradient>
#include <QImage>
#include <QCoreApplication>
#include <QScreen>
#include <QInputDialog>
#include <QJsonObject>
#include <QErrorMessage>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QFileDialog>
#include <QTime>

#include <algorithm>

const static double Z_NEAR = 1.0;
const static double Z_FAR = 500.0;

static const QString VsScreenQuadSource =
    "#version 330\n"
    "layout(location = 0) in vec3 vertex;\n"
    "out vec2 texCoord;\n"
    "uniform mat4 projMatrix;\n"
    "uniform mat4 mvMatrix;\n"
    "void main() {\n"
    "   texCoord = vec2(0.5f) + 0.5f * vertex.xy;\n"
    "   gl_Position = projMatrix * mvMatrix * vec4(vertex.xy, 1.0, 1.0);\n"
    "}\n";

static const QString FsScreenQuadSource =
    "#version 330\n"
    "in highp vec2 texCoord;\n"
    "out highp vec4 fragColor;\n"
    "uniform highp int width;\n"
    "uniform highp int height;\n"
    "uniform highp sampler2D outTex;\n"     
    "void main() {\n"
    "   vec2 os = vec2(1.0)/vec2(width, height);"
    "   vec3 color = texture(outTex, vec2(texCoord.x, texCoord.y)).xyz;\n"
    "   bool gaussFilter = false;\n"
    "   if (gaussFilter)\n"
    "   {\n"
    "       color *= 0.6;\n"
    "       color += 0.1 * texture(outTex, vec2(texCoord.x, texCoord.y+os.y)).xyz;\n"
    "       color += 0.1 * texture(outTex, vec2(texCoord.x, texCoord.y-os.y)).xyz;\n"
    "       color += 0.1 * texture(outTex, vec2(texCoord.x+os.x, texCoord.y)).xyz;\n"
    "       color += 0.1 * texture(outTex, vec2(texCoord.x-os.x, texCoord.y)).xyz;\n"
    "       //color += 0.05 * texture(outTex, vec2(texCoord.x+os.x, texCoord.y+os.y)).xyz;\n"
    "       //color += 0.05 * texture(outTex, vec2(texCoord.x-os.x, texCoord.y-os.y)).xyz;\n"
    "       //color += 0.05 * texture(outTex, vec2(texCoord.x+os.x, texCoord.y-os.y)).xyz;\n"
    "       //color += 0.05 * texture(outTex, vec2(texCoord.x-os.x, texCoord.y+os.y)).xyz;\n"
    "   }\n"
    "   fragColor.xyz = color;\n"
    "   //fragColor = pow(fragColor, vec4(1/2.2)); // gamma correction \n"
    "   fragColor.a = 1.0;\n"
    "}\n";


/**
 * @brief VolumeRenderWidget::VolumeRenderWidget
 * @param parent
 */
VolumeRenderWidget::VolumeRenderWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , _tffRange(QPoint(0, 255))
    , _timestep(0)
    , _lastLocalCursorPos(QPoint(0,0))
    , _rotQuat(QQuaternion(1, 0, 0, 0))
    , _translation(QVector3D(0, 0, 2.0))
    , _noUpdate(true)
    , _loadingFinished(false)
    , _writeImage(false)
    , _recordVideo(false)
    , _imgCount(0)
    , _imgSamplingRate(1)
    , _useGL(true)
    , _showOverlay(true)
    , _logView(false)
	, _logInteraction(false)
    , _contRendering(false)
{
    this->setMouseTracking(true);
}


/**
 * @brief VolumeRenderWidget::~VolumeRenderWidget
 */
VolumeRenderWidget::~VolumeRenderWidget()
{
}

static void drawLineFloat(QPainter &p, float x1, float y1, float x2, float y2)
{
    p.drawLine(int(x1), int(y1), int(x2), int(y2));
}

/**
 * @brief VolumeRenderWidget::paintOrientationAxis
 */
void VolumeRenderWidget::paintOrientationAxis(QPainter &p)
{
    QMatrix4x4 proj;
    proj.perspective(53.14f, 1.0f, 0.1f, 1.0);
    QMatrix4x4 viewProj(proj * _coordViewMX);
    QVector4D x =         viewProj * QVector4D( 20,  0,  0, 0);
    QVector4D xArrLeft =  viewProj * QVector4D( 16, -2,  0, 0);
    QVector4D xArrRight = viewProj * QVector4D( 16, +2,  0, 0);
    QVector4D y =         viewProj * QVector4D(  0, 20,  0, 0);
    QVector4D yArrLeft =  viewProj * QVector4D( -2, 16,  0, 0);
    QVector4D yArrRight = viewProj * QVector4D( +2, 16,  0, 0);
    QVector4D z =         viewProj * QVector4D(  0,  0, 20, 0);
    QVector4D zArrLeft =  viewProj * QVector4D( -2,  0, 16, 0);
    QVector4D zArrRight = viewProj * QVector4D( +2,  0, 16, 0);

    p.resetTransform();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.translate(66, height() - 66);
    int textOffset = 5;
    // x axis
    p.setPen(Qt::red);
    p.drawLine(0, 0, int(x.x()), int(x.y()));
    p.drawLine(int(xArrLeft.x()), int(xArrLeft.y()), int(x.x()), int(x.y()));
    p.drawLine(int(xArrRight.x()), int(xArrRight.y()), int(x.x()), int(x.y()));
    p.drawText(int(x.x()) + textOffset, int(x.y()) + textOffset, "x");
    // y axis
    p.setPen(Qt::green);
    drawLineFloat(p, 0, 0, y.x(), y.y());
    drawLineFloat(p, yArrLeft.x(), yArrLeft.y(), y.x(), y.y());
    drawLineFloat(p, yArrRight.x(), yArrRight.y(), y.x(), y.y());
    p.drawText(int(y.x()) + textOffset, int(y.y()) + textOffset, "y");
    // z axis
    p.setPen(Qt::blue);
    drawLineFloat(p, 0, 0, z.x(), z.y());
    drawLineFloat(p, zArrLeft.x(), zArrLeft.y(), z.x(), z.y());
    drawLineFloat(p, zArrRight.x(), zArrRight.y(), z.x(), z.y());
    p.drawText(int(z.x()) + textOffset, int(z.y()) + textOffset, "z");
}


/**
 * @brief paintFPS
 */
void VolumeRenderWidget::paintFps(QPainter &p, const double fps, const double lastTime)
{
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setPen(Qt::darkGreen);
    p.setFont(QFont("Helvetica", 11));
    QString s = "FPS: " + QString::number(fps);
    p.drawText(10, 20, s);
    s = "Last: " + QString::number(lastTime);
    p.drawText(10, 36, s);
    s = QString(_volumerender.getCurrentDeviceName().c_str());
    p.drawText(10, 52, s);
}


/**
 * @brief VolumeRenderWidget::initializeGL
 */
void VolumeRenderWidget::initializeGL()
{
    connect( context(), &QOpenGLContext::aboutToBeDestroyed, this,
             &VolumeRenderWidget::cleanup );

    initializeOpenGLFunctions();
    makeCurrent();

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    QString versionedVsSrc = VsScreenQuadSource;
    QString versionedFsSrc = VsScreenQuadSource;
    if (QOpenGLContext::currentContext()->isOpenGLES())
    {
        versionedVsSrc.prepend(QByteArrayLiteral("#version 300 es\n"));
        versionedFsSrc.prepend(QByteArrayLiteral("#version 300 es\n"));
    }
    else
    {
        versionedVsSrc.prepend(QByteArrayLiteral("#version 330\n"));
        versionedFsSrc.prepend(QByteArrayLiteral("#version 330\n"));
    }

    _spScreenQuad.addShaderFromSourceCode(QOpenGLShader::Vertex, VsScreenQuadSource);
    _spScreenQuad.addShaderFromSourceCode(QOpenGLShader::Fragment, FsScreenQuadSource);
    _spScreenQuad.bindAttributeLocation("vertex", 0);
    _spScreenQuad.link();

    _spScreenQuad.bind();
    _screenQuadVao.create();
    _screenQuadVao.bind();

    const int numQuadVertices = 8;
    GLfloat quadVertices[numQuadVertices] =
    {
        -1.0f, -1.0f,
        +1.0f, -1.0f,
        -1.0f, +1.0f,
        +1.0f, +1.0f
    };

    // Setup vertex buffer object.
    _quadVbo.create();
    _quadVbo.bind();
    _quadVbo.allocate( quadVertices, numQuadVertices * sizeof( GLfloat ) );
    _quadVbo.release();
    // Store the vertex attribute bindings for the program.
    setupVertexAttribs();
    _viewMX.setToIdentity();
    _viewMX.translate( 0.0f, 0.0f, -1.0f );
    // set quad model matrix
    _modelMX.setToIdentity();
    _modelMX.rotate( 180.0f, 1, 0, 0 );
    _spScreenQuad.release();
    _screenQuadVao.release();

    initVolumeRenderer();
	// FIXME: dual gpu setup
	//initVolumeRenderer(false, false); 
}

/**
 * @brief VolumeRenderWidget::initVolumeRenderer
 */
void VolumeRenderWidget::initVolumeRenderer(bool useGL, const bool useCPU)
{
    try
    {
        // try to use GPU with GL context sharing
        _volumerender.initialize(useGL, useCPU);
    }
    catch (std::invalid_argument e)
    {
        qCritical() << e.what();
    }
    catch (std::runtime_error e)
    {
        _useGL = false;
		try
		{
            qCritical() << e.what() << "\nDisabling OpenGL context sharing.";
			_volumerender.initialize(_useGL, false);
		}
		catch (std::runtime_error e)
		{
			qCritical() << e.what() << "\nSwitching to CPU rendering mode.";
			_volumerender.initialize(_useGL, true);
		}
    }
    catch (...)
    {
        qCritical() << "An unknown error occured initializing OpenCL/OpenGL.";
    }
}

/**
 * @brief VolumeRenderWidget::saveFrame
 */
void VolumeRenderWidget::saveFrame()
{
    _writeImage = true;
    update();
}

/**
 * @brief VolumeRenderWidget::toggleVideoRecording
 */
void VolumeRenderWidget::toggleVideoRecording()
{
    qInfo() << (_recordVideo ? "Stopped recording." : "Started recording.");

    _recordVideo = !_recordVideo;
    _writeImage = true;
    update();
}

/**
 * @brief VolumeRenderWidget::toggleViewRecording
 * Toggle writing camera configuration (rotation as quaternion and translation as a vecor)
 * into two files every time camera parameters change.
 */
void VolumeRenderWidget::toggleViewRecording()
{
    qInfo() << (_logView ? "Stopped view config recording." : "Started view config recording.");
    _logView = !_logView;

    if (_logView)
    {
        QFileDialog dialog;
        _viewLogFile= dialog.getSaveFileName(this, tr("Save camera path"),
                                                QDir::currentPath(), tr("All files"));
        if (_viewLogFile.isEmpty())
            return;
    }
    updateView();
}

/**
 * @brief VolumeRenderWidget::toggleInteractionLogging
 * Log all interactions (camera, tff, timestep) to file.
 */
void VolumeRenderWidget::toggleInteractionLogging()
{
	qInfo() << (_logInteraction ? "Stopped view config recording." : "Started view config recording.");
	_logInteraction = !_logInteraction;

	if (_logInteraction)
	{
		QFileDialog dialog;
		_interactionLogFile = dialog.getSaveFileName(this, tr("Save camera path"),
			QDir::currentPath(), tr("All files"));
		if (_interactionLogFile.isEmpty())
			return;
		_timer.restart();

		// log initial configuration
		QString s;
		s += QString::number(_timer.elapsed());
		s += "; tffInterpolation; ";
		s += _tffInterpol == QEasingCurve::InOutQuad ? "quad" : "linear";
		s += "\n";
		// tff
		s += QString::number(_timer.elapsed());
		s += "; transferFunction; ";
		const std::vector<unsigned char> tff = getRawTransferFunction(_tffStops);
		foreach(unsigned char c, tff)
		{
			s += QString::number(static_cast<int>(c)) + " ";
		}
		s += "\n";
		// camera
		s += QString::number(_timer.elapsed());
		s += "; camera; ";
        s += QString::number(double(_rotQuat.toVector4D().w())) + " ";
        s += QString::number(double(_rotQuat.x())) + " "
                                    + QString::number(double(_rotQuat.y())) + " ";
        s += QString::number(double(_rotQuat.z())) + ", ";
        s += QString::number(double(_translation.x())) + " "
                                    + QString::number(double(_translation.y())) + " ";
        s += QString::number(double(_translation.z())) + "\n";
		// timestep
		s += QString::number(_timer.elapsed());
		s += "; timestep; ";
		s += QString::number(_timestep) + "\n";
		logInteraction(s);
	}
}

/**
 * @brief VolumeRenderWidget::setSequenceStep
 * @param line
 */
void VolumeRenderWidget::setSequenceStep(QString line)
{
    std::cout << line.toStdString() << std::endl;

    if (line.contains("camera"))
    {
        int pos = line.lastIndexOf(';');
        line.remove(',');
        QStringList values = line.remove(0, pos + 2).split(' ');
        setCamRotation(QQuaternion(values.at(0).toFloat(), values.at(1).toFloat(),
                                   values.at(2).toFloat(), values.at(3).toFloat()));
        setCamTranslation(QVector3D(values.at(4).toFloat(), values.at(5).toFloat(),
                                    values.at(6).toFloat()));
        updateViewMatrix();
    }
    else if (line.contains("timestep"))
    {
        int pos = line.lastIndexOf(';');
        _timestep = line.remove(0, pos + 2).toInt();
        _volumerender.setTimestep(size_t(_timestep));
    }
    else if (line.contains("transferFunction"))
    {
        int pos = line.lastIndexOf(';');
        QStringList lst = line.remove(0, pos + 2).split(' ');
        if (lst.back() == "")
            lst.pop_back();
        std::vector<unsigned char> values;
        for (auto &&a : lst)
            values.push_back(static_cast<unsigned char>(a.toInt()));
        setRawTransferFunction(values);
    }
    else if (line.contains("tffInterpolation"))
    {
        int pos = line.lastIndexOf(';');
        QString interpolation = line.remove(0, pos + 2);
        if (interpolation == "linear")
            setTffInterpolation(QEasingCurve::Linear);
        else if (interpolation == "quad")
            setTffInterpolation(QEasingCurve::InOutQuad);
        else if (interpolation == "cubic")
            setTffInterpolation(QEasingCurve::InOutCubic);
    }
}

/**
 * @brief VolumeRenderWidget::playInteractionSequence
 * @param fileName
 */
void VolumeRenderWidget::playInteractionSequence(const QString &fileName, bool recording)
{
    QFile f(fileName);
    if (!f.isOpen() && !f.open(QFile::ReadOnly | QFile::Text))
        throw std::invalid_argument("Invalid file name for interaction log: " + fileName.toStdString());

    _interaction.restart();
    QTextStream sequence(&f);
    QString line;
    while (sequence.readLineInto(&line))
        _interaction.sequence.append(line);

    if (recording)
        toggleVideoRecording();
    update();
}

/**
 * @brief VolumeRenderWidget::setTimeStep
 * @param timestep
 */
void VolumeRenderWidget::setTimeStep(int timestep)
{
    _timestep = timestep;
    _volumerender.setTimestep(size_t(timestep));
    update();
	if (_logInteraction)
	{
		QString s;
		s += QString::number(_timer.elapsed());
		s += "; timestep; ";
		s += QString::number(_timestep) + "\n";

		logInteraction(s);
	}
}

/**
 * @brief VolumeRenderWidget::setImageSamplingRate
 * @param samplingRate
 */
void VolumeRenderWidget::setImageSamplingRate(const double samplingRate)
{
    _imgSamplingRate = samplingRate;
    this->resizeGL(this->width(), this->height());
}

/**
 * @brief VolumeRenderWidget::drawScreenQuad
 */
void VolumeRenderWidget::drawScreenQuad()
{
    // clear to white to avoid getting colored borders outside the quad
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // draw screen quad
    _screenQuadVao.bind();
    _quadVbo.bind();
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    // render screen quad
    _spScreenQuad.bind();
    _spScreenQuad.setUniformValue( _spScreenQuad.uniformLocation("projMatrix"),
                                   _screenQuadProjMX );
    _spScreenQuad.setUniformValue( _spScreenQuad.uniformLocation("mvMatrix"),
                                   _viewMX * _modelMX );
    _spScreenQuad.setUniformValue( _spScreenQuad.uniformLocation("width"), width());
    _spScreenQuad.setUniformValue( _spScreenQuad.uniformLocation("height"), height());

    _spScreenQuad.setUniformValue(_spScreenQuad.uniformLocation("outTex"), GL_TEXTURE0);
    glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
    _screenQuadVao.release();
    _quadVbo.release();
    _spScreenQuad.release();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

/**
 * @brief VolumeRenderWidget::paintGL
 */
void VolumeRenderWidget::paintGL()
{
    if (this->_loadingFinished && _volumerender.hasData() && !_noUpdate)
    {
        try
        {
            if (_useGL)
            {
                _volumerender.runRaycast(size_t(floor(width() * _imgSamplingRate)),
                                         size_t(floor(height()* _imgSamplingRate)));
            }
            else
            {
                std::vector<float> d;
                _volumerender.runRaycastNoGL(size_t(floor(width() * _imgSamplingRate)),
                                             size_t(floor(height()* _imgSamplingRate)), d);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
                             int(floor(this->size().width() * _imgSamplingRate)),
                             int(floor(this->size().height()* _imgSamplingRate)),
                             0, GL_RGBA, GL_FLOAT,
                             d.data());
                _volumerender.updateOutputImg(size_t(floor(width() * _imgSamplingRate)),
                                              size_t(floor(height()* _imgSamplingRate)), _outTexId);
            }
        }
        catch (std::runtime_error e)
        {
            qCritical() << e.what();
        }
    }
    double fps = getFps();

    QPainter p(this);
    p.beginNativePainting();
    {
        drawScreenQuad();

        if (_volumerender.hasData() && _writeImage)
        {
            QImage img = this->grabFramebuffer();
            QString number = QString("%1").arg(_imgCount++, 6, 10, QChar('0'));
            if (!_recordVideo)
            {
                QLoggingCategory category("screenshot");
                qCInfo(category, "Writing current frame img/frame_%s.png",
                       number.toStdString().c_str());
                _writeImage = false;
            }
            if (!QDir("img").exists())
                QDir().mkdir("img");
            img.save("img/frame_" + number + "_"
                     + QString::number(_volumerender.getLastExecTime()) + ".png");
        }
    }
    p.endNativePainting();

    // render overlays
    if (_showOverlay)
    {
        paintFps(p, fps, _volumerender.getLastExecTime());
        paintOrientationAxis(p);
    }

    // recover opengl texture
    p.beginNativePainting();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _outTexId);
    }
    p.endNativePainting();
    p.end();

    if (_contRendering)
        update();

    if (_interaction.play)
    {
        setSequenceStep(_interaction.sequence.at(_interaction.pos));
        _interaction.pos++;
        if (_interaction.pos >= _interaction.sequence.size())
        {
            if (_recordVideo)
                toggleVideoRecording();
            _interaction.play = false;
        }
        update();
    }
}


/**
 * @brief VolumeRenderWidget::resizeGL
 * @param w
 * @param h
 */
void VolumeRenderWidget::resizeGL(const int w, const int h)
{
    _screenQuadProjMX.setToIdentity();
    _screenQuadProjMX.perspective(53.14f, 1.0f, Z_NEAR, Z_FAR);

    _overlayProjMX.setToIdentity();
    _overlayProjMX.perspective(53.14f, float(w)/float(h ? h : 1), Z_NEAR, Z_FAR);

    try
    {
        generateOutputTextures(int(floor(w*_imgSamplingRate)), int(floor(h*_imgSamplingRate)));
    }
    catch (std::runtime_error e)
    {
        qCritical() << "An error occured while generating output texture." << e.what();
    }

    _spScreenQuad.setUniformValue(_spScreenQuad.uniformLocation("width"), width());
    _spScreenQuad.setUniformValue(_spScreenQuad.uniformLocation("height"), height());
    emit frameSizeChanged(this->size());
}


/**
 * @brief VolumeRenderWidget::generateOutputTextures
 */
void VolumeRenderWidget::generateOutputTextures(const int width, const int height)
{
	glDeleteTextures(1, &_outTexId);
	glGenTextures(1, &_outTexId);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _outTexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!_volumerender.hasData())
    {
        QImage img(width, height, QImage::Format_RGBA8888);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setFont(QFont("Helvetia", 12));
        p.drawText(width/2 - 110, height/2, "Drop your volume data file here.");
        p.end();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     img.bits());
    }
    else
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
    }
    glGenerateMipmap(GL_TEXTURE_2D);

    _volumerender.updateOutputImg(static_cast<size_t>(width), static_cast<size_t>(height),
                                      _outTexId);

    updateView(0, 0);
}

void VolumeRenderWidget::setShowOverlay(bool showOverlay)
{
    _showOverlay = showOverlay;
    updateView();
}

QQuaternion VolumeRenderWidget::getCamRotation() const
{
    return _rotQuat;
}

void VolumeRenderWidget::setCamRotation(const QQuaternion &rotQuat)
{
    _rotQuat = rotQuat;
}

QVector3D VolumeRenderWidget::getCamTranslation() const
{
    return _translation;
}

void VolumeRenderWidget::setCamTranslation(const QVector3D &translation)
{
    _translation = translation;
}

/**
 * @brief VolumeRenderWidget::showSelectOpenCL
 */
void VolumeRenderWidget::showSelectOpenCL()
{
    std::vector<std::string> names;
    try
    {
        names = _volumerender.getPlatformNames();
    }
    catch (std::runtime_error e)
    {
        qCritical() << "An error occured while trying to retrieve OpenCL platform information."
                    << e.what();
        return;
    }

    QStringList platforms;
    for (std::string &s : names)
        platforms.append(QString::fromStdString(s));

    bool ok;
    QString platform = QInputDialog::getItem(this, tr("Select platform"),
                                             tr("Select OpenCL platform:"),
                                             platforms, 0, false, &ok);
    if (ok && !platform.isEmpty())
    {
        cl_vendor vendor = VENDOR_ANY;
        QString type = "GPU";
        _useGL = false;

        // filter GPU only platforms
        if (platform.contains("NVIDIA"))
            vendor = VENDOR_NVIDIA;
        else
        {
            if (!platform.contains("Graphics"))
            {
                QStringList types;
                types << "GPU" << "CPU";
                type = QInputDialog::getItem(this, tr("Select type"),
                                             tr("Select device type:"), types, 0, false, &ok);
            }
            if (platform.contains("Advanced Micro Devices", Qt::CaseInsensitive))
                vendor = VENDOR_AMD;
            else if (platform.contains("Intel", Qt::CaseInsensitive))
                vendor = VENDOR_INTEL;

        }
        if (!type.isEmpty())
        {
            if (type == "GPU")
            {
                QMessageBox msgBox;
                msgBox.setText("Do you wish to try OpenGL context sharing using this platform?");
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::Yes);
                int ret = msgBox.exec();
                _useGL = ret == QMessageBox::Yes;
            }

            size_t platformId = static_cast<size_t>(platforms.indexOf(platform));
            try
            {
                names = _volumerender.getDeviceNames(platformId, type.toStdString());
            }
            catch (std::runtime_error e)
            {
                qCritical() << "No capable device found on using the selected platform and type."
                            << e.what();
                return;
            }
            QStringList devices;
            for (std::string &s : names)
                devices.append(QString::fromStdString(s));

            QString device;
            if (devices.empty())
                throw std::runtime_error("No capable device found on the selected platform.");
            else if (devices.size() == 1)
                device = devices.front();
            else
                device = QInputDialog::getItem(this, tr("Select device"),
                                                   tr("Select OpenCL device:"),
                                                   devices, 0, false, &ok);
            if (!device.isEmpty())
            {
                try {
                    _volumerender.initialize(_useGL, type == "CPU", vendor, device.toStdString(),
                                             static_cast<int>(platformId));
                } catch (std::runtime_error e) {
                    qCritical() << e.what() << "\nSwitching to CPU fallback mode.";
                    _useGL = false;
                    _volumerender.initialize(false, true);
                }
                catch (...) {
                    qCritical() << "An unknown error occured initializing OpenCL/OpenGL.";
                }
                updateTransferFunction(_tffStops);
                this->resizeGL(this->width(), this->height());
            }
        }
    }
}


/**
 * @brief VolumeRenderWidget::setupVertexAttribs
 */
void VolumeRenderWidget::setupVertexAttribs()
{
    // screen quad
    _quadVbo.bind();
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    _quadVbo.release();
}


/**
 * @brief VolumeRenderWidget::setVolumeData
 * @param fileName
 */
void VolumeRenderWidget::setVolumeData(const DatRawReader::Properties volumeFileProps)
{
    this->_noUpdate = true;
    size_t timesteps = 0;
    try
    {
        timesteps = _volumerender.loadVolumeData(volumeFileProps);
    }
    catch (std::invalid_argument e)
    {
        qCritical() << e.what();
    }
    catch (std::runtime_error e)
    {
        qCritical() << e.what();
    }
    emit timeSeriesLoaded(static_cast<int>(timesteps - 1));
    _overlayModelMX.setToIdentity();
    QVector3D res = getVolumeResolution().toVector3D();
    _overlayModelMX.scale(res / qMax(res.x(), qMax(res.y(), res.z())));
    this->_noUpdate = false;
    update();
}


/**
 * @brief VolumeRenderWidget::hasData
 * @return
 */
bool VolumeRenderWidget::hasData() const 
{
    return _volumerender.hasData();
}


/**
 * @brief VolumeRenderWidget::getVolumeResolution
 * @return
 */
const QVector4D VolumeRenderWidget::getVolumeResolution() const
{
    if (_volumerender.hasData() == false)
        return QVector4D();

    return QVector4D(_volumerender.getResolution().at(0),
                     _volumerender.getResolution().at(1),
                     _volumerender.getResolution().at(2),
                     _volumerender.getResolution().at(3));
}


/**
 * @brief VolumeRenderWidget::updateStepSize
 * @param stepSize
 */
void VolumeRenderWidget::updateSamplingRate(double samplingRate)
{
    _volumerender.updateSamplingRate(samplingRate);
    update();
}


/**
 * @brief VolumeRenderWidget::setInterpolation
 * @param method
 */
void VolumeRenderWidget::setTffInterpolation(const QEasingCurve::Type interpolation)
{
    _tffInterpol = interpolation;

//    if (interpolation.contains("Quad"))
//        _tffInterpol = QEasingCurve::InOutQuad;
//    else if (interpolation.contains("Linear"))
//        _tffInterpol = QEasingCurve::Linear;

	if (_logInteraction)
	{
		QString s;
		s += QString::number(_timer.elapsed());
		s += "; tffInterpolation; ";
        switch (QEasingCurve::InOutQuad)
        {
        case QEasingCurve::Linear: s += "linear"; break;
        case QEasingCurve::InOutQuad: s += "quad"; break;
        case QEasingCurve::InOutCubic: s += "cubic"; break;
        default: break;
        }
		s += "\n";

		logInteraction(s);
	}
}

/**
 * @brief VolumeRenderWidget::setRawTransferFunction
 * @param tff
 */
void VolumeRenderWidget::setRawTransferFunction(std::vector<unsigned char> tff)
{
    try
    {
        _volumerender.setTransferFunction(tff);
        update();
    }
    catch (std::runtime_error e)
    {
        qCritical() << e.what();
    }
}

/**
 * @brief VolumeRenderWidget::updateTransferFunction
 * @param stops
 */
void VolumeRenderWidget::updateTransferFunction(QGradientStops stops)
{
    const int tffSize = 1024;
    const qreal granularity = 8192.0;
    std::vector<uchar> tff(tffSize*4, uchar(0));
    std::vector<unsigned int> prefixSum(tffSize);

    QPropertyAnimation interpolator;
    interpolator.setEasingCurve(_tffInterpol);
    interpolator.setDuration(int(granularity));
    foreach (QGradientStop stop, stops)
        interpolator.setKeyValueAt(stop.first, stop.second);

#pragma omp for
    for (int i = 0; i < int(tffSize); ++i)
    {
        interpolator.setCurrentTime(qRound(double(i)/double(tffSize) * granularity));
        tff.at(size_t(i)*4 + 0) = uchar(qMax(0, interpolator.currentValue().value<QColor>().red()  -3));
        tff.at(size_t(i)*4 + 1) = uchar(qMax(0, interpolator.currentValue().value<QColor>().green()-3));
        tff.at(size_t(i)*4 + 2) = uchar(qMax(0, interpolator.currentValue().value<QColor>().blue() -3));
        tff.at(size_t(i)*4 + 3) = uchar(qMax(0, interpolator.currentValue().value<QColor>().alpha()-3));
        prefixSum.at(size_t(i)) = tff.at(size_t(i)*4 + 3);
    }

    try
    {
        _volumerender.setTransferFunction(tff);
        // TODO: replace with std::exclusicve_scan(std::par, ... )
        std::partial_sum(prefixSum.begin(), prefixSum.end(), prefixSum.begin());
        _volumerender.setTffPrefixSum(prefixSum);
    }
    catch (std::runtime_error e)
    {
        qCritical() << e.what();
    }
    update();
    _tffStops = stops;

	if (_logInteraction)
	{
		QString s;
		s += QString::number(_timer.elapsed());
		s += "; transferFunction; ";
		const std::vector<unsigned char> tff = getRawTransferFunction(stops);
		foreach (unsigned char c, tff)
		{
			s += QString::number(static_cast<int>(c)) + " ";
		}
		s += "\n";
	}
}

std::vector<unsigned char> VolumeRenderWidget::getRawTransferFunction(QGradientStops stops) const
{
    const size_t tffSize = 1024;
    const qreal granularity = 8192.0;
    std::vector<uchar> tff(tffSize*4);

    QPropertyAnimation interpolator;
    interpolator.setEasingCurve(_tffInterpol);
    interpolator.setDuration(static_cast<int>(granularity));
    foreach (QGradientStop stop, stops)
    {
        interpolator.setKeyValueAt(stop.first, stop.second);
    }

    for (size_t i = 0; i < tffSize; ++i)
    {
        interpolator.setCurrentTime(qRound(double(i)/double(tffSize) * granularity));
        tff.at(i*4 + 0) = uchar(qMax(0, interpolator.currentValue().value<QColor>().red()   - 3));
        tff.at(i*4 + 1) = uchar(qMax(0, interpolator.currentValue().value<QColor>().green() - 3));
        tff.at(i*4 + 2) = uchar(qMax(0, interpolator.currentValue().value<QColor>().blue()  - 3));
        tff.at(i*4 + 3) = uchar(qMax(0, interpolator.currentValue().value<QColor>().alpha() - 3));
    }
    return tff;
}


/**
 * @brief VolumeRenderWidget::cleanup
 */
void VolumeRenderWidget::cleanup()
{
//    makeCurrent();
//    if (_quadVbo.isCreated())
//        _quadVbo.destroy();
}



/**
 * @brief VolumeRenderWidget::mousePressEvent
 * @param event
 */
void VolumeRenderWidget::mousePressEvent(QMouseEvent *event)
{
    // nothing yet
    _lastLocalCursorPos = event->pos();
}


/**
 * @brief VolumeRenderWidget::mouseReleaseEvent
 * @param event
 */
void VolumeRenderWidget::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
}


/**
 * @brief VolumeRenderWidget::recordView
 */
void VolumeRenderWidget::recordViewConfig() const
{
    QFile saveQuat(_viewLogFile + "_quat.txt");
    QFile saveTrans(_viewLogFile + "_trans.txt");
    if (!saveQuat.open(QFile::WriteOnly | QFile::Append)
            || !saveTrans.open(QFile::WriteOnly | QFile::Append))
    {
        qWarning() << "Couldn't open file for saving the camera configurations: "
                   << _viewLogFile;
        return;
    }

    QTextStream quatStream(&saveQuat);
    quatStream << _rotQuat.toVector4D().w() << " " << _rotQuat.x() << " " << _rotQuat.y() << " "
               << _rotQuat.z() << "; ";
    QTextStream transStream(&saveTrans);
    transStream << _translation.x() << " " << _translation.y() << " " << _translation.z() << "; ";
}


/**
 *
 */
void VolumeRenderWidget::logInteraction(const QString &str) const
{
	QFile f(_interactionLogFile);
	if (!f.open(QFile::WriteOnly | QFile::Append))
	{
		qWarning() << "Couldn't open file for saving the camera configurations: "
				   << _interactionLogFile;
		return;
	}
	QTextStream fileStream(&f);
	fileStream << str;
}

/**
 * @brief VolumeRenderWidget::resetCam
 */
void VolumeRenderWidget::resetCam()
{
    _rotQuat = QQuaternion(1, 0, 0, 0);
    _translation = QVector3D(0, 0, 2.0);
    updateView();
}

/**
 * @brief VolumeRenderWidget::updateViewMatrix
 */
void VolumeRenderWidget::updateViewMatrix()
{
    QMatrix4x4 viewMat;
//    viewMat.scale(QVector3D(1,1,1./_zScale));
    viewMat.rotate(_rotQuat);

    _coordViewMX.setToIdentity();
    _coordViewMX.scale(1, -1, 1);
    _coordViewMX.translate(_translation * -1.0);
    _coordViewMX *= QMatrix4x4(_rotQuat.toRotationMatrix().transposed());

    viewMat.translate(_translation);
    viewMat.scale(_translation.z());

    std::array<float, 16> viewArray;
    for (size_t i = 0; i < viewArray.size(); ++i)
        viewArray.at(i) = viewMat.transposed().constData()[i];
    try
    {
        _volumerender.updateView(viewArray);
    }
    catch (std::runtime_error e)
    {
        qCritical() << e.what();
    }
}

void VolumeRenderWidget::logView()
{
    QString s;
    s += QString::number(_timer.elapsed());
    s += "; camera; ";
    s += QString::number(double(_rotQuat.toVector4D().w())) + " ";
    s += QString::number(double(_rotQuat.x())) + " "
                                + QString::number(double(_rotQuat.y())) + " ";
    s += QString::number(double(_rotQuat.z())) + ", ";

    s += QString::number(double(_translation.x())) + " "
                                + QString::number(double(_translation.y())) + " ";
    s += QString::number(double(_translation.z())) + "\n";

    logInteraction(s);
}

/**
 * @brief update camera view
 * @param dx
 * @param dy
 */
void VolumeRenderWidget::updateView(const float dx, const float dy)
{
    QVector3D rotAxis = QVector3D(dy, dx, 0.0f).normalized();
    float angle = QVector2D(dx, dy).length()*500.f;
    _rotQuat = _rotQuat * QQuaternion::fromAxisAndAngle(rotAxis, -angle);
    updateViewMatrix();
    update();

    if (_logView)
        recordViewConfig();
	if (_logInteraction)
        logView();
}

/**
 * @brief VolumeRenderWidget::updateZoom
 * @param zoom
 */
void VolumeRenderWidget::updateTranslation(const QVector3D translation)
{
    _translation = (_translation - translation);
    // limit translation to origin, otherwise camera setup breaks (flips)
    _translation.setZ(qMax(0.01f, _translation.z()));
    updateView();
}

/**
 * @brief VolumeRenderWidget::mouseMoveEvent
 * @param event
 */
void VolumeRenderWidget::mouseMoveEvent(QMouseEvent *event)
{
    float dx = float(event->pos().x() - _lastLocalCursorPos.x()) / width();
    float dy = float(event->pos().y() - _lastLocalCursorPos.y()) / height();

    // rotate object
    if (event->buttons() & Qt::LeftButton)
    {
        if (event->modifiers() & Qt::ShiftModifier)
        {
            dx *= 0.1f;
            dy *= 0.1f;
        }
        updateView(dx, dy);
    }
    // translate object
    if (event->buttons() & Qt::MiddleButton)
    {
        int sensitivity = 6;
        if (event->modifiers() & Qt::ShiftModifier)
            sensitivity = 1;

        _translation.setX(_translation.x() - dx*sensitivity);
        _translation.setY(_translation.y() + dy*sensitivity);
        updateView();
    }

    _lastLocalCursorPos = event->pos();
    event->accept();
}


/**
 * @brief VolumeRenderWidget::wheelEvent
 * @param event
 */
void VolumeRenderWidget::wheelEvent(QWheelEvent *event)
{
    float t = 1600.0;
    if (event->modifiers() & Qt::ShiftModifier)
        t *= 6.f;
    // limit translation to origin, otherwise camera setup breaks (flips)
    _translation.setZ(qMax(0.01f, _translation.z() - event->angleDelta().y() / t));
    updateView();
    event->accept();
}


/**
 * @brief VolumeRenderWidget::mouseDoubleClickEvent
 * @param event
 */
void VolumeRenderWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // nothing yet
    event->accept();
}



/**
 * @brief VolumeRenderWidget::keyReleaseEvent
 * @param event
 */
void VolumeRenderWidget::keyReleaseEvent(QKeyEvent *event)
{
    // nothing yet
    event->accept();
}


/**
 * @brief VolumeRenderWidget::getLoadingFinished
 * @return
 */
bool VolumeRenderWidget::getLoadingFinished() const
{
    return _loadingFinished;
}


/**
 * @brief VolumeRenderWidget::setLoadingFinished
 * @param loadingFinished
 */
void VolumeRenderWidget::setLoadingFinished(const bool loadingFinished)
{
    this->setTimeStep(0);
    _loadingFinished = loadingFinished;
}


/**
 * @brief VolumeRenderWidget::setCamOrtho
 * @param camOrtho
 */
void VolumeRenderWidget::setCamOrtho(const bool camOrtho)
{
    _volumerender.setCamOrtho(camOrtho);
    _overlayProjMX.setToIdentity();
    if (camOrtho)
        _overlayProjMX.ortho(QRect(0, 0, width(), height()));
    else
        _overlayProjMX.perspective(53.14f, float(width())/float(height() ? height() : 1), Z_NEAR, Z_FAR);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setContRendering
 * @param contRendering
 */
void VolumeRenderWidget::setContRendering(const bool contRendering)
{
    _contRendering = contRendering;
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setIllumination
 * @param illum
 */
void VolumeRenderWidget::setIllumination(const int illum)
{
    _volumerender.setIllumination(static_cast<unsigned int>(illum));
    this->updateView();
}


/**
 * @brief VolumeRenderWidget::setAmbientOcclusion
 * @param ao
 */
void VolumeRenderWidget::setAmbientOcclusion(const bool ao)
{
    _volumerender.setAmbientOcclusion(ao);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setLinearInterpolation
 * @param linear
 */
void VolumeRenderWidget::setLinearInterpolation(const bool linear)
{
    _volumerender.setLinearInterpolation(linear);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setContours
 * @param contours
 */
void VolumeRenderWidget::setContours(const bool contours)
{
    _volumerender.setContours(contours);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setAerial
 * @param aerial
 */
void VolumeRenderWidget::setAerial(const bool aerial)
{
    _volumerender.setAerial(aerial);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setUseGradient
 * @param useGradient
 */
void VolumeRenderWidget::setUseGradient(const bool useGradient)
{
    _volumerender.setUseGradient(useGradient);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setImgEss
 * @param useEss
 */
void VolumeRenderWidget::setImgEss(const bool useEss)
{
    if (useEss)
        _volumerender.updateOutputImg(static_cast<size_t>(width() * _imgSamplingRate),
                                      static_cast<size_t>(height() * _imgSamplingRate), _outTexId);
    _volumerender.setImgEss(useEss);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setImgEss
 * @param useEss
 */
void VolumeRenderWidget::setObjEss(const bool useEss)
{
    _volumerender.setObjEss(useEss);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setDrawBox
 * @param box
 */
void VolumeRenderWidget::setShowEss(const bool showEss)
{
    _volumerender.setShowESS(showEss);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBrickSizeLow
 */
void VolumeRenderWidget::setBrickSizeLarge()
{
    _volumerender.generateBricks(16.f);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBrickSizeMedium
 */
void VolumeRenderWidget::setBrickSizeMedium()
{
    _volumerender.generateBricks(12.f);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBrickSizeMedium
 */
void VolumeRenderWidget::setBrickSizeSmall()
{
    _volumerender.generateBricks(8.f);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBrickSizeHigh
 */
void VolumeRenderWidget::setBrickSizeTiny()
{
    _volumerender.generateBricks(4.f);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBackgroundColor
 * @param col
 */
void VolumeRenderWidget::setBackgroundColor(const QColor col)
{
    setEnvironmentMap("");
    std::array<float, 4> color = {{ static_cast<float>(col.redF()),
                                    static_cast<float>(col.greenF()),
                                    static_cast<float>(col.blueF()),
                                    static_cast<float>(col.alphaF()) }};
    _volumerender.setBackground(color);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setBBox
 * @param botLeft
 * @param botLeft
 */
void VolumeRenderWidget::setBBox(QVector3D botLeft, QVector3D topRight)
{
    botLeft = botLeft / QVector3D(_volumerender.getResolution().at(0),
                                  _volumerender.getResolution().at(1),
                                  _volumerender.getResolution().at(2));
    botLeft = botLeft * 2.f - QVector3D(1.f, 1.f, 1.f);
    topRight = topRight / QVector3D(_volumerender.getResolution().at(0),
                                    _volumerender.getResolution().at(1),
                                    _volumerender.getResolution().at(2));
    topRight = topRight * 2.f - QVector3D(1.f, 1.f, 1.f);
//    qDebug() << botLeft << topRight;
    _volumerender.setBBox(botLeft.x(), botLeft.y(), botLeft.z(),
                          topRight.x(), topRight.y(), topRight.z());
    updateView();
}

/**
 * @brief VolumeRenderWidget::updateHistogram
 */
const std::array<double, 256> & VolumeRenderWidget::getHistogram(unsigned int timestep)
{
    return _volumerender.getHistogram(timestep);
}


/**
 * @brief VolumeRenderWidget::calcFPS
 * @return
 */
double VolumeRenderWidget::getFps()
{
    _times.push_back(_volumerender.getLastExecTime());
    if (_times.length() > 60)
        _times.pop_front();

    double sum = 0.0;
#pragma omp parallel for reduction(+:sum)
    for (int i = 0; i < _times.size(); ++i)
        sum += _times.at(i);

    double fps = 1.0/(sum/_times.length());
    return fps;
}


/**
 * @brief VolumeRenderWidget::generateLowResVolume
 * @param factor
 */
void VolumeRenderWidget::generateLowResVolume()
{
    bool ok;
    int factor = QInputDialog::getInt(this, tr("Factor"),
                                         tr("Select downsampling factor:"), 2, 2, 64, 1, &ok);
    if (ok)
    {
        try
        {
            std::string name = _volumerender.volumeDownsampling(static_cast<size_t>(_timestep),
                                                                factor);
            QLoggingCategory category("volumeDownSampling");
            qCInfo(category, "Successfully created down-sampled volume data set: '%s.raw'",
                   name.c_str());
        }
        catch (std::invalid_argument e)
        {
            qCritical() << e.what();
        }
        catch (std::runtime_error e)
        {
            qCritical() << e.what();
        }
    }
}

/**
 * @brief VolumeRenderWidget::read
 * @param json
 */
void VolumeRenderWidget::read(const QJsonObject &json)
{
    if (json.contains("camRotation"))
    {
        QStringList sl = json["camRotation"].toVariant().toString().split(' ');
        if (sl.length() >= 4)
        {
            _rotQuat.setScalar(sl.at(0).toFloat());
            _rotQuat.setX(sl.at(1).toFloat());
            _rotQuat.setY(sl.at(2).toFloat());
            _rotQuat.setZ(sl.at(3).toFloat());
        }
    }
    if (json.contains("camTranslation"))
    {
        QStringList sl = json["camTranslation"].toVariant().toString().split(' ');
        if (sl.length() >= 3)
        {
            _translation.setX(sl.at(0).toFloat());
            _translation.setY(sl.at(1).toFloat());
            _translation.setZ(sl.at(2).toFloat());
        }
    }
    updateView();
}

/**
 * @brief Create a QString from a float.
 * @param f float
 * @return QString containing the float.
 */
static QString qStringFloat(float f)
{
    return QString::number(double(f));
}

/**
 * @brief VolumeRenderWidget::write
 * @param json
 */
void VolumeRenderWidget::write(QJsonObject &json) const
{
    QString sTmp = qStringFloat(_rotQuat.scalar()) + " " + qStringFloat(_rotQuat.x())
                   + " " + qStringFloat(_rotQuat.y()) + " " + qStringFloat(_rotQuat.z());
    json["camRotation"] = sTmp;
    sTmp = qStringFloat(_translation.x()) + " " + qStringFloat(_translation.y())
            + " " + qStringFloat(_translation.z());
    json["camTranslation"] = sTmp;
}

/**
 * @brief VolumeRenderWidget::reloadKernels
 */
void VolumeRenderWidget::reloadKernels()
{
    // NOTE: this reload resets all previously defined rendering settings to default values
    initVolumeRenderer();
    resizeGL(width(), height());
}

/**
 * @brief VolumeRenderWidget::setEnvirontmentMap
 * @param fileName
 */
void VolumeRenderWidget::setEnvironmentMap(QString fileName)
{
    try {
        _volumerender.createEnvironmentMap(fileName.toStdString());
    } catch (std::runtime_error e) {
        qWarning() << e.what();
    }
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setRaycast
 */
void VolumeRenderWidget::enableRaycast()
{
    _volumerender.setTechnique(VolumeRenderCL::TECH_RAYCAST);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::enablePathtrace
 */
void VolumeRenderWidget::enablePathtrace()
{
    _volumerender.setTechnique(VolumeRenderCL::TECH_PATHTRACE);
    this->updateView();
}

/**
 * @brief VolumeRenderWidget::setExtinction
 */
void VolumeRenderWidget::setExtinction(const double extinction)
{
    _volumerender.setExtinction(extinction);
    this->updateView();
}
