#include "atlas/geometry/envelope.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace atlas::geometry {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr unsigned maximumSubdivisionDepth = 24;

struct EnvelopeSample {
    double station;
    CurveEvaluation evaluation;
    double curvature;
};

Point2D add(Point2D first, Point2D second) {
    return {first.x + second.x, first.y + second.y};
}

Point2D subtract(Point2D first, Point2D second) {
    return {first.x - second.x, first.y - second.y};
}

Point2D multiply(Point2D point, double scalar) {
    return {point.x * scalar, point.y * scalar};
}

double dot(Point2D first, Point2D second) {
    return first.x * second.x + first.y * second.y;
}

double cross(Point2D first, Point2D second) {
    return first.x * second.y - first.y * second.x;
}

double magnitude(Point2D point) {
    return std::hypot(point.x, point.y);
}

Point2D interpolate(Point2D first, Point2D second, double fraction) {
    return add(first, multiply(subtract(second, first), fraction));
}

double pointToSegmentDistance(Point2D point, Point2D start, Point2D end) {
    const auto segment = subtract(end, start);
    const auto squaredLength = dot(segment, segment);
    if (squaredLength == 0.0) return distance(point, start);
    const auto fraction = std::clamp(dot(subtract(point, start), segment) / squaredLength, 0.0, 1.0);
    return distance(point, add(start, multiply(segment, fraction)));
}

Point2D normalized(Point2D point, double epsilon) {
    const auto length = magnitude(point);
    if (!std::isfinite(length) || length <= epsilon) {
        throw std::invalid_argument("Envelope join has a degenerate tangent.");
    }
    return multiply(point, 1.0 / length);
}

void addIssue(RoadEnvelopeResult& result, EnvelopeIssueCode code, double station, std::string message) {
    if (std::none_of(result.issues.begin(), result.issues.end(), [&](const EnvelopeIssue& issue) {
        return issue.code == code;
    })) {
        result.issues.push_back({code, station, std::move(message)});
    }
}

Point2D offsetPoint(const EnvelopeSample& sample, double side, double offset) {
    return add(sample.evaluation.position, multiply(sample.evaluation.normal, side * offset));
}

void appendUnique(std::vector<Point2D>& points, Point2D point, double epsilon) {
    if (points.empty() || distance(points.back(), point) > epsilon) points.push_back(point);
}

