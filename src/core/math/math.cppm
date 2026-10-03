module;
// Vulkan's NDC Z range is [0,1], not OpenGL's [-1,1] that glm::perspective/ortho/frustum assume by default
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
export module GPP.Core:Math;

export import glm;

export namespace glm {
    using glm::perspective;
    using glm::lookAt;
    using glm::translate;
    using glm::rotate;
    using glm::scale;
    using glm::value_ptr;
}