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
#include <QApplication>
#include <QErrorMessage>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QErrorMessage::qtHandler();

    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGL)
    {
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    }
    else
    {
        fmt.setVersion(3, 0);
    }
    QSurfaceFormat::setDefaultFormat(fmt);

    MainWindow w;
    w.show();

    return a.exec();
}