std::optional<double> appendJoin(
    std::vector<Point2D>& points,
    const EnvelopeSample& incoming,
    const EnvelopeSample& outgoing,
    double side,
    const RoadEnvelopeOptions& options,
    bool& miterLimitExceeded,
    double& miterLimitStation,
    double& maximumMeasuredDeviation,
    std::size_t& workItems,
    bool& workLimitExceeded) {
    const auto center = incoming.evaluation.position;
    const auto incomingNormal = multiply(incoming.evaluation.normal, side);
    const auto outgoingNormal = multiply(outgoing.evaluation.normal, side);
    const auto turn = std::atan2(cross(incoming.evaluation.tangent, outgoing.evaluation.tangent),
        dot(incoming.evaluation.tangent, outgoing.evaluation.tangent));
    const auto incomingPoint = add(center, multiply(incomingNormal, options.totalWidthMeters * 0.5));
    const auto outgoingPoint = add(center, multiply(outgoingNormal, options.totalWidthMeters * 0.5));

    if (std::abs(turn) <= options.coordinateEpsilon) {
        appendUnique(points, outgoingPoint, options.coordinateEpsilon);
        return std::nullopt;
    }
    const auto normalSum = add(incomingNormal, outgoingNormal);
    if (side * turn > 0.0 && magnitude(normalSum) > options.coordinateEpsilon) {
        const auto miterDirection = normalized(normalSum, options.coordinateEpsilon);
        const auto denominator = dot(miterDirection, outgoingNormal);
        if (std::abs(denominator) > options.coordinateEpsilon) {
            const auto intersection = add(center,
                multiply(miterDirection, options.totalWidthMeters * 0.5 / denominator));
            while (!points.empty() && dot(subtract(points.back(), intersection),
                incoming.evaluation.tangent) > options.coordinateEpsilon) {
                points.pop_back();
            }
            appendUnique(points, intersection, options.coordinateEpsilon);
            return dot(subtract(intersection, center), outgoing.evaluation.tangent);
        }
    }
    if (options.joinStyle == JoinStyle::bevel) {
        appendUnique(points, incomingPoint, options.coordinateEpsilon);
        appendUnique(points, outgoingPoint, options.coordinateEpsilon);
        return std::nullopt;
    }
    if (options.joinStyle == JoinStyle::miter) {
        if (magnitude(normalSum) <= options.coordinateEpsilon) {
            appendUnique(points, incomingPoint, options.coordinateEpsilon);
            appendUnique(points, outgoingPoint, options.coordinateEpsilon);
            return std::nullopt;
        }
        const auto miterDirection = normalized(normalSum, options.coordinateEpsilon);
        const auto denominator = dot(miterDirection, outgoingNormal);
        if (std::abs(denominator) > options.coordinateEpsilon) {
            const auto miterLength = options.totalWidthMeters * 0.5 / denominator;
            if (std::abs(miterLength) <= options.miterLimit * options.totalWidthMeters * 0.5) {
                appendUnique(points, add(center, multiply(miterDirection, miterLength)), options.coordinateEpsilon);
                return std::nullopt;
            }
            miterLimitExceeded = true;
            miterLimitStation = incoming.station;
        }
        appendUnique(points, incomingPoint, options.coordinateEpsilon);
        appendUnique(points, outgoingPoint, options.coordinateEpsilon);
        return std::nullopt;
    }

    const auto radius = options.totalWidthMeters * 0.5;
    const auto errorRatio = std::clamp(options.maximumChordErrorMeters / radius, 0.0, 1.0);
    const auto toleranceAngle = 2.0 * std::acos(std::clamp(1.0 - errorRatio, -1.0, 1.0));
    const auto maximumJoinAngle = std::min(pi / 18.0, toleranceAngle);
    const auto requestedSteps = std::ceil(
        std::abs(turn) / std::max(maximumJoinAngle, options.coordinateEpsilon));
    if (!std::isfinite(requestedSteps) || requestedSteps + 1.0 >
        static_cast<double>(options.maximumWorkItems - std::min(workItems, options.maximumWorkItems))) {
        workLimitExceeded = true;
        return std::nullopt;
    }
    const auto joinSteps = std::max<std::size_t>(1, static_cast<std::size_t>(requestedSteps));
    workItems += joinSteps + 1;
    maximumMeasuredDeviation = std::max(maximumMeasuredDeviation,
        radius * (1.0 - std::cos(std::abs(turn) / (2.0 * static_cast<double>(joinSteps)))));
    for (std::size_t step = 0; step <= joinSteps; ++step) {
        const auto fraction = static_cast<double>(step) / static_cast<double>(joinSteps);
        const auto angle = turn * fraction;
        const Point2D rotatedNormal{
            incomingNormal.x * std::cos(angle) - incomingNormal.y * std::sin(angle),
            incomingNormal.x * std::sin(angle) + incomingNormal.y * std::cos(angle)};
        appendUnique(points, add(center, multiply(rotatedNormal, options.totalWidthMeters * 0.5)),
            options.coordinateEpsilon);
    }
    return std::nullopt;
}

