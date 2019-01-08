#ifndef DRACOCOMPRESSOR_H
#define DRACOCOMPRESSOR_H

#include <memory>
#include <draco/compression/encode.h>
#include <vector>
#include <stdexcept>
#include <QString>

namespace Qt3DRender {
class QGeometry;
}
namespace Kuesa {
namespace Compression {
struct DracoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct CompressedMesh {
    std::unique_ptr<draco::EncoderBuffer> buffer;
    std::vector<std::pair<QString, int>> attributes;
};

CompressedMesh compressMesh(const Qt3DRender::QGeometry &geometry);
} // namespace Compression
} // namespace Kuesa

#endif // DRACOCOMPRESSOR_H
