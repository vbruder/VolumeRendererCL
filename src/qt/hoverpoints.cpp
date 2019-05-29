/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the demonstration applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

//#ifdef QT_OPENGL_SUPPORT
//#include <QGLWidget>
//#endif

#include "src/qt/hoverpoints.h"

#include <algorithm>

#define printf

HoverPoints::HoverPoints(QWidget *widget, PointShape shape)
    : QObject(widget)
{
    m_widget = widget;
    widget->installEventFilter(this);
    widget->setAttribute(Qt::WA_AcceptTouchEvents);

    m_connectionType = LineConnection;
    m_sortType = XSort;
    m_shape = shape;
    m_pointPen = QPen(QColor(255, 255, 255, 191), 1);
    m_connectionPen = QPen(QColor(255, 255, 255, 127), 2);
    m_pointBrush = QBrush(QColor(191, 191, 191, 127));
    m_pointSize = QSize(15, 15);
    m_currentIndex = 0;
    m_editable = true;
    m_enabled = true;

    connect(this, SIGNAL(pointsChanged(QPolygonF)), m_widget, SLOT(update()));
}


void HoverPoints::setEnabled(bool enabled)
{
    if (m_enabled != enabled)
    {
        m_enabled = enabled;
        m_widget->update();
    }
}


void HoverPoints::setColorSelected(const QColor color)
{
    Q_ASSERT(m_currentIndex < m_colors.size());
    if (m_currentIndex >= 0)
        m_colors.replace(m_currentIndex, color);
//    firePointChange();
//    emit selectionChanged(color);//m_colors.at(m_currentIndex));
}

void HoverPoints::setHistogram(const QVector<qreal> &histo)
{
    m_histogram = histo;
    firePointChange();
}


