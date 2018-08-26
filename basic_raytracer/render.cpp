#include "geometry.hpp"
#include <fstream>
#include <cstdlib>
#include <limits>
#include <vector>
#include <iomanip>

// PPM
constexpr int TRACE_W = 1600;
constexpr int TRACE_H = 1200;
constexpr char TRACE_PPM[] = "1600 1200\n";

// AA
constexpr int TRACE_SSAA = 40;
constexpr double TRACE_SSAA_INV = 1.0 / TRACE_SSAA;

// Recursive depth
constexpr uint TRACE_DEPTH = 40;

// Object and canvas plane
constexpr double CANVAS_Z = -2.0;
constexpr double OBJECT_Z = -2.25;
 
// Eye (camera) position
constexpr double PERSPECTIVE_EYE_X = 0.0;
constexpr double PERSPECTIVE_EYE_Y = -0.15;
constexpr double PERSPECTIVE_EYE_Z = 0.0;
static const vec3 ray_origin(0, -0.15, PERSPECTIVE_EYE_Z);

// LI Ambient
static vec3 TRACE_AMBIENT = vec3(0.009, 0.009, 0.01);
#define TRACE_GAMMA 1

// Scanline
static const vec3 topleft(-2, 1, CANVAS_Z);
// u increase from left to right (positive x axis)
static const vec3 u(4.0/TRACE_W, 0.0, 0.0);
// v increase from top to bottom (negative y axis)
static const vec3 v(0.0, -3.0/TRACE_H, 0.0);

static double bias = 1e-7;

#define TRACE_LI_DIFFUSE 1
#define TRACE_LI_SPECULAR 1

vec3 tracer(const std::vector<Sphere *>& objects, const std::vector<LightBase *>& lights, const Ray& r, const uint depth);

int main(int argc, char const *argv[])
{
    std::ofstream pfile;
    pfile.open("render.ppm");
    pfile << "P3\n" << TRACE_PPM << "255\n";

    std::vector<Sphere *> objects_family;
    // OBJECT: origin, radius, material; Material: kdiffuse, kspecular, specular_factor, transparent, refraction index;
    objects_family.push_back(new Sphere(vec3(0, -100.5, OBJECT_Z), 100, new Material(vec3(0.087, 0.094, 0.080), vec3(0.087, 0.094, 0.080), 0.5, false, 0.0)));

    objects_family.push_back(new Sphere(vec3(-1, 0, OBJECT_Z), 0.5, new Material(vec3(0.71, 0.52, 0.57), vec3(0.71, 0.52, 0.57), 1.0, false, 0.0)));
    objects_family.push_back(new Sphere(vec3(0, 0, OBJECT_Z), 0.5, new Material(vec3(0.8, 0.2, 0.2), vec3(0.8, 0.2, 0.2), 2.0, false, 0.0)));
    objects_family.push_back(new Sphere(vec3(1, 0, OBJECT_Z), 0.5, new Material(vec3(0.8, 0.6, 0.2), vec3(0.8, 0.6, 0.2), 4.0, false, 0.0)));

    objects_family.push_back(new Sphere(vec3(-0.85, -0.35, OBJECT_Z+0.75), 0.15, new Material(vec3(0.35, 0.35, 0.25), vec3(0.35, 0.35, 0.25), 8.0, false, 0.0)));
    objects_family.push_back(new Sphere(vec3(-0.55, -0.35, OBJECT_Z+0.75), 0.15, new Material(vec3(0.2, 0.35, 0.5), vec3(0.2, 0.35, 0.5), 16.0, false, 0.0)));
    objects_family.push_back(new Sphere(vec3(-0.25, -0.35, OBJECT_Z+0.75), 0.15, new Material(vec3(0.38, 0.82, 0.71), vec3(0.38, 0.82, 0.71), 32.0, true, 1.3)));

    objects_family.push_back(new Sphere(vec3(0.15, -0.3, OBJECT_Z+1.2), 0.2, new Material(vec3(0.3, 0.8, 0.6), vec3(0.3, 0.8, 0.6), 64.0, true, 1.05)));

    std::vector<LightBase *> lights_family;
    // position, illumination, core energy
    lights_family.push_back(new ConstantLight(vec3(100, 0, 100), vec3(1.0, 1.0, 1.0), 1e4));
    lights_family.push_back(new ConstantLight(vec3(100, 100, 100), vec3(1.0, 1.0, 1.0), 5e2));

    for (int i = 0; i < TRACE_H; i++) {
        for (int j = 0; j < TRACE_W; j++) {
            vec3 res;
            for (int k = 0; k < TRACE_SSAA; k++) {
                vec3 ro = ray_origin;
                vec3 rdir = topleft + u*(j+drand48()) + v*(i+drand48()) - ray_origin;
                rdir.normalize();
                Ray r(ro, rdir);
                res += tracer(objects_family, lights_family, r, 0);
            }
            res = res * TRACE_SSAA_INV;
#if TRACE_GAMMA
            pfile << int(sqrt(res.r())*255) << " " << int(sqrt(res.g())*255) << " " << int(sqrt(res.b())*255) << " ";
#else
            pfile << int(res.r()*255) << " " << int(res.g()*255) << " " << int(res.b()*255) << " ";
#endif
        }
        pfile << "\n";
        std::cout << "Ray Trace Processing: " << std::fixed << std::setprecision(2) << i* 100.0 / TRACE_H << "%\r";
    }

    pfile.close();

    return 0;
}

