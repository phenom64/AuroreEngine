# Textured meshes and upload synchronization

`gui_renderer::draw_textured_triangles` draws a triangle list with arbitrary UVs,
vertex colours and perspective interpolation. Existing image, rectangle and text
calls keep their default coordinate and colour behavior.

```cpp
imageRenderer.setAllocator(allocator); // once, before prepare/drawing
imageRenderer.prepare(frameCount);

std::array<auroreengine::image_renderer::textured_vertex, 3> vertices{{
    {{0.25f, 0.25f, 0.5f, 1.0f}, {0.0f, 0.0f}, {1, 1, 1, 1}},
    {{0.75f, 0.25f, 0.5f, 1.0f}, {1.0f, 0.0f}, {1, 1, 1, 1}},
    {{0.25f, 0.75f, 0.5f, 1.0f}, {0.0f, 1.0f}, {1, 1, 1, 1}},
}};
gui.draw_textured_triangles(texture, vertices, false, false);
```

Positions contain normalized screen x/y, Vulkan normalized depth z and the
original positive clip-space w. The vertex shader reconstructs clip coordinates
as `((xy * 2 - 1) * w, z * w, w)`, retaining perspective-correct UV/color
interpolation for CPU-projected meshes. Each three input vertices form one
triangle. Nonfinite triangles and nonpositive w values are omitted; callers
should clip geometry intersecting the camera plane before submitting it.

The two optional flags enable source-alpha additive blending and depth test/write
with `LessOrEqual`. Depth requires an appropriate depth attachment in the render
pass; transparent geometry generally requires back-to-front sorting with depth
writes disabled. The ordinary mode uses straight alpha blending. Both modes
respect the GUI colour stack and current viewport/scissor.

With a VMA allocator, meshes use persistently mapped, retained vertex chunks per
frame slot. A mesh normally uses one draw; crossing a retained chunk boundary
splits the draw. Chunks remain alive until `prepare()` or renderer destruction,
and counters reset at `finish(frame)`. The caller must wait for a frame slot's GPU
fence before reusing it, as for the other renderers. Capacity is bounded to
262144 vertices per slot; exceeding it produces a warning and omits excess
geometry. `geometryStatistics(frame)` reports the last finished slot's actual
mesh draw calls, triangle count, upload bytes and resident vertex capacity.

Allocatorless users retain a small-triangle fallback using push constants and
one draw per triangle. This avoids breaking old constructors but should not be
used for large models. No native frame-rate or memory claim follows from either
API; profile the intended workload and hardware.

Theme typography is per GUI context: `text_color` defaults to white and
`text_scale` to 1. Both `draw_text` and `measure_text` apply the scale consistently,
including centered labels. A new context or `reset()` restores neutral defaults.
Font renderers retain a bounded cache of shaped runs shared by measurement and
repeated draws; `textCacheStatistics()` reports its counters.

## Queue ownership

Resource loading and phase rendering can share a Vulkan queue. Use
`phase::submit_graphics(info, fence)` in phase implementations instead of calling
`graphicsQueue.submit` directly. The window and loader serialize queue operations
through the same mutex, while leaving the phase callback free to wait for uploads.
`window::waitIdle()` drains loader jobs before waiting for GPU work and is suitable
before replacing renderer-owned textures. Calling the loader drain while holding
the queue mutex would prevent workers from finishing their submissions.

## Rendering smoke

Configure `-DAUROREENGINE_BUILD_RENDER_TESTS=ON` and explicitly build
`aurore_textured_triangles_smoke`. The target is excluded from the default build
and requires a functional Vulkan driver. Run it with a writable output directory:

```sh
aurore_textured_triangles_smoke /tmp/aurore-triangle-output
```

It renders 8192 checkerboard triangles plus two occluded and two additive triangles in three mesh
draws, checks upload statistics and output pixels, and drains a pending upload from inside a phase
callback. The headless output permits pixel verification independently of a
desktop window system. This is a rendering correctness smoke, not a performance
benchmark for a physical device.
