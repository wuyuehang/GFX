#ifndef _VEC3_HPP_
#define _VEC3_HPP_

#include <iostream>
#include <cmath>

#if 1
#ifdef assert
#undef assert
#define assert(expr)
#else
#define assert(expr)
#endif
#else
#include <cassert>
#endif

class vec3 {
public:
    ~vec3() {}
    vec3() : _x(0), _y(0), _z(0) {}
    vec3(const vec3& v) : _x(v.x()), _y(v.y()), _z(v.z()) {}
    vec3(const double x, const double y, const double z) : _x(x), _y(y), _z(z) {}
    inline double x() const { return _x; }
    inline double y() const { return _y; }
    inline double z() const { return _z; }
    inline double r() const { return _x; }
    inline double g() const { return _y; }
    inline double b() const { return _z; }
    vec3 operator=(const vec3& v) {
        _x = v.x();
        _y = v.y();
        _z = v.z();
        return *this;
    }

    friend std::ostream & operator<<(std::ostream &os, const vec3& v) {
        os << "[" << v.x() << " " << v.y() << " " << v.z() << "]";
        return os;
    }

    vec3 operator+(const vec3& v) const {
        return vec3(_x + v.x(), _y + v.y(), _z + v.z());
    }

    vec3& operator+=(const vec3& v) {
        _x += v.x();
        _y += v.y();
        _z += v.z();
        return *this;
    }

    vec3 operator-() const {
        return vec3(-_x, -_y, -_z);
    }

    vec3 operator-(const vec3& v) const {
        return vec3(_x - v.x(), _y - v.y(), _z - v.z());
    }

    vec3 operator*(const vec3& v) const {
        return vec3(_x * v.x(), _y * v.y(), _z * v.z());
    }

    vec3 operator*(const double t) const {
        return vec3(_x * t, _y * t, _z * t);
    }

    vec3& normalize() {
        double nor = sqrt(_x*_x + _y*_y + _z*_z);
        if (nor > double(0.0)) {
            double nor_inv = 1.0 / nor;
            _x *= nor_inv;
            _y *= nor_inv;
            _z *= nor_inv;
        } else {
            assert(nor == 0.0);
        }
        return *this;
    }

private:
    double _x, _y, _z;
};

double dot(const vec3& u, const vec3& v) {
    double res = u.x() * v.x() + u.y() * v.y() + u.z() * v.z();
    return res;
}

vec3 cross(const vec3& u, const vec3& v) {
    return vec3(
        u.y() * v.z() - u.z() * v.y(),
        u.z() * v.x() - u.x() * v.z(),
        u.x() * v.y() - u.y() * v.x());
}

vec3 reflect(const vec3& i, const vec3& n) {
    assert(dot(i, n) <= 0.0);
    vec3 r = i - n * dot(i, n) * 2.0;
    assert(dot(r, n) >= 0.0);
    return r;
}

vec3 refract(const vec3& i, const vec3& n, const double eta) {
    /* i and n should be normalized
     */
    assert(fabs(i.x()*i.x() + i.y()*i.y() + i.z()*i.z() - 1.0) <= 1e-8);
    assert(fabs(n.x()*n.x() + n.y()*n.y() + n.z()*n.z() - 1.0) <= 1e-8);
    assert(dot(i, n) <= 0.0);

    vec3 res;
    double c = - dot(i, n);

    double r = eta;

    double delta = 1.0 - r*r*(1 - c*c);

    if (delta < 0.0) {
        /* represent internal full reflect, no refract
         */
        return res;
    }

    res = i*r + n*(r*c - sqrt(delta));
    assert(dot(res, n) <= 0.0);

    return res;
}
#endif
