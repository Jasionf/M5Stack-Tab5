#include "Object.hpp"
#include "TrigLUT.hpp"
#include <cmath>

namespace Renderer
{

    Object::Object()        
    {
    }

    void Object::calculateBoundingBox() {
        if (vertices.empty()) {
            boundingBoxMin = {0, 0, 0};
            boundingBoxMax = {0, 0, 0};
            return;
        }

        boundingBoxMin.assign(vertices[0].position);
        boundingBoxMax.assign(vertices[0].position);

        for (const auto& vertex : vertices) {
            if (vertex.position.x < boundingBoxMin.x) boundingBoxMin.x = vertex.position.x;
            if (vertex.position.y < boundingBoxMin.y) boundingBoxMin.y = vertex.position.y;
            if (vertex.position.z < boundingBoxMin.z) boundingBoxMin.z = vertex.position.z;

            if (vertex.position.x > boundingBoxMax.x) boundingBoxMax.x = vertex.position.x;
            if (vertex.position.y > boundingBoxMax.y) boundingBoxMax.y = vertex.position.y;
            if (vertex.position.z > boundingBoxMax.z) boundingBoxMax.z = vertex.position.z;
        }

        centreVolume = (boundingBoxMin + boundingBoxMax).divide(2);
    }

    void Object::bakeRotation()
    {
        // No-op fast path: nothing to bake.
        if (rotation.x == 0 && rotation.y == 0 && rotation.z == 0) return;

        // Match the per-vertex rotation order used by Scene::renderObject
        // exactly (X then Y then Z) so the baked result is bit-identical
        // to what the renderer would have produced at the previous
        // rotation. After the bake we zero rotation and recompute the
        // bounding box.
        const int32_t cosX = lookupCosI(rotation.x);
        const int32_t sinX = lookupSinI(rotation.x);
        const int32_t cosY = lookupCosI(rotation.y);
        const int32_t sinY = lookupSinI(rotation.y);
        const int32_t cosZ = lookupCosI(rotation.z);
        const int32_t sinZ = lookupSinI(rotation.z);

        auto rotXYZ = [&](Vector3& v) {
            // X-axis
            int32_t y1 = (v.y * cosX - v.z * sinX) / FIXED_POINT_SCALE;
            int32_t z1 = (v.y * sinX + v.z * cosX) / FIXED_POINT_SCALE;
            v.y = y1; v.z = z1;
            // Y-axis
            int32_t x2 = ( v.x * cosY + v.z * sinY) / FIXED_POINT_SCALE;
            int32_t z2 = (-v.x * sinY + v.z * cosY) / FIXED_POINT_SCALE;
            v.x = x2; v.z = z2;
            // Z-axis
            int32_t x3 = (v.x * cosZ - v.y * sinZ) / FIXED_POINT_SCALE;
            int32_t y3 = (v.x * sinZ + v.y * cosZ) / FIXED_POINT_SCALE;
            v.x = x3; v.y = y3;
        };

        for (auto& vert : vertices) {
            rotXYZ(vert.position);
            rotXYZ(vert.normal);
        }

        rotation.assign(0, 0, 0);
        calculateBoundingBox();
    }

    void Object::bakeScale(int32_t numerator, int32_t denominator)
    {
        if (denominator == 0) return;            // guard against divide-by-zero
        if (numerator == denominator) return;    // identity scale: nothing to do

        for (auto& vert : vertices) {
            // 64-bit intermediates: a position component near INT32_MAX/8
            // (~2.7e8 — i.e. several worlds wide) times a numerator of
            // ~256 still fits in int64.
            vert.position.x = (int32_t)(((int64_t)vert.position.x * numerator) / denominator);
            vert.position.y = (int32_t)(((int64_t)vert.position.y * numerator) / denominator);
            vert.position.z = (int32_t)(((int64_t)vert.position.z * numerator) / denominator);
            // Normals are unit-direction vectors and must not be scaled.
        }

        calculateBoundingBox();
    }

    void Object::addVertex(const Vertex &vertex)
    {
        vertices.push_back(vertex);
    }

    void Object::addTriangle(uint16_t idx1, uint16_t idx2, uint16_t idx3, Material *material)
    {
        triangles.push_back({idx1, idx2, idx3, material});
    }

    void Object::addFace(uint16_t v1, uint16_t v2, uint16_t v3, uint16_t v4, Material *material)
    {
        // Add two triangles to form a face
        addTriangle(v1, v2, v3, material); // First triangle
        addTriangle(v3, v4, v1, material); // Second triangle
    }

    void Object::setPosition(int32_t x, int32_t y, int32_t z)
    {
        position.x = x;
        position.y = y;
        position.z = z;
    }

    void Object::setPosition(const Vector3 &pos)
    {
        position.assign(pos);
    }

    void Object::setRotation(int32_t rotX, int32_t rotY, int32_t rotZ)
    {
        rotation.x = rotX % ANGLE_MAX;
        rotation.y = rotY % ANGLE_MAX;
        rotation.z = rotZ % ANGLE_MAX;
    }

    void Object::setRotation(const Vector3 &rot)
    {
        rotation.assign(rot);
    }

    void Object::rotate(int32_t angleX, int32_t angleY, int32_t angleZ)
    {
        rotation.x = (rotation.x + angleX + ANGLE_MAX) % ANGLE_MAX;
        rotation.y = (rotation.y + angleY + ANGLE_MAX) % ANGLE_MAX;
        rotation.z = (rotation.z + angleZ + ANGLE_MAX) % ANGLE_MAX;
    }

    void Object::rotate(const Vector3 &rot)
    {
        rotation.add(rot);
        rotation.x %= ANGLE_MAX;
        rotation.y %= ANGLE_MAX;
        rotation.z %= ANGLE_MAX;
    }

    void Object::translate(int32_t dx, int32_t dy, int32_t dz)
    {
        position.x += dx;
        position.y += dy;
        position.z += dz;
    }

    void Object::lookAt(const Vector3& target)
    {
        // Calculate the direction vector from the camera to the target
        Vector3 direction = target - position;

        // Calculate the rotation angles to look at the target in 3D space
        rotation.assign(
            -static_cast<int32_t>(std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)) * 180 / M_PI),
            static_cast<int32_t>(std::atan2(direction.x, direction.z) * 180 / M_PI),
            0
        );
    }

    void Object::lookAt(const Object* targetObject)
    {
        lookAt(targetObject->position);
    }
} // namespace Renderer
