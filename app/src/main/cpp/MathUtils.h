#ifndef ORBITAL_VELOCITY_MATH_UTILS_H
#define ORBITAL_VELOCITY_MATH_UTILS_H

#include <cmath>

struct Vec2 {
    float x, y;

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }

    float lengthSq() const { return x * x + y * y; }
    float length() const { return std::sqrt(lengthSq()); }

    Vec2 normalized() const {
        float l = length();
        if (l > 0) return *this / l;
        return {0, 0};
    }

    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    float cross(const Vec2& other) const { return x * other.y - y * other.x; }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

#endif // ORBITAL_VELOCITY_MATH_UTILS_H
