module;

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

export module glm;

export namespace glm {
    using glm::vec1;
    using glm::vec2;
    using glm::vec3;
    using glm::vec4;

    using glm::mat2;
    using glm::mat3;
    using glm::mat4;

    using glm::min;
    using glm::max;
    using glm::clamp;
    using glm::mix;

    using glm::operator+;
    using glm::operator-;
    using glm::operator*;
    using glm::operator/;

    using glm::translate;
    using glm::rotate;
    using glm::scale;
}