std::vector<Point2D> makeOffsetSide(
    const std::vector<EnvelopeSample>& samples,
    double side,
    const RoadEnvelopeOptions& options,
    bool& miterLimitExceeded,
    double& miterLimitStation,
    double& maximumMeasuredDeviation,
    std::size_t& workItems,
    bool& workLimitExceeded) {
    std::vector<Point2D> points;
    points.reserve(samples.size() + 4);
    if (samples.empty()) return points;
    points.push_back(offsetPoint(samples.front(), side, options.totalWidthMeters * 0.5));
    std::optional<std::pair<Point2D, std::pair<Point2D, double>>> outgoingClip;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const auto& previous = samples[index - 1];
        const auto& current = samples[index];
        if (std::abs(current.station - previous.station) <= options.stationEpsilon &&
            distance(current.evaluation.position, previous.evaluation.position) <= options.coordinateEpsilon) {
            const auto clipDistance = appendJoin(
                points, previous, current, side, options, miterLimitExceeded,
                miterLimitStation, maximumMeasuredDeviation, workItems, workLimitExceeded);
            if (clipDistance) {
                outgoingClip = std::pair{current.evaluation.position,
                    std::pair{current.evaluation.tangent, *clipDistance}};
            }
        } else {
            if (outgoingClip) {
                const auto [origin, tangentAndDistance] = *outgoingClip;
                const auto [tangent, minimumDistance] = tangentAndDistance;
                if (dot(subtract(current.evaluation.position, origin), tangent) +
                    options.coordinateEpsilon < minimumDistance) {
                    continue;
                }
                outgoingClip.reset();
            }
            appendUnique(points, offsetPoint(current, side, options.totalWidthMeters * 0.5),
                options.coordinateEpsilon);
        }
    }
    return points;
}

std::vector<Point2D> makeRing(
    std::vector<Point2D> left,
    std::vector<Point2D> right) {
    std::vector<Point2D> ring;
    ring.reserve(left.size() + right.size());
    ring.insert(ring.end(), left.begin(), left.end());
    ring.insert(ring.end(), right.rbegin(), right.rend());
    return ring;
}

double signedArea(const std::vector<Point2D>& ring) {
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const auto& first = ring[index];
        const auto& second = ring[(index + 1) % ring.size()];
        twiceArea += first.x * second.y - second.x * first.y;
    }
    return twiceArea * 0.5;
}

int orientation(Point2D first, Point2D second, Point2D third, double epsilon) {
    const auto value = cross(subtract(second, first), subtract(third, first));
    if (value > epsilon) return 1;
    if (value < -epsilon) return -1;
    return 0;
}

bool onSegment(Point2D first, Point2D second, Point2D point, double epsilon) {
    return point.x >= std::min(first.x, second.x) - epsilon &&
        point.x <= std::max(first.x, second.x) + epsilon &&
        point.y >= std::min(first.y, second.y) - epsilon &&
        point.y <= std::max(first.y, second.y) + epsilon;
}

bool segmentsIntersect(Point2D a, Point2D b, Point2D c, Point2D d, double epsilon) {
    const auto first = orientation(a, b, c, epsilon);
    const auto second = orientation(a, b, d, epsilon);
    const auto third = orientation(c, d, a, epsilon);
    const auto fourth = orientation(c, d, b, epsilon);
    if (first != second && third != fourth) return true;
    return (first == 0 && onSegment(a, b, c, epsilon)) ||
        (second == 0 && onSegment(a, b, d, epsilon)) ||
        (third == 0 && onSegment(c, d, a, epsilon)) ||
        (fourth == 0 && onSegment(c, d, b, epsilon));
}

