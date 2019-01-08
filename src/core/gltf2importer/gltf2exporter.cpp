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
#include "gltf2keys_p.h"
#include "dracocompressor_p.h"
#include "kuesa_p.h"
#include <set>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {
class QGeometryRenderer;
class QGeometry;
} // namespace Qt3DRender

namespace Kuesa {

namespace {
#if defined(KUESA_DRACO_COMPRESSION)
class GLTF2DracoCompressor
{
public:
    GLTF2DracoCompressor(
            const QJsonObject &rootObject,
            const GLTF2Import::GLTF2ContextPrivate *context)
        : ctx(context)
        , rootObject(rootObject)
        , buffers(rootObject[GLTF2Import::KEY_BUFFERS].toArray())
        , bufferViews(rootObject[GLTF2Import::KEY_BUFFERVIEWS].toArray())
        , accessors(rootObject[GLTF2Import::KEY_ACCESSORS].toArray())
        , meshes(rootObject[GLTF2Import::KEY_MESHES].toArray())
        , compressedBufferIndex(buffers.size()) // Indice of the added buffer
        , newBufferViewIndex(bufferViews.size()) // Indice of the new buffer
    {
    }

    QJsonObject compress(QDir destination)
    {
        // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md

        // 1. Compress all the meshes
        for (int i = 0, n = ctx->meshesCount(); i < n; i++) {
            const auto mesh_json = meshes[i].toObject();
            meshes[i] = compressMesh(mesh_json, i);
        }

        if (compressedBufferData.isEmpty()) {
            return rootObject; // Nothing changed
        }

        // 2. Save all the compressed data in a monolithic buffer and add it to the buffer list
        {
            QFile compressedBufferFile(destination.filePath({ "compressedBuffer.bin" }));
            if (compressedBufferFile.open(QIODevice::WriteOnly)) {
                compressedBufferFile.write(compressedBufferData);
                compressedBufferFile.close();
            }
            QJsonObject compressedBufferObject;
            compressedBufferObject[GLTF2Import::KEY_BYTELENGTH] = compressedBufferData.size();
            compressedBufferObject[GLTF2Import::KEY_URI] = "compressedBuffer.bin";
            buffers.push_back(compressedBufferObject);
        }

        // 3. Adjust the accessors: they don't need to point to a bufferView anymore since it's the Draco extension which is aware of them
        // TODO what happens if an accessor was referred by a mesh but also something else ?
        std::set<int> bufferViews_to_clean = pruneBufferViewsFromAccessors(accessors, accessorsToClean);
        // TODO remove the bufferviews, but this means that we have to update *all* the indices everywhere - not only for meshes.

        // 4. Remove the data from the buffers and adjust the remaining bufferViews
        std::set<int> buffers_to_clean = buffersFromBufferViews(bufferViews, bufferViews_to_clean);

        rootObject[GLTF2Import::KEY_BUFFERS] = std::move(buffers);
        rootObject[GLTF2Import::KEY_BUFFERVIEWS] = std::move(bufferViews);
        rootObject[GLTF2Import::KEY_ACCESSORS] = std::move(accessors);
        rootObject[GLTF2Import::KEY_MESHES] = std::move(meshes);

        addExtension(rootObject, GLTF2Import::KEY_EXTENSIONS_REQUIRED, GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION);
        addExtension(rootObject, GLTF2Import::KEY_EXTENSIONS_USED, GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION);

        return rootObject;
    }

private:
    struct CompressedGLTFPrimitive {
        QJsonObject primitive_json;
        QByteArray compressed_data;
        QJsonObject new_buffer_view;
        std::set<int> accessors_to_clean;
    };

    const GLTF2Import::GLTF2ContextPrivate *ctx;

    QJsonObject rootObject;
    QJsonArray buffers;
    QJsonArray bufferViews;
    QJsonArray accessors;
    QJsonArray meshes;

    QByteArray compressedBufferData;
    const int compressedBufferIndex;
    int newBufferViewIndex;
    std::set<int> accessorsToClean;

    // Compress a single mesh
    QJsonObject compressMesh(QJsonObject mesh_json, int mesh_idx)
    {
        auto &primitives = ctx->mesh(mesh_idx).meshPrimitives;
        auto primitives_json = mesh_json[GLTF2Import::KEY_PRIMITIVES].toArray();

        Q_ASSERT_X(primitives_json.size() == primitives.size(), "GLTF2DracoCompressor::compressMesh", "Bad primitive count");

        for (int p = 0; p < primitives_json.size(); ++p) {
            auto primitive_json = primitives_json[p].toObject();

            // First check if the primitive is already draco-compressed
            auto ext_it = primitive_json.find(GLTF2Import::KEY_EXTENSIONS);
            if (ext_it != primitive_json.end()) {
                auto ext = ext_it->toObject();
                if (ext.contains(GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION)) {
                    continue;
                }
            }

            auto compressed_mesh = compressPrimitive(
                    primitive_json,
                    primitives[p]);

            if (!compressed_mesh.primitive_json.empty()) {
                //* all the side-effects of this method are in this block: */
                primitives_json[p] = compressed_mesh.primitive_json;
                compressedBufferData.push_back(compressed_mesh.compressed_data);
                bufferViews.push_back(compressed_mesh.new_buffer_view);
                accessorsToClean.insert(compressed_mesh.accessors_to_clean.begin(), compressed_mesh.accessors_to_clean.end());

                newBufferViewIndex++;
            } else {
                const auto meshName = mesh_json[GLTF2Import::KEY_NAME].toString().toUtf8();
                if (!meshName.isEmpty())
                    qCWarning(kuesa, "A mesh could not be compressed: %s -> %d", meshName.constData(), p);
                else
                    qCWarning(kuesa, "A mesh could not be compressed: %d -> %d", mesh_idx, p);
            }
        }

        mesh_json[GLTF2Import::KEY_PRIMITIVES] = std::move(primitives_json);
        return mesh_json;
    }