bool HoverPoints::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_widget && m_enabled)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonDblClick:
        {
            if (!m_fingerPointMapping.isEmpty())
                return true;
            QMouseEvent *me = static_cast<QMouseEvent *>(event);

            QPointF clickPos = me->pos();
            int index = -1;
            for (int i = 0; i < m_points.size(); ++i)
            {
                QPainterPath path;
                if (m_shape == CircleShape)
                    path.addEllipse(pointBoundingRect(i));
                else
                    path.addRect(pointBoundingRect(i));

                if (path.contains(clickPos)) {
                    index = i;
                    break;
                }
            }
            if (me->button() == Qt::LeftButton && index >= 0)
            {
                QColorDialog cd;
                QColor c = cd.getColor(m_colors.at(index), Q_NULLPTR, "Choose color of added point",
                                       QColorDialog::ShowAlphaChannel);
                if (c.isValid())
                {
                    m_colors.replace(index, c);
                    firePointChange();
                }
            }
            break;
        }
        case QEvent::MouseButtonPress:
        {
            if (!m_fingerPointMapping.isEmpty())
                return true;
            QMouseEvent *me = static_cast<QMouseEvent *>(event);

            QPointF clickPos = me->pos();
            int index = -1;
            for (int i=0; i<m_points.size(); ++i)
            {
                QPainterPath path;
                if (m_shape == CircleShape)
                    path.addEllipse(pointBoundingRect(i));
                else
                    path.addRect(pointBoundingRect(i));

                if (path.contains(clickPos))
                {
                    index = i;
                    break;
                }
            }
            if (me->button() == Qt::LeftButton)
            {
                if (index == -1) // insert new point
                {
                    if (!m_editable)
                        return false;
                    int pos = 0;
                    // Insert sort for x or y
                    if (m_sortType == XSort)
                    {
                        for (int i = 0; i < m_points.size(); ++i)
                        {
                            if (m_points.at(i).x() > clickPos.x())
                            {
                                pos = i;
                                break;
                            }
                        }
                    }
                    else if (m_sortType == YSort)
                    {
                        for (int i = 0; i < m_points.size(); ++i)
                        {
                            if (m_points.at(i).y() > clickPos.y())
                            {
                                pos = i;
                                break;
                            }
                        }
                    }

                    m_points.insert(pos, clickPos);
                    m_locks.insert(pos, 0);
                    m_currentIndex = pos;
                    if (m_currentIndex >= 0)
                    {
                        m_colors.insert(pos, m_colors.at(m_currentIndex));
                    }
                }
                else    // select point
                {
                    m_currentIndex = index;
                }
                firePointChange();
                return true;
            }
            else if (me->button() == Qt::RightButton)
            {
                if (index >= 0 && m_editable)
                {
                    if (m_locks[index] == 0)
                    {
                        m_locks.remove(index);
                        m_points.remove(index);
                        m_colors.remove(index);
                        firePointChange();
                    }
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (!m_fingerPointMapping.isEmpty())
                return true;
            break;
        case QEvent::MouseMove:
            if (!m_fingerPointMapping.isEmpty())
                return true;
            if (m_currentIndex >= 0)
                movePoint(m_currentIndex, (static_cast<QMouseEvent *>(event))->pos());
            break;
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        {
            const QTouchEvent *const touchEvent = static_cast<const QTouchEvent*>(event);
            const QList<QTouchEvent::TouchPoint> points = touchEvent->touchPoints();
            const qreal pointSize = qMax(m_pointSize.width(), m_pointSize.height());
            foreach (const QTouchEvent::TouchPoint &touchPoint, points)
            {
                const int id = touchPoint.id();
                switch (touchPoint.state())
                {
                case Qt::TouchPointPressed:
                {
                    // find the point, move it
                    QSet<int> activePoints = QSet<int>::fromList(m_fingerPointMapping.values());
                    int activePoint = -1;
                    qreal distance = -1;
                    const int pointsCount = m_points.size();
                    const int activePointCount = activePoints.size();
                    if (pointsCount == 2 && activePointCount == 1) { // only two points
                        activePoint = activePoints.contains(0) ? 1 : 0;
                    } else {
                        for (int i=0; i<pointsCount; ++i) {
                            if (activePoints.contains(i))
                                continue;

                            qreal d = QLineF(touchPoint.pos(), m_points.at(i)).length();
                            if ((distance < 0 && d < 12 * pointSize) || d < distance) {
                                distance = d;
                                activePoint = i;
                            }

                        }
                    }
                    if (activePoint != -1) {
                        m_fingerPointMapping.insert(touchPoint.id(), activePoint);
                        movePoint(activePoint, touchPoint.pos());
                    }
                }
                    break;
                case Qt::TouchPointReleased:
                {
                    // move the point and release
                    QHash<int,int>::iterator it = m_fingerPointMapping.find(id);
                    movePoint(it.value(), touchPoint.pos());
                    m_fingerPointMapping.erase(it);
                }
                    break;
                case Qt::TouchPointMoved:
                {
                    // move the point
                    const int pointIdx = m_fingerPointMapping.value(id, -1);
                    if (pointIdx >= 0) // do we track this point?
                        movePoint(pointIdx, touchPoint.pos());
                }
                    break;
                default:
                    break;
                }
            }

            if (m_fingerPointMapping.isEmpty())
            {
                event->ignore();
                return false;
            }
            else
            {
                return true;
            }
            break;
        }
        case QEvent::TouchEnd:
        {
            if (m_fingerPointMapping.isEmpty())
            {
                event->ignore();
                return false;
            }
            return true;
            break;
        }
        case QEvent::Resize:
        {
            QResizeEvent *e = static_cast<QResizeEvent *>(event);
            if (e->oldSize().width() == 0 || e->oldSize().height() == 0)
                break;
            qreal stretch_x = e->size().width() / qreal(e->oldSize().width());
            qreal stretch_y = e->size().height() / qreal(e->oldSize().height());
            for (int i=0; i<m_points.size(); ++i)
            {
                QPointF p = m_points[i];
                movePoint(i, QPointF(p.x() * stretch_x, p.y() * stretch_y), false);
            }

            firePointChange();
            break;
        }
        case QEvent::Wheel:
        {
            // TODO: implement proper zoom
            QWheelEvent *e = static_cast<QWheelEvent *>(event);
            QPoint delta =  e->angleDelta();
            qreal factor = 1.0;
            if (delta.y() < 0)
                factor = 0.8;
            else if (delta.y() > 0)
                factor = 1.25;

            qreal stretch_x = factor;
            qreal stretch_y = 1.0;
            for (int i = 0; i < m_points.size(); ++i)
            {
                QPointF p = m_points[i];
                movePoint(i, QPointF(p.x() * stretch_x, p.y() * stretch_y), false);
            }
            firePointChange();
            break;
        }
        case QEvent::Paint:
        {
            QWidget *that_widget = m_widget;
            m_widget = nullptr;
            QApplication::sendEvent(object, event);
            m_widget = that_widget;
            paintPoints();
            return true;
        }
        default:
            break;
        }
    }

    return false;
}

void HoverPoints::paintHistogram(QPainter &p)
{
    if (m_histogram.isEmpty())
    {
        p.fillRect(0, 0, int(boundingRect().width()), int(boundingRect().height()), Qt::transparent);
        return;
    }

    qreal barWidth = qMax(boundingRect().width() / static_cast<qreal>(m_histogram.size()), 1.);
    QVector<QPointF> points(m_histogram.size() + 4);    // 1 point per bin center + 4 corners
    points[0] = QPointF(0., boundingRect().height());
    qreal h = qBound(0., m_histogram[0], 1.) * boundingRect().height();
    points[1] = QPointF(0., boundingRect().height() - h);

    for (int i = 0; i < m_histogram.size(); ++i)
    {
        h = qBound(0., m_histogram[i], 1.) * boundingRect().height();
        points[i+2] = QPointF(barWidth * i + 0.5 * barWidth, boundingRect().height() - h);
    }
    points[m_histogram.size() + 2] = QPointF(boundingRect().width(), boundingRect().height() - h);
    points[m_histogram.size() + 3] = QPointF(boundingRect().width(), boundingRect().height());

    p.setPen(m_histoPen);
    p.setBrush(m_histoBrush);
    p.drawPolygon(points.data(), m_histogram.size() + 4);
}

