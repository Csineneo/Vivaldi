# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
#    {
#      'target_name': 'actions_controller',
#      'dependencies': [
#        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
#        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
#        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:ui',
#        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:context_menu_handler',
#        'actions_model',
#      ],
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'actions_model',
#      'dependencies': [
#        '../../background/js/compiled_resources2.gyp:drive_sync_handler',
#        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
#        '<(EXTERNS_GYP):file_manager_private',
#        'folder_shortcuts_data_model',
#        'metadata/compiled_resources2.gyp:metadata_model',
#      ],
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'app_state_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'column_visibility_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'constants',
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'dialog_action_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'dialog_type',
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'directory_contents',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'directory_model',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'directory_tree_naming_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'elements_importer',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'empty_folder_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'file_list_model',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:file_type',
        '../../common/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:array_data_model',
        'metadata/compiled_resources2.gyp:metadata_model',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'file_manager',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'file_manager_commands',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'file_selection',
#      'dependencies': [
#        '../../common/js/compiled_resources2.gyp:util',
#        '../../common/js/compiled_resources2.gyp:volume_manager_common',
#        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
#        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
#      ],
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'file_tasks',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'file_transfer_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'file_watcher',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'folder_shortcuts_data_model',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:async_util',
        '../../common/js/compiled_resources2.gyp:metrics',
        '../../common/js/compiled_resources2.gyp:util',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        '<(EXTERNS_GYP):chrome_extensions',
        'volume_manager_wrapper',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'gear_menu_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'import_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'launch_param',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        'dialog_type',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'list_thumbnail_loader',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'main',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'main_scripts',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'main_window_component',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'metadata_box_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'metadata_update_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'metrics_start',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'mouse_inactivity_watcher',
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'naming_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'navigation_list_model',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:volume_info',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:array_data_model',
        'folder_shortcuts_data_model',
        'volume_manager_wrapper',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'progress_center_item_group',
      'dependencies': [
        '../../common/js/compiled_resources2.gyp:progress_center_common',
        '../../common/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'providers_model',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):file_manager_private',
        'volume_manager_wrapper',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'quick_view_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'quick_view_model',
      'dependencies': [
        '../../../../../ui/webui/resources/js/compiled_resources2.gyp:cr',
        '../../../../../ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'quick_view_uma',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'scan_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'search_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'share_client',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:entry_location',
        '../../../externs/compiled_resources2.gyp:gallery_foreground',
        '../../../externs/compiled_resources2.gyp:volume_info',
        '../../../externs/compiled_resources2.gyp:volume_info_list',
        '../../../externs/compiled_resources2.gyp:volume_manager',
        '../../../externs/compiled_resources2.gyp:webview_tag',
        '../../common/js/compiled_resources2.gyp:volume_manager_common',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'sort_menu_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'spinner_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
#    {
#      'target_name': 'task_controller',
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'thumbnail_loader',
      'dependencies': [
        '../../../image_loader/compiled_resources2.gyp:image_loader_client',
        '../../common/js/compiled_resources2.gyp:file_type',
        '../../common/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
#    {
#      'target_name': 'toolbar_controller',
#      'dependencies': [
#        '../../common/js/compiled_resources2.gyp:util',
#        'file_selection',
#        'ui/compiled_resources2.gyp:list_container',
#        'ui/compiled_resources2.gyp:location_line',
#      ],
#      'includes': ['../../../compile_js2.gypi'],
#    },
    {
      'target_name': 'volume_manager_wrapper',
      'dependencies': [
        '../../background/js/compiled_resources2.gyp:volume_manager_factory',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'web_store_utils',
      'dependencies': [
        'constants',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
 ],
}
