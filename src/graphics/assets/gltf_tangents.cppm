export module GPP.Graphics:Assets.Tangents;

import std;
import glm;

export namespace GPP
{
    // Any unit vector perpendicular to the normal, w = +1.
    [[nodiscard]] inline glm::vec4 ArbitraryTangent(const glm::vec3& normal)
    {
        const glm::vec3 axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 tangent = glm::normalize(axis - normal * glm::dot(normal, axis));
        return {tangent, 1.0f};
    }

    // Per-vertex tangents from triangle UV gradients (angle-weighted accumulation, Gram-Schmidt against the
    // normal). UVs follow glTF (origin top-left, v down); xyz points along increasing u and
    // cross(normal, xyz) * w along decreasing v, the +Y axis of a glTF tangent-space normal map.
    // Vertices without a usable UV gradient get an arbitrary perpendicular tangent. uvs may be empty.
    [[nodiscard]] inline std::vector<glm::vec4> GenerateTangents(std::span<const glm::vec3> positions,
                                                                 std::span<const glm::vec3> normals,
                                                                 std::span<const glm::vec2> uvs,
                                                                 std::span<const std::uint32_t> indices)
    {
        const std::size_t count = positions.size();
        std::vector<glm::vec3> tangentSum(count, glm::vec3(0.0f));
        std::vector<glm::vec3> bitangentSum(count, glm::vec3(0.0f));

        if (uvs.size() == count)
        {
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::uint32_t corner[3] = {indices[i], indices[i + 1], indices[i + 2]};
                if (corner[0] >= count || corner[1] >= count || corner[2] >= count) continue;
                const glm::vec3 e1 = positions[corner[1]] - positions[corner[0]];
                const glm::vec3 e2 = positions[corner[2]] - positions[corner[0]];
                const glm::vec2 d1 = uvs[corner[1]] - uvs[corner[0]];
                const glm::vec2 d2 = uvs[corner[2]] - uvs[corner[0]];
                const float dv1 = -d1.y;
                const float dv2 = -d2.y;
                const float determinant = d1.x * dv2 - d2.x * dv1;
                if (std::abs(determinant) < 1e-20f) continue;
                const glm::vec3 t = (e1 * dv2 - e2 * dv1) / determinant;
                const glm::vec3 b = (e2 * d1.x - e1 * d2.x) / determinant;
                if (glm::dot(t, t) < 1e-30f || glm::dot(b, b) < 1e-30f) continue;
                const glm::vec3 tn = glm::normalize(t);
                const glm::vec3 bn = glm::normalize(b);
                for (int k = 0; k < 3; ++k)
                {
                    const glm::vec3 a = positions[corner[(k + 1) % 3]] - positions[corner[k]];
                    const glm::vec3 c = positions[corner[(k + 2) % 3]] - positions[corner[k]];
                    const float denominator = glm::length(a) * glm::length(c);
                    if (denominator <= 0.0f) continue;
                    const float weight = std::acos(std::clamp(glm::dot(a, c) / denominator, -1.0f, 1.0f));
                    tangentSum[corner[k]] += tn * weight;
                    bitangentSum[corner[k]] += bn * weight;
                }
            }
        }

        std::vector<glm::vec4> result(count);
        for (std::size_t v = 0; v < count; ++v)
        {
            const glm::vec3 n = normals.size() == count ? normals[v] : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 t = tangentSum[v] - n * glm::dot(n, tangentSum[v]);
            if (!(glm::dot(t, t) > 1e-20f))
            {
                result[v] = ArbitraryTangent(n);
                continue;
            }
            const glm::vec3 tangent = glm::normalize(t);
            const float sign = glm::dot(glm::cross(n, tangent), bitangentSum[v]) < 0.0f ? -1.0f : 1.0f;
            result[v] = {tangent, sign};
        }
        return result;
    }
}
