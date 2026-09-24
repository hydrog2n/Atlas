#include "atlas/render/layer.hpp"

namespace atlas::render {

LayerStack::LayerStack()
    : orderedLayers_{
        LayerKind::background,
        LayerKind::grid,
        LayerKind::geometry,
        LayerKind::interaction,
        LayerKind::annotations} {}

const std::vector<LayerKind>& LayerStack::orderedLayers() const noexcept {
    return orderedLayers_;
}

} // namespace atlas::render
