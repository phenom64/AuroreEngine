/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

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
    using glm::operator==;
    using glm::operator!=;

    using glm::radians;

    using glm::translate;
    using glm::rotate;
    using glm::scale;
    using glm::perspective;
    using glm::lookAt;
}
