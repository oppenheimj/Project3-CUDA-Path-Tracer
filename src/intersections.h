#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>

#include "sceneStructs.h"
#include "utilities.h"

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t, float gap) {
    return r.origin + (t + gap) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v) {
    return glm::vec3(m * v);
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(Geom box, Ray r, glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    Ray q;
    q.origin    =                multiplyMV(box.inverseTransform, glm::vec4(r.origin   , 1.0f));
    q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        outside = true;
        if (tmin <= 0) {
            tmin = tmax;
            tmin_n = tmax_n;
            outside = false;
        }
        intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin, 0), 1.0f));
        normal = glm::normalize(multiplyMV(box.invTranspose, glm::vec4(tmin_n, 0.0f)));
        return glm::length(r.origin - intersectionPoint);
    }
    return -1;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(Geom sphere, Ray r, glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    float radius = 0.5;

    glm::vec3 ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float vDotDirection = glm::dot(rt.origin, rt.direction);
    float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
    if (radicand < 0) {
        return -1;
    }

    float squareRoot = sqrt(radicand);
    float firstTerm = -vDotDirection;
    float t1 = firstTerm + squareRoot;
    float t2 = firstTerm - squareRoot;

    float t = 0;
    if (t1 < 0 && t2 < 0) {
        return -1;
    } else if (t1 > 0 && t2 > 0) {
        t = min(t1, t2);
        outside = true;
    } else {
        t = max(t1, t2);
        outside = false;
    }

    glm::vec3 objspaceIntersection = getPointOnRay(rt, t, 0);

    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));
    if (!outside) {
        normal = -normal;
    }

    return glm::length(r.origin - intersectionPoint);
}

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
__host__ __device__
bool rayIntersectsTriangle(const Ray r, const Triangle triangle, glm::vec3 &intersection, float &t) {
    glm::vec3 vertex0 = triangle.points[0];
    glm::vec3 vertex1 = triangle.points[1];
    glm::vec3 vertex2 = triangle.points[2];

    glm::vec3 edge1, edge2, h, s, q;
    float a, f, u, v;

    edge1 = vertex1 - vertex0;
    edge2 = vertex2 - vertex0;

    h = glm::cross(r.direction, edge2);
    a = glm::dot(edge1, h);
    if (a > -EPSILON && a < EPSILON) {
        return false;    // This ray is parallel to this triangle.
    }

    f = 1.0 / a;
    s = r.origin - vertex0;
    u = f * glm::dot(s, h);
    if (u < 0.0 || u > 1.0) {
        return false;
    }
    
    q = glm::cross(s, edge1);
    v = f * glm::dot(r.direction, q);
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }

    // At this stage we can compute t to find out where the intersection point is on the line.
    t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        // ray intersection
        intersection = getPointOnRay(r, t, 0);
        return true;
    } else {
        // This means that there is a line intersection but not a ray intersection.
        return false;
    }
}

__host__ __device__
float trianglesIntersectionTest(Geom bb, Ray r, glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    glm::vec3 tmp_intersect;
    glm::vec3 tmp_normal;
    float tmp_t;

    float min_t = FLT_MAX;
    glm::vec3 min_intersect;
    glm::vec3 min_normal;
    glm::vec3 min_color;

    for (int i = 0; i < bb.numTriangles; i++) {
        if (rayIntersectsTriangle(r, bb.device_triangles[i], tmp_intersect, tmp_t)) {
            if (tmp_t < min_t) {
                min_t = tmp_t;
                min_intersect = tmp_intersect;
                min_normal = bb.device_triangles[i].normal;
            }
        }
    }

    intersectionPoint = min_intersect;
    normal = min_normal;
    //outside = true;
    if (min_t < 1000000) {
        float ang = glm::acos(glm::dot(r.direction, normal) / (glm::length(r.direction) * glm::length(normal)));
        outside = glm::degrees(ang) > 90;
    }
   
    return min_t > 10000 ? -1.0 : min_t;
}

__host__ __device__
bool rayIntersectsCube(const Ray r, const Node n) {
    Ray q;
    q.origin = multiplyMV(n.inverseTransform, glm::vec4(r.origin, 1.0f));
    q.direction = glm::normalize(multiplyMV(n.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        //if (glm::abs(qdxyz) > 0.00001f) {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        //}
    }

    return tmax >= tmin && tmax > 0;
}

__host__ __device__
void recurse(const Node* nodes, const int i, const Ray r, float& minT, glm::vec3& minIntersect, glm::vec3& minNormal, glm::vec3& minColor) {
    Node n = nodes[i];
    
    if (rayIntersectsCube(r, n)) {
        if (n.leaf) {
            float tmp_t;
            glm::vec3 tmp_intersect;

            for (int j = 0; j < n.numTriangles; j++) {
                Triangle t = n.device_triangles[j];

                if (rayIntersectsTriangle(r, t, tmp_intersect, tmp_t)) {
                    if (tmp_t < minT) {
                        minT = tmp_t;
                        minIntersect = tmp_intersect;
                        minNormal = t.normal;
                    }
                }
            }
        } else {
            for (int j = n.childrenStartIndex; j < n.childrenStartIndex + 8; j++) {
                recurse(nodes, j, r, minT, minIntersect, minNormal, minColor);
            }
        }
    }
}

__host__ __device__
float octreeIntersectionTest(Geom bb, Ray r, glm::vec3& intersectionPoint, glm::vec3& normal, bool& outside) {
    float minT = FLT_MAX;
    glm::vec3 minIntersect;
    glm::vec3 minNormal;
    glm::vec3 minColor;
    
    recurse(bb.device_tree, 0, r, minT, minIntersect, minNormal, minColor);

    intersectionPoint = minIntersect;
    normal = minNormal;

    if (minT < FLT_MAX) {
        float ang = glm::acos(glm::dot(r.direction, normal) / (glm::length(r.direction) * glm::length(normal)));
        outside = glm::degrees(ang) > 90;
        if (!outside) {
            normal = -normal;
        }
    }

    return minT == FLT_MAX ? -1.0 : minT;
}
