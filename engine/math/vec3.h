#pragma once

#include <cstdint>
#include <cmath>
#include <limits>

namespace chonk {
namespace math {

struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(float scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
    Vec3 operator/(float scalar) const { return Vec3(x / scalar, y / scalar, z / scalar); }
    
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    float lengthSquared() const { return x*x + y*y + z*z; }
    Vec3 normalize() const { float len = length(); return len > 0.0001f ? Vec3(x/len, y/len, z/len) : Vec3(); }
    float dot(const Vec3& other) const { return x*other.x + y*other.y + z*other.z; }
    Vec3 cross(const Vec3& other) const { 
        return Vec3(y*other.z - z*other.y, z*other.x - x*other.z, x*other.y - y*other.x);
    }
};

struct AABB {
    Vec3 min;
    Vec3 max;
    
    AABB() {}
    AABB(const Vec3& min, const Vec3& max) : min(min), max(max) {}
    
    bool intersects(const Vec3& origin, const Vec3& dir, float& tmin, float& tmax) const;
    bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    Vec3 getCenter() const { return Vec3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f); }
    Vec3 getSize() const { return Vec3(max.x - min.x, max.y - min.y, max.z - min.z); }
};

} // namespace math
} // namespace chonk

