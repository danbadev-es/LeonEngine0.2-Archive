#include "FOpenGLUniformBuffer.hpp"
#include "RHI/FRenderer.hpp"

namespace Leon {

    FOpenGLUniformBuffer::FOpenGLUniformBuffer(uint32_t InSize, uint32_t InBinding)
        : Binding(InBinding), AllocatedBytes(InSize) {
        glCreateBuffers(1, &RendererID);
        glNamedBufferData(RendererID, InSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, InBinding, RendererID);

        FRenderer::OnGPUAlloc(AllocatedBytes, EGPUMemoryCategory::UniformBuffer);
    }

    FOpenGLUniformBuffer::~FOpenGLUniformBuffer() {
        if (RendererID) {
            FRenderer::OnGPUFree(AllocatedBytes, EGPUMemoryCategory::UniformBuffer);
            glDeleteBuffers(1, &RendererID);
        }
    }

    void FOpenGLUniformBuffer::SetData(const void* InData, uint32_t InSize, uint32_t InOffset) {
        glNamedBufferSubData(RendererID, InOffset, InSize, InData);
        // Re-attach the buffer to its binding point after every write. A buffer rewritten several
        // times per frame (one bone palette per skinned mesh) was otherwise read one update late by
        // the Intel UHD driver: each mesh was drawn with the palette of the mesh before it.
        glBindBufferBase(GL_UNIFORM_BUFFER, Binding, RendererID);
    }

} // namespace Leon
