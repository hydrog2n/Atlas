#include "atlas/render/hit_test.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace atlas::render {

std::vector<HitResult> HitTester::orderedHits(
    atlas::geometry::Point2D cursor,
    const std::vector<HitTarget>& targets,
    double tolerance,
    bool includeLocked,
    bool includeHidden) const {
    std::vector<HitResult> result;
    for (const auto& target : targets) {
        if ((!target.visible && !includeHidden) || (target.locked && !includeLocked)) continue;
        const auto targetDistance = atlas::geometry::distance(cursor, target.position);
        if (targetDistance <= tolerance) {
            result.push_back({target.objectId, target.type, target.precedence, targetDistance});
        }
    }
    std::array<std::vector<HitResult>, 4> precedenceBuckets;
    for (auto& hit : result) {
        precedenceBuckets[static_cast<std::size_t>(std::clamp(hit.precedence, 0, 3))].push_back(std::move(hit));
    }

    std::vector<HitResult> ordered;
    ordered.reserve(result.size());
    for (auto& bucket : precedenceBuckets) {
        if (!std::is_sorted(bucket.begin(), bucket.end(), [](const HitResult& left, const HitResult& right) {
            return left.distance < right.distance;
            })) {
            std::stable_sort(bucket.begin(), bucket.end(), [](const HitResult& left, const HitResult& right) {
                return left.distance < right.distance;
            });
        }
        ordered.insert(ordered.end(),
            std::make_move_iterator(bucket.begin()), std::make_move_iterator(bucket.end()));
    }
    return ordered;
}

} // namespace atlas::render
