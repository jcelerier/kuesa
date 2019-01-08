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
#include <SceneEntity>
#include "gltf2exporter.h"
#include "gltf2context.h"
#include "gltf2context_p.h"
#include "dracocompressor_p.h"
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

SceneEntity *GLTF2Exporter::scene() const
{
    return m_scene;
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

QJsonObject GLTF2Exporter::compress(
        QDir gltfFileDir,
        QJsonObject rootObject)
{
    auto buffers = rootObject["buffers"].toArray();
    auto bufferViews = rootObject["bufferViews"].toArray();
    const auto nbBuffers = buffers.size();

    QByteArray compressedBufferData;
    // Meshes are added in to the scene entity as they appear as primitives in the mesh attribute

    // TODO if we have an already-compressed mesh, we should skip it
    // We must keep a link to the bufferView in the Mesh here
    for (const auto &meshName : m_scene->meshes()->names()) {
        const auto encodedBuffer = compressMesh(*m_scene->mesh(meshName)->geometry());
        compressedBufferData.push_back(QByteArray { encodedBuffer.get()->data(), static_cast<int>(encodedBuffer.get()->size()) });
    }

    // Remove the compressed data from the original buffers and resave them (for now, in a "compressed" folder)

    // Adjust the bufferView arrays
    // before.bin: [ A ] [ m1 ] [ B ] [ C ] [ m2 ] [ D ]
    // after.bin : [ A ] [ B ] [ C ] [ D ]
    // -> m1, m2 must refer to compressedBuffer.bin


    // Adjust the attributes to add the KHR_... extension
    // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md




    // Save all the compressed data in a monolithic buffer
    QFile compressedBufferFile(
            QUrl(gltfFileDir.filePath({ "compressedBuffer.bin" })).toLocalFile());
    if (compressedBufferFile.open(QIODevice::WriteOnly))
        compressedBufferFile.write(compressedBufferData);

    QJsonObject compressedBufferObject;
    compressedBufferObject["byteLength"] = compressedBufferData.size();
    compressedBufferObject["uri"] = QUrl(gltfFileDir.filePath({ "compressedBuffer.bin" })).toLocalFile();
    buffers.push_back(compressedBufferObject);
    rootObject["buffers"] = buffers;

    return rootObject;
}

void GLTF2Exporter::setScene(SceneEntity *scene)
{
    if (m_scene == scene)
        return;

    m_scene = scene;
    emit sceneChanged(m_scene);
}

QJsonDocument GLTF2Exporter::updateDocument(QJsonDocument doc) const Q_DECL_NOEXCEPT
{
    // Buffer compression handling


    return doc;
}
} // namespace Kuesa

QT_END_NAMESPACE
