#ifndef _GEOMETRY_HPP_
#define _GEOMETRY_HPP_

#include "vec3.hpp"

class Ray {
public:
    ~Ray() {}
    Ray() = delete;
    Ray(const vec3& o, const vec3& dir) : _origin(o), _direction(dir) {}
    Ray(const Ray& r) : _origin(r.origin()), _direction(r.direction()) {}
    inline vec3 origin() const { return _origin; }
    inline vec3 direction() const { return _direction; }
    inline vec3 parameterize_at(const double t) const { return _origin + _direction * t; }
    Ray operator=(const Ray& r) { return Ray(r); }
    friend std::ostream & operator<<(std::ostream &os, const Ray& r) {
        os << "[" << r.origin() << " " << r.direction() << "]";
        return os;
    }
private:
    vec3 _origin;
    vec3 _direction;
};

class Material;
class Sphere {
public:
    ~Sphere() {}
    Sphere() = delete;
    Sphere(const Sphere& s) :
        _origin(s.origin()),
        _radius(s.radius()),
        _matte(s.matte()) {}

    Sphere(const vec3& o, const double r, Material *m) :
        _origin(o),
        _radius(r),
        _matte(m) {}

    inline vec3 origin() const { return _origin; }
    inline double radius() const { return _radius; }
    inline Material* matte() const { return _matte; }

    Sphere operator=(const Sphere& s) { return Sphere(s); }
    friend std::ostream & operator<<(std::ostream &os, const Sphere& s) {
        os << "[" << s.origin() << " " << s.radius() << "]";
        return os;
    }
    bool intersect(const Ray& r, double& t0, double& t1, bool& inside);
private:
    vec3 _origin;
    double _radius;
    Material *_matte;
};

bool Sphere::intersect(const Ray& r, double& t0, double& t1, bool& inside) {
    vec3 oc = r.origin() - _origin;
    if (dot(oc, oc) < _radius*_radius) {
        /* ray origin is inside the sphere
         */
        inside = true;
    } else {
        /* ray origin is outside the sphere
         */
        inside = false;
    }
    double a = dot(r.direction(), r.direction());
    double b = dot(oc, r.direction()) * 2.0;
    double c = dot(oc, oc) - _radius * _radius;
    double delta = b*b - 4*a*c;

    if (delta >= 0.0) {
        t0 = (-b - sqrt(delta))/(2.0*a);
        t1 = (-b + sqrt(delta))/(2.0*a);
        return true;
    }
    return false;
}

class Material {
public:
    ~Material() {}
    Material(const vec3& kd, const vec3& ks, const double sf, const bool tsp, const double ri) :
        _kdiffuse(kd),
        _kspecular(ks),
        _specular_factor(sf),
        _transparent(tsp),
        _refract_idx(ri) {}
    Material(const Material&) = delete;
    inline vec3 kdiffuse() const { return _kdiffuse; }
    inline vec3 kspecular() const { return _kspecular; }
    inline double specular_factor() const { return _specular_factor; }
    inline bool transparent() const { return _transparent; }
    inline double refract_idx() const { return _refract_idx; }
private:
    vec3 _kdiffuse;
    vec3 _kspecular;
    double _specular_factor;
    bool _transparent;
    double _refract_idx;
};

class LightBase {
public:
    ~LightBase() {}
    LightBase() = delete;
    explicit LightBase(const vec3& o, const vec3& i, double e) : _origin(o), _illumination(i), _energy(e) {}
    inline vec3 origin() const { return _origin; }
    inline vec3 illumination() const { return _illumination*_energy; }
    inline double energy() const { return _energy; }
    virtual vec3 calc_illumination(const vec3& pos) const = 0;
    friend std::ostream & operator<<(std::ostream &os, const LightBase& l) {
        os << "[" << l.origin() << " " << l.illumination() << "]";
        return os;
    }
private:
    vec3 _origin;
    vec3 _illumination;
    double _energy;
};

class ConstantLight : public LightBase{
public:
    ~ConstantLight() {}
    ConstantLight() = delete;
    explicit ConstantLight(const vec3& o, const vec3& i, double e) : LightBase(o, i, e) {}
    ConstantLight operator=(const ConstantLight& l) = delete;
    vec3 calc_illumination(const vec3& pos) const final { return this->illumination(); }
};
#endif
