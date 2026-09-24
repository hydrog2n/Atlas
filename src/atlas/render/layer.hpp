#pragma once

#include <vector>

namespace atlas::render {

enum class LayerKind { background, grid, geometry, interaction, annotations };

class LayerStack {
public:
    LayerStack();

    const std::vector<LayerKind>& orderedLayers() const noexcept;

private:
    std::vector<LayerKind> orderedLayers_;
};

} // namespace atlas::render