bool selfIntersects(
    const std::vector<Point2D>& ring,
    double epsilon,
    std::size_t maximumWorkItems,
    std::size_t& workItems,
    bool& workLimitExceeded) {
    if (ring.size() < 4) return false;
    for (std::size_t first = 0; first < ring.size(); ++first) {
        const auto firstNext = (first + 1) % ring.size();
        for (std::size_t second = first + 1; second < ring.size(); ++second) {
            if (workItems >= maximumWorkItems) {
                workLimitExceeded = true;
                return false;
            }
            ++workItems;
            const auto secondNext = (second + 1) % ring.size();
            if (first == second || firstNext == second || secondNext == first) continue;
            if (segmentsIntersect(ring[first], ring[firstNext], ring[second], ring[secondNext], epsilon)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Point2D> boundedPoints(const std::vector<Point2D>& points, std::size_t maximumCount) {
    if (points.size() <= maximumCount) return points;
    std::vector<Point2D> result;
    result.reserve(maximumCount);
    for (std::size_t index = 0; index < maximumCount; ++index) {
        const auto sourceIndex = index * (points.size() - 1) / (maximumCount - 1);
        result.push_back(points[sourceIndex]);
    }
    return result;
}

std::string resultHash(
    const std::vector<Point2D>& points,
    const RoadEnvelopeOptions& options) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t hash = 14695981039346656037ull;
    const auto appendByte = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    const auto appendString = [&](std::string_view value) {
        for (const auto character : value) appendByte(static_cast<std::uint8_t>(character));
        appendByte(0);
    };
    const auto appendDouble = [&](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (unsigned shift = 0; shift < 64; shift += 8) {
            appendByte(static_cast<std::uint8_t>((bits >> shift) & 0xff));
        }
    };
    const auto appendInteger = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            appendByte(static_cast<std::uint8_t>((value >> shift) & 0xff));
        }
    };
    appendString(curveKernelVersion);
    appendString(roadEnvelopeAlgorithmVersion);
    appendString(roadEnvelopeToleranceVersion);
    appendDouble(options.totalWidthMeters);
    appendDouble(options.maximumStationStep);
    appendDouble(options.maximumChordErrorMeters);
    appendDouble(options.miterLimit);
    appendDouble(options.stationEpsilon);
    appendDouble(options.coordinateEpsilon);
    appendByte(static_cast<std::uint8_t>(options.joinStyle));
    appendInteger(static_cast<std::uint64_t>(options.maximumSamples));
    appendInteger(static_cast<std::uint64_t>(options.maximumPreviewVertices));
    appendInteger(static_cast<std::uint64_t>(options.maximumWorkItems));
    for (const auto& point : points) {
        appendDouble(point.x);
        appendDouble(point.y);
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

std::string primitiveId(const CurvePrimitive& primitive) {
    return std::visit([](const auto& value) { return value.id; }, primitive);
}

} // namespace

RoadEnvelopeResult generateRoadEnvelope(
    const CurveKernel& curve,
    const RoadEnvelopeOptions& options) {
    if (!std::isfinite(options.totalWidthMeters) || options.totalWidthMeters <= 0.0 ||
        !std::isfinite(options.maximumStationStep) || options.maximumStationStep <= 0.0 ||
        !std::isfinite(options.maximumChordErrorMeters) || options.maximumChordErrorMeters <= 0.0 ||
        options.maximumSamples < 4 || options.maximumPreviewVertices < 4 ||
        options.maximumSamples > 100000 || options.maximumPreviewVertices > 100000 ||
        options.maximumWorkItems == 0 || options.maximumWorkItems > 10000000 ||
        !std::isfinite(options.stationEpsilon) || options.stationEpsilon <= 0.0 ||
        !std::isfinite(options.coordinateEpsilon) || options.coordinateEpsilon <= 0.0 ||
        (options.joinStyle == JoinStyle::miter &&
            (!std::isfinite(options.miterLimit) || options.miterLimit < 1.0))) {
        throw std::invalid_argument("Road envelope options are invalid.");
    }

    RoadEnvelopeResult result;
    std::vector<EnvelopeSample> samples;
    std::vector<EnvelopeSample> diagnosticSamples;
    const auto& primitives = curve.primitives();
    const auto sampleAt = [&](const std::string& id, double parameter) {
        return EnvelopeSample{
            curve.stationOfPrimitiveParameter(id, parameter),
            curve.evaluateAtPrimitiveParameter(id, parameter),
            curve.curvatureAtPrimitiveParameter(id, parameter)};
    };
    std::size_t evaluationCount = 0;
    const auto maximumEvaluations = options.maximumSamples * 8;
    bool samplingFailed = false;
    std::function<bool(const std::string&, const EnvelopeSample&, double, const EnvelopeSample&,
        double, unsigned)> refineInterval;
    refineInterval = [&](const std::string& id, const EnvelopeSample& startSample, double startParameter,
        const EnvelopeSample& endSample, double endParameter, unsigned depth) {
        if (samplingFailed) return false;
        if (evaluationCount + 3 > maximumEvaluations) {
            samplingFailed = true;
            return false;
        }
        const auto quarterParameter = startParameter + (endParameter - startParameter) * 0.25;
        const auto middleParameter = (startParameter + endParameter) * 0.5;
        const auto threeQuarterParameter = startParameter + (endParameter - startParameter) * 0.75;
        const auto quarterSample = sampleAt(id, quarterParameter);
        const auto middleSample = sampleAt(id, middleParameter);
        const auto threeQuarterSample = sampleAt(id, threeQuarterParameter);
        evaluationCount += 3;
        diagnosticSamples.insert(diagnosticSamples.end(),
            {quarterSample, middleSample, threeQuarterSample});
        double maximumDeviation = 0.0;
        for (const auto side : {-1.0, 1.0}) {
            const auto startOffset = offsetPoint(startSample, side, options.totalWidthMeters * 0.5);
            const auto endOffset = offsetPoint(endSample, side, options.totalWidthMeters * 0.5);
            for (const auto* sample : {&quarterSample, &middleSample, &threeQuarterSample}) {
                maximumDeviation = std::max(maximumDeviation, pointToSegmentDistance(
                    offsetPoint(*sample, side, options.totalWidthMeters * 0.5), startOffset, endOffset));
            }
        }
        const auto stationSpan = endSample.station - startSample.station;
        if (stationSpan <= options.maximumStationStep &&
            maximumDeviation <= options.maximumChordErrorMeters) {
            if (samples.size() >= options.maximumSamples) {
                samplingFailed = true;
                return false;
            }
            result.maximumMeasuredChordDeviationMeters = std::max(
                result.maximumMeasuredChordDeviationMeters, maximumDeviation);
            samples.push_back(endSample);
            return true;
        }
        if (depth >= maximumSubdivisionDepth) {
            samplingFailed = true;
            return false;
        }
        return refineInterval(id, startSample, startParameter, middleSample, middleParameter, depth + 1) &&
            refineInterval(id, middleSample, middleParameter, endSample, endParameter, depth + 1);
    };

    for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
        const auto id = primitiveId(primitives[primitiveIndex]);
        const auto startSample = sampleAt(id, 0.0);
        const auto endSample = sampleAt(id, 1.0);
        evaluationCount += 2;
        if (samples.size() >= options.maximumSamples) {
            samplingFailed = true;
            break;
        }
        samples.push_back(startSample);
        diagnosticSamples.push_back(startSample);
        if (!refineInterval(id, startSample, 0.0, endSample, 1.0, 0)) break;
    }
    if (samplingFailed) {
        addIssue(result, EnvelopeIssueCode::resourceLimit, 0.0,
            "Envelope subdivision could not meet its station/chord tolerances within the configured work bound.");
        const auto start = curve.evaluateAtStation(0.0);
        const auto end = curve.evaluateAtStation(curve.totalLength());
        const auto halfWidth = options.totalWidthMeters * 0.5;
        result.boundedPreview = {
            add(start.position, multiply(start.normal, halfWidth)),
            add(end.position, multiply(end.normal, halfWidth)),
            add(end.position, multiply(end.normal, -halfWidth)),
            add(start.position, multiply(start.normal, -halfWidth))};
        result.deterministicHash = resultHash(result.boundedPreview, options);
        return result;
    }

    const auto offset = options.totalWidthMeters * 0.5;
    for (const auto& sample : diagnosticSamples) {
        const auto absoluteCurvature = std::abs(sample.curvature);
        if (absoluteCurvature <= options.coordinateEpsilon) continue;
        const auto localRadius = 1.0 / absoluteCurvature;
        if (std::abs(localRadius - offset) <= options.coordinateEpsilon) {
            addIssue(result, EnvelopeIssueCode::cusp, sample.station,
                "A lateral offset reaches the local centerline radius and forms a cusp.");
        } else if (localRadius < offset) {
            addIssue(result, EnvelopeIssueCode::tightRadius, sample.station,
                "The local centerline radius is smaller than the requested lateral offset.");
        }
        if (1.0 - offset * sample.curvature <= options.coordinateEpsilon ||
            1.0 + offset * sample.curvature <= options.coordinateEpsilon) {
            addIssue(result, EnvelopeIssueCode::orientationInversion, sample.station,
                "A lateral offset reverses orientation at this station.");
        }
    }

    bool miterLimitExceeded = false;
    double miterLimitStation = 0.0;
    std::size_t workItems = 0;
    bool workLimitExceeded = false;
    auto left = makeOffsetSide(samples, 1.0, options, miterLimitExceeded,
        miterLimitStation, result.maximumMeasuredChordDeviationMeters, workItems, workLimitExceeded);
    auto right = makeOffsetSide(samples, -1.0, options, miterLimitExceeded,
        miterLimitStation, result.maximumMeasuredChordDeviationMeters, workItems, workLimitExceeded);
    if (workLimitExceeded) {
        addIssue(result, EnvelopeIssueCode::resourceLimit, 0.0,
            "Envelope join generation reached its configured aggregate work bound.");
        const auto start = curve.evaluateAtStation(0.0);
        const auto end = curve.evaluateAtStation(curve.totalLength());
        const auto halfWidth = options.totalWidthMeters * 0.5;
        result.boundedPreview = {
            add(start.position, multiply(start.normal, halfWidth)),
            add(end.position, multiply(end.normal, halfWidth)),
            add(end.position, multiply(end.normal, -halfWidth)),
            add(start.position, multiply(start.normal, -halfWidth))};
        result.deterministicHash = resultHash(result.boundedPreview, options);
        return result;
    }
    if (miterLimitExceeded) {
        addIssue(result, EnvelopeIssueCode::miterLimitExceeded, miterLimitStation,
            "A miter join exceeds the project limit; the bounded preview uses a bevel at that corner.");
    }
    auto ring = makeRing(std::move(left), std::move(right));
    if (ring.size() < 4 || std::abs(signedArea(ring)) <= options.coordinateEpsilon) {
        addIssue(result, EnvelopeIssueCode::orientationInversion, 0.0,
            "The generated envelope has a degenerate or inverted orientation.");
    } else if (signedArea(ring) > 0.0) {
        addIssue(result, EnvelopeIssueCode::orientationInversion, 0.0,
            "The generated envelope orientation is inverted.");
    }
    const auto intersects = selfIntersects(
        ring, options.coordinateEpsilon, options.maximumWorkItems, workItems, workLimitExceeded);
    if (workLimitExceeded) {
        addIssue(result, EnvelopeIssueCode::resourceLimit, 0.0,
            "Envelope intersection validation reached its configured aggregate work bound.");
    } else if (intersects) {
        addIssue(result, EnvelopeIssueCode::selfIntersection, 0.0,
            "The generated envelope polygon self-intersects.");
    }

    result.valid = result.issues.empty();
    result.boundedPreview = boundedPoints(ring, options.maximumPreviewVertices);
    if (result.valid) result.polygon = ring;
    result.deterministicHash = resultHash(result.boundedPreview, options);
    return result;
}

RoadEnvelopeCache::RoadEnvelopeCache(std::size_t maximumEntries)
    : maximumEntries_(maximumEntries) {
    if (maximumEntries_ == 0) throw std::invalid_argument("Road envelope cache capacity must be positive.");
}

RoadEnvelopeCacheLookup RoadEnvelopeCache::getOrGenerate(
    const std::string& authoritativeSourceKey,
    const CurveKernel& curve,
    const RoadEnvelopeOptions& options) {
    if (authoritativeSourceKey.empty()) {
        throw std::invalid_argument("Road envelope cache requires an authoritative source key.");
    }
    const auto optionsHash = resultHash({}, options);
    const auto cacheKey = std::to_string(authoritativeSourceKey.size()) + ":" +
        authoritativeSourceKey + ":" + optionsHash;
    const auto found = entries_.find(cacheKey);
    if (found != entries_.end()) return {found->second, true};

    auto result = std::make_shared<const RoadEnvelopeResult>(generateRoadEnvelope(curve, options));
    entries_.emplace(cacheKey, result);
    if (entries_.size() > maximumEntries_) entries_.erase(entries_.begin());
    return {std::move(result), false};
}

std::size_t RoadEnvelopeCache::size() const noexcept {
    return entries_.size();
}

void RoadEnvelopeCache::clear() noexcept {
    entries_.clear();
}

} // namespace atlas::geometry
