#pragma once
// Math ported verbatim from phantomX paid (core/roblox/math/Math.h).
// Self-contained so the paid aim modules paste 1:1 without touching RBX math.
#include <cmath>
#include <algorithm>
#undef min
#undef max

namespace paidm {

struct Vector3 {
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s) const { return Vector3(x / s, y / s, z / s); }
    Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vector3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    Vector3 operator-() const { return Vector3(-x, -y, -z); }
    bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3& o) const { return !(*this == o); }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float LengthSquared() const { return x * x + y * y + z * z; }
    Vector3 Normalized() const { float l = Length(); return l > 0.f ? *this / l : Vector3(); }
    void Normalize() { float l = Length(); if (l > 0.f) { x /= l; y /= l; z /= l; } }
    float Dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vector3 Cross(const Vector3& o) const {
        return Vector3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
    }
    float DistanceTo(const Vector3& o) const { return (*this - o).Length(); }
};

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
    Vector2 operator+(const Vector2& o) const { return Vector2(x + o.x, y + o.y); }
    Vector2 operator-(const Vector2& o) const { return Vector2(x - o.x, y - o.y); }
    bool operator!=(const Vector2& o) const { return x != o.x || y != o.y; }
};

struct Matrix3x3 {
    float data[9]{};
    float& operator[](int i) { return data[i]; }
    const float& operator[](int i) const { return data[i]; }
};

struct Matrix4x4 {
    float m[4][4];
    Matrix4x4() { for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m[r][c] = (r == c) ? 1.f : 0.f; }
};

} // namespace paidm