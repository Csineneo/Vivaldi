# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from gpu_tests.webgl_conformance_expectations import WebGLConformanceExpectations

# See the GpuTestExpectations class for documentation.

class WebGL2ConformanceExpectations(WebGLConformanceExpectations):
  def __init__(self, conformance_path):
    super(WebGL2ConformanceExpectations, self).__init__(conformance_path)

  def SetExpectations(self):
    # ===================================
    # Extension availability expectations
    # ===================================
    # It's expected that not all extensions will be available on all platforms.
    # Having a test listed here is not necessarily a problem.

    self.Fail('WebglExtension.WEBGL_compressed_texture_astc',
        ['win', 'mac', 'linux'])
    self.Fail('WebglExtension.WEBGL_compressed_texture_atc',
        ['win', 'mac', 'linux'])
    self.Fail('WebglExtension.WEBGL_compressed_texture_etc1',
        ['mac', 'linux'])
    self.Fail('WebglExtension.WEBGL_compressed_texture_pvrtc',
        ['win', 'mac', 'linux'])

    # ========================
    # Conformance expectations
    # ========================
    # All platforms.
    self.Skip('deqp/functional/gles3/builtinprecision/*.html', bug=483282)
    self.Skip('deqp/functional/gles3/draw.html', bug=483282)
    self.Skip('deqp/functional/gles3/fbocolorbuffer.html', bug=483282)
    self.Skip('deqp/functional/gles3/fbocompleteness.html', bug=483282)
    self.Skip('deqp/functional/gles3/fbodepthbuffer.html', bug=483282)
    self.Skip('deqp/functional/gles3/fboinvalidate.html', bug=483282)
    self.Skip('deqp/functional/gles3/fbomultisample.html', bug=483282)
    self.Skip('deqp/functional/gles3/fborender.html', bug=483282)
    self.Skip('deqp/functional/gles3/fragmentoutput.html', bug=483282)
    self.Skip('deqp/functional/gles3/framebufferblit.html', bug=483282)
    self.Skip('deqp/functional/gles3/instancedrendering.html', bug=483282)
    self.Skip('deqp/functional/gles3/integerstatequery.html', bug=483282)
    self.Skip('deqp/functional/gles3/lifetime.html', bug=483282)
    self.Skip('deqp/data/gles3/shaders/linkage.html', bug=601821)
    self.Skip('deqp/functional/gles3/multisample.html', bug=483282)
    self.Skip('deqp/functional/gles3/negativebufferapi.html', bug=483282)
    self.Skip('deqp/functional/gles3/negativetextureapi.html', bug=483282)
    self.Skip('deqp/functional/gles3/negativevertexarrayapi.html', bug=483282)
    self.Skip('deqp/functional/gles3/occlusionquery.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderbuiltinvar.html', bug=483282)
    self.Skip('deqp/functional/gles3/shadercommonfunction.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderderivate.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderloop.html', bug=483282)
    self.Skip('deqp/functional/gles3/shadermatrix.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderoperator.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderpackingfunction.html', bug=483282)
    self.Skip('deqp/functional/gles3/shaderstatequery.html', bug=483282)
    self.Skip('deqp/functional/gles3/shadertexturefunction/*.html', bug=483282)
    self.Skip('deqp/functional/gles3/sync.html', bug=483282)
    self.Skip('deqp/functional/gles3/textureformat.html', bug=483282)
    self.Skip('deqp/functional/gles3/textureshadow.html', bug=483282)
    self.Skip('deqp/functional/gles3/texturespecification/*.html', bug=483282)
    self.Skip('deqp/functional/gles3/texturewrap.html', bug=483282)
    self.Skip('deqp/functional/gles3/transformfeedback.html', bug=483282)
    self.Skip('deqp/functional/gles3/uniformapi.html', bug=483282)
    self.Skip('deqp/functional/gles3/uniformbuffers.html', bug=483282)

    self.Fail('deqp/data/gles3/shaders/preprocessor.html', bug=483282)
    self.Flaky('deqp/functional/gles3/shaderindexing.html', bug=483282)

    self.Fail('conformance2/glsl3/forbidden-operators.html', bug=483282)

    self.Fail('conformance2/misc/expando-loss-2.html', bug=483282)
    self.Flaky('conformance2/query/occlusion-query.html', bug=603168)
    self.Fail('conformance2/vertex_arrays/vertex-array-object.html', bug=483282)

    # All platforms when running ANGLE
    self.Skip('conformance2/textures/misc/copy-texture-image.html',
        ['d3d9', 'd3d11', 'opengl'], bug=577144) # crash on debug
    self.Fail('conformance2/textures/misc/tex-unpack-params.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)

    # Avoid a conflict with a Mac expectation by setting
    self.Fail('conformance2/textures/misc/tex-input-validation.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)

    # Failing because the tests are using invalid combinations of source and
    # destination formats, see https://github.com/KhronosGroup/WebGL/issues/1628
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgb565-rgb-unsigned_short_5_6_5.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgb5_a1-rgba-unsigned_short_5_5_5_1.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgba4-rgba-unsigned_byte.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-2d-rgba4-rgba-unsigned_short_4_4_4_4.html',
        ['d3d9', 'd3d11', 'opengl'], bug=483282)

    # Windows only.

    self.Fail('deqp/functional/gles3/vertexarrays.html',
        ['win'], bug=483282)

    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgb565-rgb-unsigned_short_5_6_5.html',
        ['win'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgb5_a1-rgba-unsigned_short_5_5_5_1.html',
        ['win'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgba4-rgba-unsigned_byte.html',
        ['win'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-2d-rgba4-rgba-unsigned_short_4_4_4_4.html',
        ['win'], bug=483282)

    self.Flaky('deqp/functional/gles3/buffercopy.html', ['win'], bug=587601)

    self.Skip('deqp/functional/gles3/readpixel.html', ['win'], bug=483282)
    self.Fail('conformance2/glsl3/array-in-complex-expression.html',
        ['win'], bug=483282)
    self.Skip('conformance2/reading/read-pixels-pack-parameters.html',
        ['win'], bug=483282)
    self.Skip('conformance2/textures/misc/tex-mipmap-levels.html',
        ['win'], bug=483282)
    self.Skip('conformance2/transform_feedback/transform_feedback.html',
        ['win'], bug=483282)
    self.Skip('conformance2/reading/read-pixels-into-pixel-pack-buffer.html',
        ['win'], bug=1266) # angle bug ID
    self.Fail('conformance2/state/gl-object-get-calls.html',
        ['win'], bug=483282)

    # Windows 8 only.

    self.Fail('conformance2/reading/read-pixels-from-fbo-test.html',
        ['win8'], bug=483282)

    self.Fail('conformance2/textures/image_data/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_data/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/svg_image/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/svg_image/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/video/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/video/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_data/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_data/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_video/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_video/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_canvas/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_canvas/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_blob/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_blob/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_bitmap/' +
        'tex-2d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_bitmap/' +
        'tex-2d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)

    self.Fail('conformance2/textures/video/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/video/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image_data/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image_data/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/svg_image/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/svg_image/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image_bitmap_from_image_data/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_data/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_video/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_video/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/canvas/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/canvas/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/webgl_canvas/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=560555)
    self.Fail('conformance2/textures/image_bitmap_from_canvas/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_canvas/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_blob/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_blob/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_bitmap/' +
        'tex-3d-rgb565-rgb-unsigned_byte.html',
        ['win8'], bug=483282)
    self.Fail('conformance2/textures/image_bitmap_from_image_bitmap/' +
        'tex-3d-rgb5_a1-rgba-unsigned_byte.html',
        ['win8'], bug=483282)

    # Windows Debug. Causing assertions in the GPU process which raise
    # a dialog box, so have to skip them rather than mark them as
    # failing.
    self.Skip('conformance2/textures/canvas/' +
        'tex-2d-rgba8-rgba-unsigned_byte.html',
        ['win', 'debug'], bug=542901)

    # This test actually passes from time to time.
    self.Fail('deqp/functional/gles3/texturefiltering/cube_sizes_00.html',
        ['win'], bug=606021)
    # More intermittently flaky.
    self.Fail('deqp/functional/gles3/texturefiltering/cube_sizes_03.html',
        ['win'], bug=606021)

    # Win / Release.
    self.Fail(
        'deqp/functional/gles3/texturefiltering/cube_combinations_01.html',
        ['win', 'release'], bug=607393)
    self.Fail(
        'deqp/functional/gles3/texturefiltering/cube_combinations_05.html',
        ['win', 'release'], bug=607393)

    # Win / AMD
    # It's unfortunate that this suppression needs to be so broad, but
    # basically any test that uses readPixels is potentially flaky, and
    # it's infeasible to suppress individual failures one by one.
    self.Flaky('conformance2/*', ['win', ('amd', 0x6779)], bug=491419)
    self.Flaky('deqp/*', ['win', ('amd', 0x6779)], bug=491419)

    # Win / Intel
    self.Fail('conformance2/buffers/uniform-buffers.html',
        ['win', 'intel'], bug=483282)
    self.Fail('deqp/functional/gles3/shaderstruct.html',
        ['win', 'intel'], bug=483282)

    # Mac only.
    self.Fail('deqp/functional/gles3/texturefiltering/cube_formats_*',
        ['mac'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/cube_sizes_*',
        ['mac'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/cube_combinations_*',
        ['mac'], bug=606074)

    self.Fail('deqp/data/gles3/shaders/qualification_order.html',
        ['mac'], bug=483282)
    self.Fail('deqp/data/gles3/shaders/scoping.html',
        ['mac'], bug=483282)
    self.Fail('deqp/functional/gles3/pixelbufferobject.html',
        ['mac'], bug=483282)
    self.Fail('deqp/functional/gles3/texturestatequery.html',
        ['mac'], bug=483282)
    self.Fail('deqp/functional/gles3/negativeshaderapi.html',
        ['mac'], bug=483282)
    self.Fail('deqp/functional/gles3/vertexarrays.html',
        ['mac'], bug=483282)

    self.Fail('conformance2/reading/read-pixels-from-fbo-test.html',
        ['mac'], bug=483282)
    self.Fail('conformance2/textures/misc/compressed-tex-image.html',
        ['mac'], bug=565438)
    self.Fail('conformance2/textures/misc/tex-new-formats.html',
        ['mac'], bug=483282)
    self.Fail('conformance2/textures/misc/tex-storage-compressed-formats.html',
        ['mac'], bug=295792)
    self.Fail('conformance2/renderbuffers/framebuffer-test.html',
        ['mac'], bug=483282)
    self.Fail('conformance2/rendering/framebuffer-completeness-unaffected.html',
        ['mac'], bug=604053)

    # Mac Retina NVIDIA
    self.Fail('conformance2/rendering/draw-buffers.html',
        ['mac', ('nvidia', 0xfe9)], bug=483282)
    self.Fail('conformance2/textures/misc/tex-input-validation.html',
        ['mac', ('nvidia', 0xfe9), 'no_angle'], bug=483282)
    self.Fail('conformance2/textures/misc/tex-mipmap-levels.html',
        ['mac', ('nvidia', 0xfe9)], bug=483282)
    self.Fail('deqp/functional/gles3/shaderstruct.html',
        ['mac', ('nvidia', 0xfe9)], bug=483282)
    self.Fail('deqp/functional/gles3/shaderswitch.html',
        ['mac', ('nvidia', 0xfe9)], bug=483282)

    # Mac AMD
    self.Fail('deqp/functional/gles3/clipping.html',
        ['mac', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/primitiverestart.html',
        ['mac', 'amd'], bug=598930)

    # Mac Intel
    self.Fail('conformance2/textures/misc/tex-unpack-params.html',
        ['mac', 'intel', 'no_angle'], bug=483282)
    self.Fail('deqp/functional/gles3/texturefiltering/2d_combinations_01.html',
        ['mac', 'intel'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/' +
        '2d_array_combinations_01.html',
        ['mac', 'intel'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/3d_combinations_06.html',
        ['mac', 'intel'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/3d_combinations_07.html',
        ['mac', 'intel'], bug=606074)
    self.Fail('deqp/functional/gles3/texturefiltering/3d_combinations_08.html',
        ['mac', 'intel'], bug=606074)

    # Linux only.
    self.Fail('deqp/data/gles3/shaders/functions.html',
        ['linux'], bug=483282)
    self.Fail('conformance2/glsl3/vector-dynamic-indexing.html',
        ['linux'], bug=483282)
    self.Fail('conformance2/reading/read-pixels-from-fbo-test.html',
        ['linux'], bug=483282)
    self.Fail('conformance2/rendering/draw-buffers.html',
        ['linux'], bug=483282)
    self.Fail('deqp/functional/gles3/texturefiltering/cube_sizes_00.html',
        ['linux'], bug=606021)
    self.Flaky('deqp/functional/gles3/texturefiltering/cube_sizes_03.html',
        ['linux'], bug=606021)

    self.Skip('deqp/data/gles3/shaders/qualification_order.html',
        ['linux', 'amd', 'intel'], bug=483282)
    self.Fail('deqp/functional/gles3/clipping.html',
        ['linux', 'amd', 'intel'], bug=483282)

    # Linux NVIDIA only.
    self.Fail('conformance2/glsl3/array-complex-indexing.html',
        ['linux', 'nvidia'], bug=606498)
    self.Fail('deqp/functional/gles3/vertexarrays.html',
        ['linux', 'nvidia', 'debug'], bug=483282)

    # Linux NVIDIA with ANGLE only
    self.Fail('deqp/functional/gles3/buffercopy.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)
    self.Fail('deqp/functional/gles3/bufferobjectquery.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)
    self.Fail('conformance2/buffers/bound-buffer-size-change-test.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)
    self.Fail('conformance2/buffers/buffer-copying-contents.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)
    self.Fail('conformance2/reading/read-pixels-pack-parameters.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)
    self.Fail('conformance2/transform_feedback/transform_feedback.html',
        ['linux', 'nvidia', 'opengl'], bug=483282)

    # Linux Intel with ANGLE only
    self.Fail('deqp/functional/gles3/pixelbufferobject.html',
        ['linux', 'intel', 'opengl'], bug=483282)

    # The Mesa Intel driver has a scoping bug, see
    # https://bugs.freedesktop.org/show_bug.cgi?id=95184
    self.Fail('deqp/data/gles3/shaders/scoping.html',
        ['linux', 'intel'], bug=610800)

    # Linux AMD only.
    # It looks like AMD shader compiler rejects many valid ES3 semantics.
    self.Fail('deqp/data/gles3/shaders/conversions.html',
        ['linux', 'amd'], bug=483282)
    self.Skip('deqp/data/gles3/shaders/arrays.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/internalformatquery.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/texturestatequery.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/vertexarrays.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/buffercopy.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/samplerobject.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/shaderprecision.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('deqp/functional/gles3/texturefiltering/3d*',
        ['linux', 'amd'], bug=606114)

    self.Fail('conformance2/misc/uninitialized-test-2.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('conformance2/reading/read-pixels-pack-parameters.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('conformance2/reading/read-pixels-into-pixel-pack-buffer.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('conformance2/renderbuffers/framebuffer-texture-layer.html',
        ['linux', 'amd'], bug=295792)
    self.Fail('conformance2/textures/misc/tex-mipmap-levels.html',
        ['linux', 'amd'], bug=483282)
    self.Fail('conformance2/textures/misc/tex-unpack-params.html',
        ['linux', 'amd', 'no_angle'], bug=483282)

    # Conflicting expectations to test that the
    # "Expectations Have No collisions" unittest works.
    # page_name = 'conformance/glsl/constructors/glsl-construct-ivec4.html'

    # Conflict when all conditions match
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug', 'opengl'])
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug', 'opengl'])

    # Conflict when all conditions match (and different sets)
    # self.Fail(page_name,
    #     ['linux', 'win', ('nvidia', 0x1), 'debug', 'opengl'])
    # self.Fail(page_name,
    #     ['linux', 'mac', ('nvidia', 0x1), 'amd', 'debug', 'opengl'])

    # Conflict with one aspect not specified
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug', 'opengl'])

    # Conflict with one aspect not specified (in both conditions)
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])

    # Conflict even if the GPU is specified in a device ID
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', 'nvidia', 'debug'])

    # Test there are no conflicts between two different devices
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x2), 'debug'])

    # Test there are no conflicts between two devices with different vendors
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', ('amd', 0x1), 'debug'])

    # Conflicts if there is a device and nothing specified for the other's
    # GPU vendors
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug'])
    # self.Fail(page_name,
    #     ['linux', 'debug'])

    # Test no conflicts happen when only one aspect differs
    # self.Fail(page_name,
    #     ['linux', ('nvidia', 0x1), 'debug', 'opengl'])
    # self.Fail(page_name,
    #     ['win', ('nvidia', 0x1), 'debug', 'opengl'])

    # Conflicts if between a generic os condition and a specific version
    # self.Fail(page_name,
    #     ['xp', ('nvidia', 0x1), 'debug', 'opengl'])
    # self.Fail(page_name,
    #     ['win', ('nvidia', 0x1), 'debug', 'opengl'])
