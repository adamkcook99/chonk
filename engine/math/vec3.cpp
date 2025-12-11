#include "vec3.h"

namespace chonk {
namespace math {

bool AABB::intersects(const Vec3& origin, const Vec3& dir, float& tmin, float& tmax) const {
    tmin = 0.0f;
    tmax = std::numeric_limits<float>::max();
    
    for (int i = 0; i < 3; ++i) {
        float invD = 1.0f / ((&dir.x)[i]);
        float t0 = ((&min.x)[i] - (&origin.x)[i]) * invD;
        float t1 = ((&max.x)[i] - (&origin.x)[i]) * invD;
        
        if (invD < 0.0f) {
            float temp = t0;
            t0 = t1;
            t1 = temp;
        }
        tmin = t0 > tmin ? t0 : tmin;
        tmax = t1 < tmax ? t1 : tmax;
        
        if (tmax < tmin) return false;
    }
    return true;
}

} // namespace math
} // namespace chonk

