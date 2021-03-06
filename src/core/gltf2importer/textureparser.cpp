/*
    textureparser.cpp

    This file is part of Kuesa.

    Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
    Author: Paul Lemire <paul.lemire@kdab.com>

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

#include "textureparser_p.h"

#include "gltf2context_p.h"
#include "kuesa_p.h"
#include "texturesamplerparser_p.h"

#include <Qt3DRender/QTexture>
#include <Qt3DRender/QTextureWrapMode>
#include <Qt3DRender/QTextureImage>

QT_BEGIN_NAMESPACE
using namespace Kuesa::GLTF2Import;

namespace {
const QLatin1String KEY_SAMPLER = QLatin1Literal("sampler");
const QLatin1String KEY_SOURCE = QLatin1Literal("source");
const QLatin1String KEY_NAME = QLatin1Literal("name");
const QLatin1String KEY_GLTF_EXTENSION = QLatin1String("extensions");
const QLatin1String KEY_MSFT_DDS_EXTENSION = QLatin1String("MSFT_texture_dds");

} // namespace

bool TextureParser::parse(const QJsonArray &texturesArray, GLTF2ContextPrivate *context) const
{
    QHash<QUrl, Qt3DRender::QTextureImage *> sharedImages;

    for (const auto &textureValue : texturesArray) {
        const auto &textureObject = textureValue.toObject();

        auto texture = Texture();
        texture.name = textureObject[KEY_NAME].toString();

        auto sourceValue = textureObject[KEY_SOURCE];
        //Check whether there is a compressed texture available and if so, use it instead
        bool isDDSTexture = false;
        if (textureObject.contains(KEY_GLTF_EXTENSION)) {
            const auto &ddsExtension = textureObject[KEY_GLTF_EXTENSION][KEY_MSFT_DDS_EXTENSION];
            if (!ddsExtension.isUndefined()) {
                isDDSTexture = true;
                sourceValue = ddsExtension[KEY_SOURCE];
            }
        }
        if (sourceValue.isUndefined()) {
            qCWarning(kuesa, "Unknown image source for texture");
        } else {
            const auto image = context->image(sourceValue.toInt());
            if (image.url.isEmpty())
                return false; // Not a valid image

            auto texture2d = std::unique_ptr<Qt3DRender::QAbstractTexture>(nullptr);
            if (isDDSTexture) {
                auto textureLoader = new Qt3DRender::QTextureLoader;
                texture2d.reset(textureLoader);
                textureLoader->setSource(image.url);
            } else {
                texture2d.reset(new Qt3DRender::QTexture2D);

                auto *textureImage = sharedImages.value(image.url);
                if (textureImage == nullptr) {
                    textureImage = new Qt3DRender::QTextureImage();
                    textureImage->setSource(image.url);
                    textureImage->setMirrored(false);
                    sharedImages.insert(image.url, textureImage);
                }

                // Add Image to Texture if compatible
                if (ensureImageIsCompatibleWithTexture(textureImage, texture2d.get()))
                    texture2d->addTextureImage(textureImage);
                else
                    qCWarning(kuesa) << "Image with source" << image.url << "is incompatbile with texture" << texture2d->objectName();
            }

            const auto &samplerValue = textureObject[KEY_SAMPLER];
            if (samplerValue.isUndefined()) {
                // Repeat wrappring and auto filtering should be used
                texture2d->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
            } else {
                const auto sampler = context->textureSampler(samplerValue.toInt());
                if (!sampler.textureWrapMode)
                    return false; // We could use default wrapping, but the file is wrong, so we reject it

                texture2d->setWrapMode(*sampler.textureWrapMode.get());
                texture2d->setMinificationFilter(sampler.minificationFilter);
                texture2d->setMagnificationFilter(sampler.magnificationFilter);
            }

            texture2d->setObjectName(textureObject[KEY_NAME].toString());
            texture.texture = texture2d.release();
        }

        context->addTexture(texture);
    }

    return texturesArray.size() > 0;
}

bool TextureParser::ensureImageIsCompatibleWithTexture(Qt3DRender::QAbstractTextureImage *image,
                                                       Qt3DRender::QAbstractTexture *texture)
{
    const Qt3DRender::QAbstractTexture::Target arrayTargets[] = {
        Qt3DRender::QAbstractTexture::Target1DArray,
        Qt3DRender::QAbstractTexture::Target2DArray,
        Qt3DRender::QAbstractTexture::Target3D,
        Qt3DRender::QAbstractTexture::TargetCubeMapArray,
        Qt3DRender::QAbstractTexture::Target2DMultisampleArray
    };

    if (image->layer() != 0 &&
        std::find(std::begin(arrayTargets),
                  std::end(arrayTargets),
                  texture->target()) == std::end(arrayTargets)) {
        qCWarning(kuesa) << "Using an image with layer specified for a non array texture" << texture->objectName();
        return false;
    } else {
        for (const Qt3DRender::QAbstractTextureImage *img : texture->textureImages()) {
            // Non critical error, just warn user
            if (img->layer() == image->layer() &&
                img->mipLevel() == image->mipLevel() &&
                img->face() == image->face()) {
                qCWarning(kuesa) << "Texture contains two images for the same layer and mipmap level";
                break;
            }
        }
    }
    return true;
}

QT_END_NAMESPACE
