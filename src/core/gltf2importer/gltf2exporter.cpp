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
            QDir basePath,
            const GLTF2Import::GLTF2ContextPrivate *context)
        : ctx(context)
        , m_root(rootObject)
        , m_buffers(rootObject[GLTF2Import::KEY_BUFFERS].toArray())
        , m_bufferViews(rootObject[GLTF2Import::KEY_BUFFERVIEWS].toArray())
        , m_accessors(rootObject[GLTF2Import::KEY_ACCESSORS].toArray())
        , m_meshes(rootObject[GLTF2Import::KEY_MESHES].toArray())
        , m_images(rootObject[GLTF2Import::KEY_IMAGES].toArray())
        , m_compressedBufferIndex(m_buffers.size()) // Indice of the added buffer
        , m_newBufferViewIndex(m_bufferViews.size()) // Indice of the new buffer
        , m_basePath(basePath)
    {
    }

    QJsonObject compress(QDir destination)
    {
        // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md

        // 1. Compress all the meshes
        for (int i = 0, n = ctx->meshesCount(); i < n; i++) {
            const auto mesh_json = m_meshes[i].toObject();
            m_meshes[i] = compressMesh(mesh_json, i);
        }

        if (m_compressedBuffer.isEmpty()) {
            return m_root; // Nothing changed
        }

        // 2. Save all the compressed data in a monolithic buffer and add it to the buffer list
        {
            // TODO use an unique name
            // TODO maybe warn the user of unused assets ?
            QFile compressedBufferFile(destination.filePath({ "compressedBuffer.bin" }));
            if (compressedBufferFile.open(QIODevice::WriteOnly)) {
                compressedBufferFile.write(m_compressedBuffer);
                compressedBufferFile.close();
            }
            QJsonObject compressedBufferObject;
            compressedBufferObject[GLTF2Import::KEY_BYTELENGTH] = m_compressedBuffer.size();
            compressedBufferObject[GLTF2Import::KEY_URI] = "compressedBuffer.bin";
            m_buffers.push_back(compressedBufferObject);
        }

        // 3. Adjust the accessors: they don't need to point to a bufferView anymore since it's the Draco extension which is aware of them
        // TODO what happens if an accessor was referred by a mesh but also something else ?
        std::set<int> bufferViews_to_clean = pruneBufferViewsFromAccessors(m_accessors, m_accessorsToClean);

        // 4. Remove the data from the buffers and adjust the remaining bufferViews
        // We have to update *all* the indices everywhere - not only for meshes.
        removeBufferViews(bufferViews_to_clean);

        // 5. Finalize the JSON
        m_root[GLTF2Import::KEY_BUFFERS] = std::move(m_buffers);
        m_root[GLTF2Import::KEY_BUFFERVIEWS] = std::move(m_bufferViews);
        m_root[GLTF2Import::KEY_ACCESSORS] = std::move(m_accessors);
        m_root[GLTF2Import::KEY_MESHES] = std::move(m_meshes);
        m_root[GLTF2Import::KEY_IMAGES] = std::move(m_images);

        addExtension(m_root, GLTF2Import::KEY_EXTENSIONS_REQUIRED, GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION);
        addExtension(m_root, GLTF2Import::KEY_EXTENSIONS_USED, GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION);

        return m_root;
    }

