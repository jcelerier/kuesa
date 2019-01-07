/*
    gltf2exporter.cpp

    This file is part of Kuesa.

    Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
    Author: Jean-Michaël Celerier <jean-michael.celerier@kdab.com>

    Licensees holding valid proprietary KDAB Kuesa licenses may use this file in
    accordance with the Kuesa Enterprise License Agreement provided with the Software in the
    LICENSE.KUESA.ENTERPRISE file.

    Contact info@kdab.com if any conditions of this licensing are not clear to you.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <QFile>
#include "gltf2exporter.h"
#include "gltf2context.h"
#include "gltf2context_p.h"
#include "kuesa_p.h"

QT_BEGIN_NAMESPACE

namespace Kuesa {
GLTF2Exporter::GLTF2Exporter(QObject *parent)
    : QObject(parent)
{
}

GLTF2Context *GLTF2Exporter::context() const
{
    return m_context;
}

void GLTF2Exporter::save(QUrl target)
{
    if (!m_context) {
        qCWarning(kuesa, "Tried to save GLTF without a context");
        return;
    }

    QFile out { target.toLocalFile() };
    if (!out.open(QIODevice::WriteOnly)) {
        qCWarning(kuesa, "Could not open file to save GLTF");
        return;
    }

    auto ctx = GLTF2Import::GLTF2ContextPrivate::get(m_context);
    out.write(updateDocument(ctx->json()).toJson());
}

void GLTF2Exporter::setContext(GLTF2Context *context)
{
    if (m_context == context)
        return;

    m_context = context;
    emit contextChanged(m_context);
}

QJsonDocument GLTF2Exporter::updateDocument(QJsonDocument doc) const Q_DECL_NOEXCEPT
{
    // Buffer compression handling


    return doc;
}
} // namespace Kuesa

QT_END_NAMESPACE
