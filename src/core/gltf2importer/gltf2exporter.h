/*
    gltf2exporter.h

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

#ifndef GLTF2EXPORTER_H
#define GLTF2EXPORTER_H

#include <QObject>
#include <QUrl>
#include <QDir>
#include <QJsonObject>
#include <Kuesa/kuesa_global.h>

QT_BEGIN_NAMESPACE

namespace Kuesa {
class GLTF2Context;
class SceneEntity;
class KUESASHARED_EXPORT GLTF2Exporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(GLTF2Context *context READ context WRITE setContext NOTIFY contextChanged)
    Q_PROPERTY(SceneEntity *scene READ scene WRITE setScene NOTIFY sceneChanged)

public:
    explicit GLTF2Exporter(QObject *parent = nullptr);

    GLTF2Context *context() const;
    SceneEntity *scene() const;

Q_SIGNALS:
    void contextChanged(GLTF2Context *context);
    void sceneChanged(SceneEntity *scene);

public Q_SLOTS:
    void save(QUrl target);
    void setContext(GLTF2Context *context);
    void setScene(SceneEntity *scene);

    QJsonObject compress(QDir folder, QJsonObject rootObject);

private:
    QJsonDocument updateDocument(QJsonDocument) const Q_DECL_NOEXCEPT;
    GLTF2Context *m_context;
    SceneEntity *m_scene;
};
} // namespace Kuesa
QT_END_NAMESPACE

#endif // GLTF2EXPORTER_H