private:
    struct CompressedGLTFPrimitive {
        QJsonObject primitiveJson;
        QByteArray compressedData;
        QJsonObject newBufferView;
        std::set<int> accessorsToClean;
    };

    const GLTF2Import::GLTF2ContextPrivate *ctx;

    QJsonObject m_root;
    QJsonArray m_buffers;
    QJsonArray m_bufferViews;
    QJsonArray m_accessors;
    QJsonArray m_meshes;
    QJsonArray m_images;

    QByteArray m_compressedBuffer;
    const int m_compressedBufferIndex;
    int m_newBufferViewIndex;
    std::set<int> m_accessorsToClean;
    QDir m_basePath;

    // Compress a single mesh
    QJsonObject compressMesh(QJsonObject mesh_json, int mesh_idx)
    {
        const auto &primitives = ctx->mesh(mesh_idx).meshPrimitives;
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

            if (!compressed_mesh.primitiveJson.empty()) {
                //* all the side-effects of this method are in this block: */
                primitives_json[p] = compressed_mesh.primitiveJson;
                m_compressedBuffer.push_back(compressed_mesh.compressedData);
                m_bufferViews.push_back(compressed_mesh.newBufferView);
                m_accessorsToClean.insert(compressed_mesh.accessorsToClean.begin(), compressed_mesh.accessorsToClean.end());

                m_newBufferViewIndex++;
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

        int offset = m_compressedBuffer.size();
        auto &eb = *compressed.buffer.get();
        int eb_size = static_cast<int>(eb.size());

        compressed_primitive.compressedData = QByteArray{ eb.data(), eb_size };

        // For now we allocate new bufferViews per compressed chunk; then we should do a pass to remove / replace unused bv ?

        // Allocate a new buffer view
        {
            compressed_primitive.newBufferView[GLTF2Import::KEY_BUFFER] = m_compressedBufferIndex;
            compressed_primitive.newBufferView[GLTF2Import::KEY_BYTEOFFSET] = offset;
            compressed_primitive.newBufferView[GLTF2Import::KEY_BYTELENGTH] = eb_size;
        }

        // Create or modify the extension object
        {
            auto ext_obj = primitive_json[GLTF2Import::KEY_EXTENSIONS].toObject();

            {
                QJsonObject draco_ext;
                draco_ext[GLTF2Import::KEY_BUFFERVIEW] = m_newBufferViewIndex;
                {
                    QJsonObject draco_ext_attr;
                    for (const auto &attribute : compressed.attributes) {

                        // TODO the names aren't the correct ones, how to get them ?
                        // e.g. vertexNormal instead of NORMAL
                        // for the sake of testing, this dirty hack :
                        static const QMap<QString, QString> nameMap{
                            { "vertexNormal", "NORMAL" },
                            { "vertexPosition", "POSITION" }
                        };
                        draco_ext_attr[nameMap[attribute.first]] = attribute.second;
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
                compressed_primitive.accessorsToClean.insert(it->toInt());
            }

            it = primitive_json.find(GLTF2Import::KEY_ATTRIBUTES);
            if (it != primitive_json.end() && it->isObject()) {
                const auto attributes = it->toObject();
                for (const auto &attr : attributes) {
                    if (attr.isDouble())
                        compressed_primitive.accessorsToClean.insert(attr.toInt());
                }
            }
        }
        compressed_primitive.primitiveJson = primitive_json;

        return compressed_primitive;
    }

    void removeBufferViews(const std::set<int> &indices)
    {
        // First compute the new indicides of the buffer views
        std::map<int, int> bufferViewIndex;
        int currentOffset = 0;
        for (int i = 0, n = m_bufferViews.size(); i < n; i++) {
            if (indices.count(i) != 0) {
                currentOffset++;
            }
            bufferViewIndex[i] = i - currentOffset;
        }

        // Fix them in the accessors
        // TODO sparse accessors
        for (int i = 0; i < m_accessors.size(); i++) {
            auto acc = m_accessors[i].toObject();

            auto it = acc.find(GLTF2Import::KEY_BUFFERVIEW);
            if (it != acc.end()) {
                it.value() = bufferViewIndex.at(it.value().toInt());
            }

            m_accessors[i] = std::move(acc);
        }

        // Fix them in the meshes
        for (int i = 0; i < m_meshes.size(); i++) {
            auto mesh = m_meshes[i].toObject();
            auto primitives = mesh[GLTF2Import::KEY_PRIMITIVES].toArray();
            for (int p = 0; p < primitives.size(); p++) {
                auto primitive = primitives[p].toObject();

                auto ext_it = primitive.find(GLTF2Import::KEY_EXTENSIONS);
                if (ext_it != primitive.end()) {
                    auto ext = ext_it->toObject();

                    auto draco_it = ext.find(GLTF2Import::KEY_KHR_DRACO_MESH_COMPRESSION_EXTENSION);
                    if (draco_it != ext.end()) {
                        auto draco = draco_it->toObject();
                        draco[GLTF2Import::KEY_BUFFERVIEW] = bufferViewIndex.at(draco[GLTF2Import::KEY_BUFFERVIEW].toInt());
                        *draco_it = std::move(draco);
                    }

                    *ext_it = std::move(ext);
                }

                primitives[p] = std::move(primitive);
            }
            mesh[GLTF2Import::KEY_PRIMITIVES] = std::move(primitives);
            m_meshes[i] = std::move(mesh);
        }

        // Fix them in the images
        for (int i = 0; i < m_images.size(); i++) {
            auto img = m_images[i].toObject();
            auto bv_it = img.find(GLTF2Import::KEY_BUFFERVIEW);
            if (bv_it != img.end()) {
                *bv_it = bufferViewIndex.at(bv_it->toInt());
            }
            m_images[i] = std::move(img);
        }

        // Remove the buffer views
        QJsonArray removedBufferViews;
        // (note: this loop could be merged with the earlier one
        // but this would be a bit less readable imho)
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            auto bv_it = m_bufferViews.begin() + (*it);

            removedBufferViews.push_back(std::move(*bv_it));
            m_bufferViews.erase(bv_it);
        }

        cleanupBuffers(removedBufferViews);
    }

    struct BufferParts {
        int offset;
        int length;
        int stride;
        friend bool operator==(const BufferParts &lhs, const BufferParts &rhs)
        {
            return lhs.offset == rhs.offset;
        }
        friend bool operator<(const BufferParts &lhs, const BufferParts &rhs)
        {
            return lhs.offset < rhs.offset;
        }
    };

    struct BufferRemoval {
        int index;

        std::set<BufferParts> parts;
    };

    static int mapBufferOffset(const std::set<BufferParts> &removed_parts, int offset)
    {
        auto end = std::lower_bound(
                removed_parts.begin(), removed_parts.end(), offset, [](const BufferParts &lhs, int rhs) { return lhs.offset < rhs; });

        for (auto it = removed_parts.begin(); it != end; ++it) {
            offset -= it->length;
        }

        return offset;
    }

    void cleanupBuffers(const QJsonArray &removedBufferViews)
    {
        std::map<int, std::set<BufferParts>> bufferParts;
        // List and order all the parts of the buffers to remove
        for (const QJsonValue &bv_value : removedBufferViews) {
            auto bv = bv_value.toObject();
            auto it = bv.find(GLTF2Import::KEY_BUFFER);
            if (it != bv.end()) {
                int buffer_idx = it.value().toInt();
                auto buf = m_buffers[buffer_idx].toObject();
                auto uri = buf[GLTF2Import::KEY_URI].toString();

                if (QFileInfo::exists(m_basePath.absoluteFilePath(uri))) {
                    BufferParts p;
                    p.offset = bv[GLTF2Import::KEY_BYTEOFFSET].toInt();
                    p.length = bv[GLTF2Import::KEY_BYTELENGTH].toInt();
                    p.stride = bv[GLTF2Import::KEY_BYTESTRIDE].toInt();

                    bufferParts[buffer_idx].insert(p);
                } else {
                    // TODO Maybe we should download it and strip the downloaded asset
                    // There's also the base64 case
                }
            }
        }

        for (auto p : bufferParts) {
            qDebug() << p.first;
            for (auto b : p.second) {
                qDebug() << "offset! " << b.offset << b.length << b.stride;
            }
        }

        // Remove the parts in the buffers
        std::vector<int> buffersToRemove;

        // TODO we should check that all buffer objects map to a unique file
        // else this won't work at all
        for (const auto &buffer : bufferParts) {
            QJsonObject buf = m_buffers[buffer.first].toObject();

            auto uri = buf[GLTF2Import::KEY_URI].toString();
            QFile f(m_basePath.absoluteFilePath(uri));
            if (!f.open(QIODevice::ReadWrite))
                continue;

            int removed_length = 0;

            // TODO it would be better to do it in-place.
            // TODO handle overlapping bufferViews
            // TODO handle strided buffers
            QByteArray data = f.readAll();
            for (auto it = buffer.second.rbegin(); it != buffer.second.rend(); ++it) {
                data.remove(it->offset, it->length);
                removed_length += it->length;
            }

            if (data.isEmpty()) {
                // Actually we could precompute this case...
                buffersToRemove.push_back(buffer.first);
                f.remove();
            } else {
                f.seek(0);
                f.write(data);

                auto len_it = buf.find(GLTF2Import::KEY_BYTELENGTH);
                if (len_it != buf.end()) {
                    (*len_it) = len_it->toInt() - removed_length;
                }

                m_buffers[buffer.first] = std::move(buf);
            }
        }

        // Change the offsets in the bufferViews
        for (auto bv_it = m_bufferViews.begin(); bv_it != m_bufferViews.end(); ++bv_it) {
            auto bv = bv_it->toObject();
            auto it = bv.find(GLTF2Import::KEY_BUFFER);
            if (it != bv.end()) {
                int buffer_idx = it.value().toInt();

                // Change the offset of the data in the buffer
                auto offset_it = bv.find(GLTF2Import::KEY_BYTEOFFSET);
                (*offset_it) = mapBufferOffset(bufferParts[buffer_idx], offset_it->toInt());

                // Change the buffer index if there are removed buffers
                int new_buffer_idx = buffer_idx;
                for (int removed_buf : buffersToRemove) {
                    if (removed_buf < buffer_idx)
                        new_buffer_idx--;
                    else
                        break;
                }
                (*it) = new_buffer_idx;
                (*bv_it) = std::move(bv);
            }
        }

        // Remove unused buffers
        for (int buffer : buffersToRemove) {
            m_buffers.erase(m_buffers.begin() + buffer);
        }

        // TODO Do a final pass to check if there are any unreferenced buffers
        // Or it could maybe be an additional "lint" pass not part of the draco one ?
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

    // Given an accessor array, remove their "bufferView": key, and return the corresponding set of
    // bufferViews
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

    QFile out{ target.toLocalFile() };
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
    GLTF2DracoCompressor compressor(rootObject, gltfFileDir, ctx);
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