vec3 tracer(const std::vector<Sphere *>& objects, const std::vector<LightBase *>& lights, const Ray& r, const uint depth) {
    /* iterate until find the hit object
     */
    double tnearest = std::numeric_limits<double>::max();
    Sphere *obj = nullptr;
    /* when non-transparent object is very close to transparent object
     * it becomes very diffult to handle the hit position biasing
     */
    bool ray_origin_inside_object = false;

    for (const auto objiter : objects) {
        double t0 = std::numeric_limits<double>::max();
        double t1 = std::numeric_limits<double>::max();
        bool inside = false;

        if (objiter->intersect(r, t0, t1, inside)) {
            assert((t0 != 0.0) && (t1 != 0.0)); /* origin of ray is on the surface of the object and direct outwards */
            if (inside) {
                assert(t0 < 0.0);
                assert(t1 > 0.0);
                ray_origin_inside_object = true;
                if (t1 < tnearest) {
                    tnearest = t1;
                    obj = objiter;
                }
            } else {
                if ((t0 <= 0.0) && (t1 <= 0.0)) {
                    /* ray is shooting outwards sphere and sphere is behind
                     */
                } else if (t0 >= 0.0) {
                    if (t0 < tnearest) {
                        tnearest = t0;
                        obj = objiter;
                        ray_origin_inside_object = false;
                    }
                } else {
                    assert(0);
                }
            }
        }
    }

    if (nullptr == obj) {
        return TRACE_AMBIENT;
    }

    /* calculate the position and normal of the hit point
     * and add a bias of the original hit point.
     * after parameterize t, we can not gaurantee, that position is absolutely
     * the same relative location of inside or outside the objects
     */ 
    vec3 pos = r.parameterize_at(tnearest);
    vec3 nor;
    vec3 C = TRACE_AMBIENT;

    if(false == obj->matte()->transparent()) {
        nor = pos - obj->origin();
        nor.normalize();
        /* bias hit position outwards sphere's origin for non-transparent objects
         * so that there's no chance for next ray's origin resident inside
         * non-transparent objects after recursive iterations
         */
        while (dot(pos-obj->origin(), pos-obj->origin()) < obj->radius()*obj->radius()) {
            pos += nor * bias;
        }

        /* calculate local illumination (ambient, diffuse, specular)
         * generate shadow ray from hit point towards lights, if it
         * doesn't intersect any objects than shade LI
         */
        for (const auto lightiter : lights) {
            vec3 shadow_ray_dir = lightiter->origin() - pos;
            shadow_ray_dir.normalize();
            Ray shadow_ray(pos, shadow_ray_dir);

            bool inshadow = false;
            for (const auto objiter : objects) {
                double t0 = std::numeric_limits<double>::max();
                double t1 = std::numeric_limits<double>::max();
                bool inside = false;
                if (objiter->intersect(shadow_ray, t0, t1, inside)) {
                    inshadow = true;
                    if (t0 <= 0.0 && t1 <= 0.0) {
                        inshadow = false;
                    }
                }
            }

            if (false == inshadow) {
                double distance = dot(lightiter->origin() - pos, lightiter->origin() - pos);
                distance = 1.0 / distance;

#if TRACE_LI_DIFFUSE
                double diffuse = std::max(0.0, dot(nor, shadow_ray_dir));
                C += lightiter->calc_illumination(pos) * obj->matte()->kdiffuse() * diffuse * distance;
#endif

#if TRACE_LI_SPECULAR
                vec3 pos2eye = ray_origin - pos;
                pos2eye.normalize();

                vec3 specular_light;
                if (dot(shadow_ray_dir, nor) < 0.0) {
                    /* no specular light */
                } else {
                    specular_light = reflect(pos - lightiter->origin(), nor);
                    specular_light.normalize();
                }
                double specular = std::max(0.0, dot(pos2eye, specular_light));

                C += lightiter->calc_illumination(pos) * obj->matte()->kdiffuse() * pow(specular, obj->matte()->specular_factor()) * distance;
#endif
            }
        }
    }

    if (depth < TRACE_DEPTH) {
        /* recursive to calculate global illumination
         */
        if (obj->matte()->transparent()) {
            if (ray_origin_inside_object) {
                /* reflection pull pos towards origin
                 */
                nor = obj->origin() - pos;
                nor.normalize();
                vec3 modify_reflect_pos = pos;
                while (dot(modify_reflect_pos-obj->origin(), modify_reflect_pos-obj->origin()) > obj->radius()*obj->radius()) {
                    modify_reflect_pos += nor * bias;
                }

                vec3 refldir = reflect(r.direction(), nor);
                Ray next_reflect_ray(modify_reflect_pos, refldir.normalize());
                C += tracer(objects, lights, next_reflect_ray, depth+1)*0.25;
                /* refraction push pos outwards origin
                 */
                vec3 modify_refract_pos = pos;
                while (dot(modify_refract_pos-obj->origin(), modify_refract_pos-obj->origin()) < obj->radius()*obj->radius()) {
                    modify_refract_pos = modify_refract_pos - nor * bias;
                }

                vec3 rin = r.direction();
                rin.normalize();

                if (dot(rin, nor) < 0.0) {
                    vec3 refradir = refract(rin, nor, obj->matte()->refract_idx());
                    Ray next_refract_ray(modify_refract_pos, refradir.normalize());
                    C += tracer(objects, lights, next_refract_ray, depth+1)*0.75;
                }
            } else {
                /* reflection push pos outwards origin
                 */
                nor = pos - obj->origin();
                nor.normalize();
                vec3 modify_reflect_pos = pos;
                while(dot(modify_reflect_pos-obj->origin(), modify_reflect_pos-obj->origin()) < obj->radius()*obj->radius()) {
                    modify_reflect_pos += nor * bias;
                }

                vec3 refldir = reflect(r.direction(), nor);
                Ray next_reflect_r(modify_reflect_pos, refldir.normalize());
                C += tracer(objects, lights, next_reflect_r, depth+1)*0.25;
                /* refraction pull pos towards origin
                 */
                vec3 modify_refract_pos = pos;
                while(dot(modify_refract_pos-obj->origin(), modify_refract_pos-obj->origin()) > obj->radius()*obj->radius()) {
                    modify_refract_pos = modify_refract_pos - nor * bias;
                }

                vec3 rin = r.direction();
                rin.normalize();

                vec3 refradir = refract(rin, nor, 1.0 / obj->matte()->refract_idx());
                if (dot(rin, nor) < 0.0) {
                    Ray next_refract_r(modify_refract_pos, refradir.normalize());
                    C += tracer(objects, lights, next_refract_r, depth+1)*0.75;
                }
            }
        } else {
            if (dot(r.direction(), nor) < 0.0) {
                vec3 refldir = reflect(r.direction(), nor);
                Ray next_r(pos, refldir.normalize());
                C += tracer(objects, lights, next_r, depth+1)*0.5;
            }
        }
    }
    return C;
}
