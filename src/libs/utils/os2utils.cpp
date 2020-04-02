/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "os2utils.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QWidget>

#include <qt_os2.h>

using namespace Utils;


void Os2Utils::openFolder(QWidget *parent, const QString &path)
{
    QByteArray name = QFile::encodeName( QDir::toNativeSeparators(path) );
    int        n = name.lastIndexOf('\\');
    HOBJECT    hFldr;

    if (n < 2) {
        name = "<WP_DRIVES>";
    } else {
        if (n == 2)
            n++;
        name.truncate( n );
    }

    hFldr = WinQueryObject(name.constData());
    if (hFldr) {
        // call this twice in order to bring the folder
        // to the top when it is not already open.
        WinOpenObject(hFldr, 0, TRUE);
        WinOpenObject(hFldr, 0, TRUE);
    }
}