    // Compress a single primitive of a mesh
    CompressedGLTFPrimitive compressPrimitive(
            QJsonObject primitive_json, const Kuesa::GLTF2Import::Primitive &primitive) const
    {
        CompressedGLTFPrimitive compressed_primitive;

        // Do the compression
        const auto compressed = Kuesa::Compression::compressMesh(*primitive.primitiveRenderer->geometry());
        if (!compressed.buffer)
            return {};

        int offset = compressedBufferData.size();
        auto &eb = *compressed.buffer.get();
        int eb_size = static_cast<int>(eb.size());

        compressed_primitive.compressed_data = QByteArray { eb.data(), eb_size };

        // For now we allocate new bufferViews per compressed chunk; then we should do a pass to remove / replace unused bv ?

        // Allocate a new buffer view
        {
            compressed_primitive.new_buffer_view[GLTF2Import::KEY_BUFFER] = compressedBufferIndex;
            compressed_primitive.new_buffer_view[GLTF2Import::KEY_BYTEOFFSET] = offset;
            compressed_primitive.new_buffer_view[GLTF2Import::KEY_BYTELENGTH] = eb_size;
        }

        // Create or modify the extension object
        {
            auto ext_obj = primitive_json[GLTF2Import::KEY_EXTENSIONS].toObject();

            {
                QJsonObject draco_ext;
                draco_ext[GLTF2Import::KEY_BUFFERVIEW] = newBufferViewIndex;
                {
                    QJsonObject draco_ext_attr;
                    for (const auto &[name, idx] : compressed.attributes) {

                        // TODO the names aren't the correct ones, how to get them ?
                        // e.g. vertexNormal instead of NORMAL
                        // for the sake of testing, this dirty hack :
                        static const QMap<QString, QString> nameMap {
                            { "vertexNormal", "NORMAL" },
                            { "vertexPosition", "POSITION" }
                        };
                        draco_ext_attr[nameMap[name]] = idx;
                    }
                    draco_ext[GLTF2Import::KEY_ATTRIBUTES] = draco_ext_attr;
                }

                ext_obj[GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION] = draco_ext;
            }

            primitive_json[GLTF2Import::KEY_EXTENSIONS] = ext_obj;
            qDebug() << ext_obj;
        }

        // Mark the primitive's accessors
        {
            auto it = primitive_json.find(GLTF2Import::KEY_INDICES);
            if (it != primitive_json.end() && it->isDouble()) {
                compressed_primitive.accessors_to_clean.insert(it->toInt());
            }

            it = primitive_json.find(GLTF2Import::KEY_ATTRIBUTES);
            if (it != primitive_json.end() && it->isObject()) {
                const auto attributes = it->toObject();
                for (const auto &attr : attributes) {
                    if (attr.isDouble())
                        compressed_primitive.accessors_to_clean.insert(attr.toInt());
                }
            }
        }
        compressed_primitive.primitive_json = primitive_json;

        return compressed_primitive;
    }

    // Add an extension if it is not already registered - this could be moved at a more generic place
    // if further extensions are to be added
    static void addExtension(QJsonObject &rootObject, const QString &where, const QString &extension)
    {
        auto extensions = rootObject[where].toArray();
        auto ext_it = std::find_if(extensions.begin(), extensions.end(), [&](const QJsonValue &v) {
            return v.toString() == extension;
        });

        if (ext_it == extensions.end()) {
            extensions.push_back(extension);
            rootObject[where] = std::move(extensions);
        }
    }

    static std::set<int> pruneBufferViewsFromAccessors(QJsonArray &accessors, const std::set<int> &indices)
    {
        std::set<int> bufferViews;
        for (int accessor : indices) {
            auto acc = accessors[accessor].toObject();
            auto it = acc.find(GLTF2Import::KEY_BUFFERVIEW);
            if (it != acc.end()) {
                bufferViews.insert(it.value().toInt());
                acc.erase(it);
            }
            accessors[accessor] = std::move(acc);
        }
        return bufferViews;
    }

    static std::set<int> buffersFromBufferViews(const QJsonArray &bufferViews, const std::set<int> &indices)
    {
        std::set<int> buffers;
        for (int bufferView : indices) {
            auto bv = bufferViews[bufferView].toObject();
            auto it = bv.find(GLTF2Import::KEY_BUFFER);
            if (it != bv.end()) {
                buffers.insert(it.value().toInt());
            }
        }
        return buffers;
    }
};
#endif
} // namespace

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
    const auto *ctx = GLTF2Import::GLTF2ContextPrivate::get(m_context);

#if defined(KUESA_DRACO_COMPRESSION)
    GLTF2DracoCompressor compressor(rootObject, ctx);
    rootObject = compressor.compress(gltfFileDir);
#endif

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

    return doc;
}
} // namespace Kuesa

QT_END_NAMESPACE
