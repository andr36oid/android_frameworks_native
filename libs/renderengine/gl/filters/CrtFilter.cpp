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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "CrtFilter.h"

#include <GLES2/gl2.h>
#include <utils/Trace.h>

namespace android {
namespace renderengine {
namespace gl {

// The most the corners get pushed out at full curvature, in half screens.
static constexpr float kMaxCurve = 0.18f;

static const char* kVertexShader = R"SHADER(#version 100
    attribute vec2 aPosition;
    attribute highp vec2 aUV;
    varying highp vec2 vUV;

    void main() {
        vUV = aUV;
        gl_Position = vec4(aPosition, 0.0, 1.0);
    }
)SHADER";

// One pass, at most 5 texture reads per pixel (1 without glow).
static const char* kFragmentShader = R"SHADER(#version 100
    precision mediump float;

    uniform sampler2D uTexture;
    uniform highp vec2 uTexel;   // 1 / buffer size
    uniform highp float uCurve;  // 0 = flat
    uniform float uGlow;         // 0 - 1
    uniform float uMask;         // 0 - 1
    uniform float uRotated;      // 1: the panel is turned, stripes run along x

    varying highp vec2 vUV;

    void main() {
        highp vec2 uv = vUV;
        float edge = 1.0;
        if (uCurve > 0.0) {
            // barrel: the middle bulges out, the corners fall off the glass
            highp vec2 c = uv * 2.0 - 1.0;
            c += c * (c.yx * c.yx) * uCurve;
            highp vec2 d = 1.0 - abs(c);
            edge = clamp(min(d.x / (4.0 * uTexel.x), d.y / (4.0 * uTexel.y)), 0.0, 1.0);
            uv = c * 0.5 + 0.5;
        }

        vec3 col = texture2D(uTexture, uv).rgb;

        if (uGlow > 0.0) {
            // each bilinear tap averages 4 texels: a 4x4 pixel blur from 4 reads
            highp vec2 o = uTexel * 1.5;
            vec3 blur = texture2D(uTexture, uv + vec2( o.x,  o.y)).rgb;
            blur += texture2D(uTexture, uv + vec2(-o.x,  o.y)).rgb;
            blur += texture2D(uTexture, uv + vec2( o.x, -o.y)).rgb;
            blur += texture2D(uTexture, uv + vec2(-o.x, -o.y)).rgb;
            blur *= 0.25;
            // light spills into darker neighbours, bright parts bloom a little
            col += uGlow * (max(blur - col, 0.0) * 0.6 + blur * blur * 0.35);
        }

        if (uMask > 0.0) {
            // aperture grille: every pixel column lets one colour through fully
            highp float x = mix(gl_FragCoord.x, gl_FragCoord.y, uRotated);
            float m = mod(floor(x), 3.0);
            vec3 pass = vec3(step(m, 0.5), step(0.5, m) * step(m, 1.5), step(1.5, m));
            vec3 mask = mix(vec3(1.0 - uMask), vec3(1.0), pass);
            // win back part of the light the mask takes
            col *= mask * (1.0 + 0.6 * uMask);
        }

        gl_FragColor = vec4(col * edge, 1.0);
    }
)SHADER";

CrtFilter::CrtFilter(GLESRenderEngine& engine)
      : mEngine(engine), mFbo(engine), mProgram(engine) {
    mProgram.compile(kVertexShader, kFragmentShader);
    mPosLoc = mProgram.getAttributeLocation("aPosition");
    mUvLoc = mProgram.getAttributeLocation("aUV");
    mTextureLoc = mProgram.getUniformLocation("uTexture");
    mTexelLoc = mProgram.getUniformLocation("uTexel");
    mCurveLoc = mProgram.getUniformLocation("uCurve");
    mGlowLoc = mProgram.getUniformLocation("uGlow");
    mMaskLoc = mProgram.getUniformLocation("uMask");
    mRotatedLoc = mProgram.getUniformLocation("uRotated");

    // one triangle covering the screen, as in BlurFilter
    static constexpr auto size = 2.0f;
    static constexpr auto translation = 1.0f;
    const GLfloat vboData[] = {
        // Vertex data
        translation - size, -translation - size,
        translation - size, -translation + size,
        translation + size, -translation + size,
        // UV data
        0.0f, 0.0f - translation,
        0.0f, size - translation,
        size, size - translation
    };
    mMeshBuffer.allocateBuffers(vboData, 12 /* size */);
}

status_t CrtFilter::setAsDrawTarget(uint32_t width, uint32_t height) {
    ATRACE_NAME("CrtFilter::setAsDrawTarget");
    if (!mProgram.isValid()) {
        ALOGE("CRT shader didn't build");
        return GL_INVALID_OPERATION;
    }
    if (mWidth != width || mHeight != height) {
        mWidth = width;
        mHeight = height;
        mFbo.allocateBuffers(width, height);
    }
    if (mFbo.getStatus() != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("Invalid CRT buffer");
        return mFbo.getStatus();
    }
    mFbo.bind();
    return NO_ERROR;
}

status_t CrtFilter::render(const DisplaySettings& display) {
    ATRACE_NAME("CrtFilter::render");
    glViewport(0, 0, mWidth, mHeight);
    glDisable(GL_BLEND);

    mProgram.useProgram();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFbo.getTextureName());
    glUniform1i(mTextureLoc, 0);
    glUniform2f(mTexelLoc, 1.0f / mWidth, 1.0f / mHeight);
    glUniform1f(mCurveLoc, display.crtCurvature * kMaxCurve);
    glUniform1f(mGlowLoc, display.crtGlow);
    glUniform1f(mMaskLoc, display.crtMask);
    glUniform1f(mRotatedLoc, display.crtRotated ? 1.0f : 0.0f);

    glEnableVertexAttribArray(mUvLoc);
    glEnableVertexAttribArray(mPosLoc);
    mMeshBuffer.bind();
    glVertexAttribPointer(mPosLoc, 2 /* size */, GL_FLOAT, GL_FALSE,
                          2 * sizeof(GLfloat) /* stride */, 0 /* offset */);
    glVertexAttribPointer(mUvLoc, 2 /* size */, GL_FLOAT, GL_FALSE, 0 /* stride */,
                          (GLvoid*)(6 * sizeof(GLfloat)) /* offset */);
    mMeshBuffer.unbind();
    glDrawArrays(GL_TRIANGLES, 0 /* first */, 3 /* count */);

    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    mEngine.checkErrors("Drawing CRT pass");
    return NO_ERROR;
}

} // namespace gl
} // namespace renderengine
} // namespace android
