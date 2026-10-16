/*
 * Copyright 2026 The andr36oid Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <renderengine/DisplaySettings.h>
#include <ui/GraphicTypes.h>
#include "../GLESRenderEngine.h"
#include "../GLFramebuffer.h"
#include "../GLVertexBuffer.h"
#include "GenericProgram.h"

namespace android {
namespace renderengine {
namespace gl {

/**
 * andr36oid CRT look on the GPU: the layers are drawn into an offscreen texture, then one
 * full-screen pass draws it into the real buffer with screen curvature, a small glow
 * (4 bilinear taps around the pixel, so about 16 texels for 5 fetches) and a coloured
 * phosphor mask (an aperture grille worked out from the pixel position, no texture).
 * Only used while one of those is on; scanlines come from the display controller
 * (hardware/rockchip crt_mask.cpp), not from here.
 */
class CrtFilter {
public:
    explicit CrtFilter(GLESRenderEngine& engine);
    ~CrtFilter() = default;

    // Redirects drawing to the offscreen texture, sized like the output buffer.
    status_t setAsDrawTarget(uint32_t width, uint32_t height);
    // Draws the offscreen texture through the CRT pass into the bound framebuffer.
    status_t render(const DisplaySettings& display);

private:
    GLESRenderEngine& mEngine;
    GLFramebuffer mFbo;
    GLVertexBuffer mMeshBuffer;
    GenericProgram mProgram;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    GLuint mPosLoc;
    GLuint mUvLoc;
    GLuint mTextureLoc;
    GLuint mTexelLoc;
    GLuint mCurveLoc;
    GLuint mGlowLoc;
    GLuint mMaskLoc;
    GLuint mRotatedLoc;
};

} // namespace gl
} // namespace renderengine
} // namespace android
