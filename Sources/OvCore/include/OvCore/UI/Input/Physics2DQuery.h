#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace OvCore::UI::Input
{
    struct QueryVector { double x = 0, y = 0, z = 0; };
    struct QueryRay { QueryVector origin, direction; double distance = 0; };
    struct QueryShape2D
    {
        enum class Kind { Box, Circle };
        Kind kind = Kind::Box;
        QueryVector center;
        double halfWidth = 0.5, halfHeight = 0.5, radians = 0;
    };
    // A minimal 2D query backend: oriented rectangles and circles in XY planes.
    // Supports camera rays crossing the plane and coplanar ray intersections.
    inline std::optional<double> Raycast2D(QueryRay ray, const QueryShape2D& shape)
    {
        for (double value : {ray.origin.x,ray.origin.y,ray.origin.z,ray.direction.x,ray.direction.y,ray.direction.z,
            ray.distance,shape.center.x,shape.center.y,shape.center.z,shape.halfWidth,shape.halfHeight,shape.radians})
            if (!std::isfinite(value)) throw std::invalid_argument("Non-finite 2D query");
        if (ray.distance < 0 || shape.halfWidth <= 0 || shape.halfHeight <= 0 ||
            (shape.kind != QueryShape2D::Kind::Box && shape.kind != QueryShape2D::Kind::Circle)) throw std::invalid_argument("Invalid 2D query");
        const double length = std::sqrt(ray.direction.x*ray.direction.x+ray.direction.y*ray.direction.y+ray.direction.z*ray.direction.z);
        if (length <= 1e-12) throw std::invalid_argument("Zero ray direction");
        ray.direction.x /= length; ray.direction.y /= length; ray.direction.z /= length;
        const double c = std::cos(shape.radians), s = std::sin(shape.radians);
        const double ox = ray.origin.x-shape.center.x, oy = ray.origin.y-shape.center.y;
        const double x = c*ox+s*oy, y = -s*ox+c*oy;
        const double dx = c*ray.direction.x+s*ray.direction.y, dy = -s*ray.direction.x+c*ray.direction.y;
        auto inside = [&](double px, double py) { return shape.kind == QueryShape2D::Kind::Circle ?
            px*px+py*py <= shape.halfWidth*shape.halfWidth : std::abs(px) <= shape.halfWidth && std::abs(py) <= shape.halfHeight; };
        if (std::abs(ray.direction.z) > 1e-12)
        {
            const double distance = (shape.center.z-ray.origin.z)/ray.direction.z;
            return distance >= 0 && distance <= ray.distance && inside(x+distance*dx,y+distance*dy) ? std::optional(distance) : std::nullopt;
        }
        if (std::abs(ray.origin.z-shape.center.z) > 1e-8) return std::nullopt;
        if (inside(x,y)) return 0;
        if (shape.kind == QueryShape2D::Kind::Circle)
        {
            const double b = x*dx+y*dy, discriminant = b*b-(x*x+y*y-shape.halfWidth*shape.halfWidth);
            if (discriminant < 0) return std::nullopt;
            const double distance = -b-std::sqrt(discriminant);
            return distance >= 0 && distance <= ray.distance ? std::optional(distance) : std::nullopt;
        }
        double near = 0, far = ray.distance;
        for (auto values : {std::array{x,dx,shape.halfWidth}, std::array{y,dy,shape.halfHeight}})
        {
            const auto [o,d,h] = values;
            if (std::abs(d) <= 1e-12) { if (std::abs(o) > h) return std::nullopt; continue; }
            double a = (-h-o)/d, b = (h-o)/d; if (a > b) std::swap(a,b);
            near = std::max(near,a); far = std::min(far,b); if (near > far) return std::nullopt;
        }
        return near;
    }
}
