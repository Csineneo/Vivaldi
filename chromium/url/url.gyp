# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'includes': [
    'url_srcs.gypi',
  ],
  'targets': [
    {
      # Note, this target_name cannot be 'url', because that will generate
      # 'url.dll' for a Windows component build, and that will confuse Windows,
      # which has a system DLL with the same name.
      'target_name': 'url_lib',
      'type': '<(component)',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '../third_party/icu/icu.gyp:icui18n',
        '../third_party/icu/icu.gyp:icuuc',
      ],
      'sources': [
        '<@(gurl_sources)',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '..',
        ],
      },
      'defines': [
        'URL_IMPLEMENTATION',
      ],
      # TODO(jschuh): crbug.com/167187 fix size_t to int truncations.
      'msvs_disabled_warnings': [4267, ],
    },
    {
      'target_name': 'url_unittests',
      'type': 'executable',
      'dependencies': [
        '../base/base.gyp:test_support_base',
        '../mojo/mojo_edk.gyp:mojo_common_test_support',
        '../testing/gtest.gyp:gtest',
        '../third_party/icu/icu.gyp:icuuc',
        'url_test_mojom',
        'url_lib',
      ],
      'sources': [
        'gurl_unittest.cc',
        'origin_unittest.cc',
        'run_all_unittests.cc',
        'scheme_host_port_unittest.cc',
        'url_canon_icu_unittest.cc',
        'url_canon_unittest.cc',
        'url_parse_unittest.cc',
        'url_test_utils.h',
        'url_util_unittest.cc',
      ],
      'conditions': [
        ['OS!="ios"', {
          'sources': [
            'mojo/url_gurl_struct_traits_unittest.cc',
          ],
        }],
      ],
      # TODO(jschuh): crbug.com/167187 fix size_t to int truncations.
      'msvs_disabled_warnings': [4267, ],
    },
    {
      'target_name': 'url_interfaces_mojom',
      'type': 'none',
      'variables': {
        'mojom_extra_generator_args': [
          '--typemap', '<(DEPTH)/url/mojo/origin.typemap',
          '--typemap', '<(DEPTH)/url/mojo/gurl.typemap',
        ],
        'mojom_files': [
          'mojo/origin.mojom',
          'mojo/url.mojom',
        ],
      },
      'includes': [ '../mojo/mojom_bindings_generator_explicit.gypi' ],
    },
    {
      'target_name': 'url_mojom',
      'type': 'static_library',
      'export_dependent_settings': [
        '../mojo/mojo_public.gyp:mojo_cpp_bindings',
      ],
      'dependencies': [
        '../mojo/mojo_public.gyp:mojo_cpp_bindings',
        'url_interfaces_mojom',
      ],
    },
    {
      'target_name': 'url_test_interfaces_mojom',
      'type': 'none',
      'variables': {
        'mojom_extra_generator_args': [
          '--typemap', '<(DEPTH)/url/mojo/gurl.typemap',
          '--typemap', '<(DEPTH)/url/mojo/origin.typemap',
        ],
        'mojom_files': [
          'mojo/url_test.mojom',
        ],
      },
      'includes': [ '../mojo/mojom_bindings_generator_explicit.gypi' ],
      'dependencies': [
        '../mojo/mojo_public.gyp:mojo_cpp_bindings',
        'url_interfaces_mojom',
      ],
    },
    {
      'target_name': 'url_test_mojom',
      'type': 'static_library',
      'export_dependent_settings': [
        '../mojo/mojo_public.gyp:mojo_cpp_bindings',
      ],
      'dependencies': [
        '../mojo/mojo_public.gyp:mojo_cpp_bindings',
        'url_mojom',
        'url_test_interfaces_mojom',
      ],
    },
  ],
  'conditions': [
    ['OS=="android"', {
      'targets': [
        {
          'target_name': 'url_jni_headers',
          'type': 'none',
          'sources': [
            'android/java/src/org/chromium/url/IDNStringUtil.java'
          ],
          'variables': {
            'jni_gen_package': 'url',
          },
          'includes': [ '../build/jni_generator.gypi' ],
        },
        {
          'target_name': 'url_java',
          'type': 'none',
          'variables': {
            'java_in_dir': '../url/android/java',
          },
          'dependencies': [
            '../base/base.gyp:base',
          ],
          'includes': [ '../build/java.gypi' ],
        },
        {
          # Same as url_lib but using ICU alternatives on Android.
          'target_name': 'url_lib_use_icu_alternatives_on_android',
          'type': '<(component)',
          'dependencies': [
            '../base/base.gyp:base',
            '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
            'url_java',
            'url_jni_headers',
          ],
          'sources': [
            '<@(gurl_sources)',
            'url_canon_icu_alternatives_android.cc',
            'url_canon_icu_alternatives_android.h',
          ],
          'sources!': [
            'url_canon_icu.cc',
            'url_canon_icu.h',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              '..',
            ],
          },
          'defines': [
            'URL_IMPLEMENTATION',
            'USE_ICU_ALTERNATIVES_ON_ANDROID=1',
          ],
        },
      ],
    }],
    ['test_isolation_mode != "noop"', {
      'targets': [
        {
          'target_name': 'url_unittests_run',
          'type': 'none',
          'dependencies': [
            'url_unittests',
          ],
          'includes': [
            '../build/isolate.gypi',
          ],
          'sources': [
            'url_unittests.isolate',
          ],
        },
      ],
    }],
  ],
}
