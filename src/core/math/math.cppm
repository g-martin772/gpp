module;
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