void HoverPoints::paintPoints()
{
    QPainter p;
    p.begin(m_widget);
    p.setRenderHint(QPainter::Antialiasing);
    paintHistogram(p);
    if (m_connectionPen.style() != Qt::NoPen && m_connectionType != NoConnection)
    {
        p.setPen(m_connectionPen);
        p.setBrush(m_curveBrush);
        if (m_connectionType == CurveConnection)
        {
            QPainterPath path;
            path.moveTo(m_points.at(0));
            for (int i=1; i<m_points.size(); ++i)
            {
                QPointF p1 = m_points.at(i-1);
                QPointF p2 = m_points.at(i);
                qreal distance = p2.x() - p1.x();

                path.cubicTo(p1.x() + distance / 2, p1.y(),
                             p1.x() + distance / 2, p2.y(),
                             p2.x(), p2.y());
            }
            p.drawPath(path);
        }
        else
        {
            p.drawPolyline(m_points);
        }
    }

    p.setPen(m_pointPen);
    p.setBrush(m_pointBrush);
    for (int i = 0; i < m_points.size(); ++i)
    {
        m_pointBrush.setColor(m_colors.at(i));
        p.setBrush(m_pointBrush);
        QRectF bounds = pointBoundingRect(i);
        if (i == m_currentIndex)
            p.setPen(QPen(Qt::red, 2));
        else
            p.setPen(m_pointPen);

        if (m_shape == CircleShape)
            p.drawEllipse(bounds);
        else
            p.drawRect(bounds);
    }
}

static QPointF bound_point(const QPointF &point, const QRectF &bounds, int lock)
{
    QPointF p = point;

    qreal left = bounds.left();
    qreal right = bounds.right();
    qreal top = bounds.top();
    qreal bottom = bounds.bottom();

    if (p.x() < left || (lock & HoverPoints::LockToLeft)) p.setX(left);
    else if (p.x() > right || (lock & HoverPoints::LockToRight)) p.setX(right);

    if (p.y() < top || (lock & HoverPoints::LockToTop)) p.setY(top);
    else if (p.y() > bottom || (lock & HoverPoints::LockToBottom)) p.setY(bottom);

    return p;
}

void HoverPoints::setPoints(const QPolygonF &points)
{
    if (points.size() != m_points.size())
        m_fingerPointMapping.clear();
    m_points.clear();
    m_colors.clear();
    for (int i=0; i<points.size(); ++i)
        m_points << bound_point(points.at(i), boundingRect(), 0);

    m_locks.clear();
    if (m_points.size() > 0)
    {
        m_locks.resize(m_points.size());
        m_colors.resize(m_points.size());
        m_colors.fill(Qt::white);

        m_locks.fill(0);
    }
}


void HoverPoints::setColoredPoints(const QPolygonF &points, QVector<QColor> colors)
{
    if (points.size() != m_points.size())
        m_fingerPointMapping.clear();
    m_points.clear();
    m_colors.clear();
    if (colors.size() == points.size())
    {
        for (int i=0; i<points.size(); ++i)
        {
            m_points << bound_point(points.at(i), boundingRect(), 0);
            m_colors.push_back(colors.at(i));
        }
    }
    m_locks.clear();
    if (m_points.size() > 0) {
        m_locks.resize(m_points.size());
        m_locks.fill(0);
        if (m_colors.size() < m_points.size())
        {
            m_colors.resize(m_points.size());
            m_colors.fill(Qt::white);
        }
    }
}


void HoverPoints::movePoint(int index, const QPointF &point, bool emitUpdate)
{
    m_points[index] = bound_point(point, boundingRect(), int(m_locks.at(index)));
    if (emitUpdate)
        firePointChange();
}


inline static bool x_less_than(const QPointF &p1, const QPointF &p2)
{
    return p1.x() < p2.x();
}


inline static bool y_less_than(const QPointF &p1, const QPointF &p2)
{
    return p1.y() < p2.y();
}

void HoverPoints::firePointChange()
{
    if (m_sortType != NoSort)
    {
        QPointF oldCurrent;
        if (m_currentIndex != -1 && m_currentIndex < m_points.size())
        {
            oldCurrent = m_points.at(m_currentIndex);
        }

        if (m_sortType == XSort)
            std::sort(m_points.begin(), m_points.end(), x_less_than);
        else if (m_sortType == YSort)
            std::sort(m_points.begin(), m_points.end(), y_less_than);

        // Compensate for changed order
        if (m_currentIndex != -1)
        {
            for (int i = 0; i < m_points.size(); ++i)
            {
                if (m_points[i] == oldCurrent)
                {
                    m_currentIndex = i;
                    break;
                }
            }
        }
    }

    emit pointsChanged(m_points);
    if (m_currentIndex >= 0 && m_colors.size() > m_currentIndex)
        emit selectionChanged(m_colors.at(m_currentIndex));
}
