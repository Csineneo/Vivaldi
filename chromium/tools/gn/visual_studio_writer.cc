// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/visual_studio_writer.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "tools/gn/builder.h"
#include "tools/gn/commands.h"
#include "tools/gn/config.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/label_pattern.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/path_output.h"
#include "tools/gn/source_file_type.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/target.h"
#include "tools/gn/variables.h"
#include "tools/gn/visual_studio_utils.h"
#include "tools/gn/xml_element_writer.h"

#if defined(OS_WIN)
#include "base/win/registry.h"
#endif

namespace {

struct SemicolonSeparatedWriter {
  void operator()(const std::string& value, std::ostream& out) const {
    out << value + ';';
  }
};

struct IncludeDirWriter {
  explicit IncludeDirWriter(PathOutput& path_output)
      : path_output_(path_output) {}
  ~IncludeDirWriter() = default;

  void operator()(const SourceDir& dir, std::ostream& out) const {
    path_output_.WriteDir(out, dir, PathOutput::DIR_NO_LAST_SLASH);
    out << ";";
  }

  PathOutput& path_output_;
};

struct SourceFileWriter {
  SourceFileWriter(PathOutput& path_output, const SourceFile& source_file)
      : path_output_(path_output), source_file_(source_file) {}
  ~SourceFileWriter() = default;

  void operator()(std::ostream& out) const {
    path_output_.WriteFile(out, source_file_);
  }

  PathOutput& path_output_;
  const SourceFile& source_file_;
};

const char kToolsetVersionVs2013[] = "v120";               // Visual Studio 2013
const char kToolsetVersionVs2015[] = "v140";               // Visual Studio 2015
const char kProjectVersionVs2013[] = "12.0";               // Visual Studio 2013
const char kProjectVersionVs2015[] = "14.0";               // Visual Studio 2015
const char kVersionStringVs2013[] = "Visual Studio 2013";  // Visual Studio 2013
const char kVersionStringVs2015[] = "Visual Studio 2015";  // Visual Studio 2015
const char kWindowsKitsVersion[] = "10";                   // Windows 10 SDK
const char kWindowsKitsIncludeVersion[] = "10.0.10586.0";  // Windows 10 SDK

const char kGuidTypeProject[] = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}";
const char kGuidTypeFolder[] = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";
const char kGuidSeedProject[] = "project";
const char kGuidSeedFolder[] = "folder";
const char kGuidSeedFilter[] = "filter";

const char kConfigurationName[] = "GN";

std::string GetWindowsKitsIncludeDirs() {
  std::string kits_path;

#if defined(OS_WIN)
  const base::char16* const subkeys[] = {
      L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
      L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows Kits\\Installed Roots"};

  base::string16 value_name =
      base::ASCIIToUTF16("KitsRoot") + base::ASCIIToUTF16(kWindowsKitsVersion);

  for (const base::char16* subkey : subkeys) {
    base::win::RegKey key(HKEY_LOCAL_MACHINE, subkey, KEY_READ);
    base::string16 value;
    if (key.ReadValue(value_name.c_str(), &value) == ERROR_SUCCESS) {
      kits_path = base::UTF16ToUTF8(value);
      break;
    }
  }
#endif  // OS_WIN

  if (kits_path.empty()) {
    kits_path = std::string("C:\\Program Files (x86)\\Windows Kits\\") +
                kWindowsKitsVersion + "\\";
  }

  return kits_path + "Include\\" + kWindowsKitsIncludeVersion + "\\shared;" +
         kits_path + "Include\\" + kWindowsKitsIncludeVersion + "\\um;" +
         kits_path + "Include\\" + kWindowsKitsIncludeVersion + "\\winrt;";
}

std::string GetConfigurationType(const Target* target, Err* err) {
  switch (target->output_type()) {
    case Target::EXECUTABLE:
      return "Application";
    case Target::SHARED_LIBRARY:
    case Target::LOADABLE_MODULE:
      return "DynamicLibrary";
    case Target::STATIC_LIBRARY:
    case Target::SOURCE_SET:
      return "StaticLibrary";
    case Target::GROUP:
      return "Utility";

    default:
      *err = Err(Location(),
                 "Visual Studio doesn't support '" + target->label().name() +
                     "' target output type: " +
                     Target::GetStringForOutputType(target->output_type()));
      return std::string();
  }
}

void ParseCompilerOptions(const std::vector<std::string>& cflags,
                          CompilerOptions* options) {
  for (const std::string& flag : cflags)
    ParseCompilerOption(flag, options);
}

void ParseCompilerOptions(const Target* target, CompilerOptions* options) {
  for (ConfigValuesIterator iter(target); !iter.done(); iter.Next()) {
    ParseCompilerOptions(iter.cur().cflags(), options);
    ParseCompilerOptions(iter.cur().cflags_c(), options);
    ParseCompilerOptions(iter.cur().cflags_cc(), options);
  }
}

// Returns a string piece pointing into the input string identifying the parent
// directory path, excluding the last slash. Note that the input pointer must
// outlive the output.
base::StringPiece FindParentDir(const std::string* path) {
  DCHECK(path && !path->empty());
  for (int i = static_cast<int>(path->size()) - 2; i >= 0; --i) {
    if (IsSlash((*path)[i]))
      return base::StringPiece(path->data(), i);
  }
  return base::StringPiece();
}

}  // namespace

VisualStudioWriter::SolutionEntry::SolutionEntry(const std::string& _name,
                                                 const std::string& _path,
                                                 const std::string& _guid)
    : name(_name), path(_path), guid(_guid), parent_folder(nullptr) {}

VisualStudioWriter::SolutionEntry::~SolutionEntry() = default;

VisualStudioWriter::SolutionProject::SolutionProject(
    const std::string& _name,
    const std::string& _path,
    const std::string& _guid,
    const std::string& _label_dir_path,
    const std::string& _config_platform)
    : SolutionEntry(_name, _path, _guid),
      label_dir_path(_label_dir_path),
      config_platform(_config_platform) {}

VisualStudioWriter::SolutionProject::~SolutionProject() = default;

VisualStudioWriter::VisualStudioWriter(const BuildSettings* build_settings,
                                       const char* config_platform,
                                       Version version)
    : build_settings_(build_settings),
      config_platform_(config_platform),
      ninja_path_output_(build_settings->build_dir(),
                         build_settings->root_path_utf8(),
                         EscapingMode::ESCAPE_NINJA_COMMAND) {
  switch (version) {
    case Version::Vs2013:
      project_version_ = kProjectVersionVs2013;
      toolset_version_ = kToolsetVersionVs2013;
      version_string_ = kVersionStringVs2013;
      break;
    case Version::Vs2015:
      project_version_ = kProjectVersionVs2015;
      toolset_version_ = kToolsetVersionVs2015;
      version_string_ = kVersionStringVs2015;
      break;
    default:
      NOTREACHED() << "Not a valid Visual Studio Version: " << version;
  }

  windows_kits_include_dirs_ = GetWindowsKitsIncludeDirs();
}

VisualStudioWriter::~VisualStudioWriter() {
  STLDeleteContainerPointers(projects_.begin(), projects_.end());
  STLDeleteContainerPointers(folders_.begin(), folders_.end());
}

// static
bool VisualStudioWriter::RunAndWriteFiles(const BuildSettings* build_settings,
                                          Builder* builder,
                                          Version version,
                                          const std::string& sln_name,
                                          const std::string& dir_filters,
                                          Err* err) {
  std::vector<const Target*> targets;
  if (dir_filters.empty()) {
    targets = builder->GetAllResolvedTargets();
  } else {
    std::vector<std::string> tokens = base::SplitString(
        dir_filters, ";", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    SourceDir root_dir =
        SourceDirForCurrentDirectory(build_settings->root_path());

    std::vector<LabelPattern> filters;
    for (const std::string& token : tokens) {
      LabelPattern pattern =
          LabelPattern::GetPattern(root_dir, Value(nullptr, token), err);
      if (err->has_error())
        return false;
      filters.push_back(pattern);
    }

    commands::FilterTargetsByPatterns(builder->GetAllResolvedTargets(), filters,
                                      &targets);
  }

  const char* config_platform = "Win32";

  // Assume the "target_cpu" variable does not change between different
  // toolchains.
  if (!targets.empty()) {
    const Scope* scope = targets.front()->settings()->base_config();
    const Value* target_cpu_value = scope->GetValue(variables::kTargetCpu);
    if (target_cpu_value != nullptr &&
        target_cpu_value->string_value() == "x64")
      config_platform = "x64";
  }

  VisualStudioWriter writer(build_settings, config_platform, version);
  writer.projects_.reserve(targets.size());
  writer.folders_.reserve(targets.size());

  for (const Target* target : targets) {
    // Skip actions and bundle targets.
    if (target->output_type() == Target::COPY_FILES ||
        target->output_type() == Target::ACTION ||
        target->output_type() == Target::ACTION_FOREACH ||
        target->output_type() == Target::BUNDLE_DATA) {
      continue;
    }

    if (!writer.WriteProjectFiles(target, err))
      return false;
  }

  if (writer.projects_.empty()) {
    *err = Err(Location(), "No Visual Studio projects generated.");
    return false;
  }

  // Sort projects so they appear always in the same order in solution file.
  // Otherwise solution file is rewritten and reloaded by Visual Studio.
  std::sort(writer.projects_.begin(), writer.projects_.end(),
            [](const SolutionEntry* a, const SolutionEntry* b) {
              return a->path < b->path;
            });

  writer.ResolveSolutionFolders();
  return writer.WriteSolutionFile(sln_name, err);
}

bool VisualStudioWriter::WriteProjectFiles(const Target* target, Err* err) {
  std::string project_name = target->label().name();
  const char* project_config_platform = config_platform_;
  if (!target->settings()->is_default()) {
    project_name += "_" + target->toolchain()->label().name();
    const Value* value =
        target->settings()->base_config()->GetValue(variables::kCurrentCpu);
    if (value != nullptr && value->string_value() == "x64")
      project_config_platform = "x64";
    else
      project_config_platform = "Win32";
  }

  SourceFile target_file = GetTargetOutputDir(target).ResolveRelativeFile(
      Value(nullptr, project_name + ".vcxproj"), err);
  if (target_file.is_null())
    return false;

  base::FilePath vcxproj_path = build_settings_->GetFullPath(target_file);
  std::string vcxproj_path_str = FilePathToUTF8(vcxproj_path);

  projects_.push_back(new SolutionProject(
      project_name, vcxproj_path_str,
      MakeGuid(vcxproj_path_str, kGuidSeedProject),
      FilePathToUTF8(build_settings_->GetFullPath(target->label().dir())),
      project_config_platform));

  std::stringstream vcxproj_string_out;
  if (!WriteProjectFileContents(vcxproj_string_out, *projects_.back(), target,
                                err)) {
    projects_.pop_back();
    return false;
  }

  // Only write the content to the file if it's different. That is
  // both a performance optimization and more importantly, prevents
  // Visual Studio from reloading the projects.
  if (!WriteFileIfChanged(vcxproj_path, vcxproj_string_out.str(), err))
    return false;

  base::FilePath filters_path = UTF8ToFilePath(vcxproj_path_str + ".filters");
  std::stringstream filters_string_out;
  WriteFiltersFileContents(filters_string_out, target);
  return WriteFileIfChanged(filters_path, filters_string_out.str(), err);
}

bool VisualStudioWriter::WriteProjectFileContents(
    std::ostream& out,
    const SolutionProject& solution_project,
    const Target* target,
    Err* err) {
  PathOutput path_output(GetTargetOutputDir(target),
                         build_settings_->root_path_utf8(),
                         EscapingMode::ESCAPE_NONE);

  out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
  XmlElementWriter project(
      out, "Project",
      XmlAttributes("DefaultTargets", "Build")
          .add("ToolsVersion", project_version_)
          .add("xmlns", "http://schemas.microsoft.com/developer/msbuild/2003"));

  {
    std::unique_ptr<XmlElementWriter> configurations = project.SubElement(
        "ItemGroup", XmlAttributes("Label", "ProjectConfigurations"));
    std::unique_ptr<XmlElementWriter> project_config =
        configurations->SubElement(
            "ProjectConfiguration",
            XmlAttributes("Include", std::string(kConfigurationName) + '|' +
                                         solution_project.config_platform));
    project_config->SubElement("Configuration")->Text(kConfigurationName);
    project_config->SubElement("Platform")
        ->Text(solution_project.config_platform);
  }

  {
    std::unique_ptr<XmlElementWriter> globals =
        project.SubElement("PropertyGroup", XmlAttributes("Label", "Globals"));
    globals->SubElement("ProjectGuid")->Text(solution_project.guid);
    globals->SubElement("Keyword")->Text("Win32Proj");
    globals->SubElement("RootNamespace")->Text(target->label().name());
    globals->SubElement("IgnoreWarnCompileDuplicatedFilename")->Text("true");
    globals->SubElement("PreferredToolArchitecture")->Text("x64");
  }

  project.SubElement(
      "Import", XmlAttributes("Project",
                              "$(VCTargetsPath)\\Microsoft.Cpp.Default.props"));

  {
    std::unique_ptr<XmlElementWriter> configuration = project.SubElement(
        "PropertyGroup", XmlAttributes("Label", "Configuration"));
    configuration->SubElement("CharacterSet")->Text("Unicode");
    std::string configuration_type = GetConfigurationType(target, err);
    if (configuration_type.empty())
      return false;
    configuration->SubElement("ConfigurationType")->Text(configuration_type);
  }

  {
    std::unique_ptr<XmlElementWriter> locals =
        project.SubElement("PropertyGroup", XmlAttributes("Label", "Locals"));
    locals->SubElement("PlatformToolset")->Text(toolset_version_);
  }

  project.SubElement(
      "Import",
      XmlAttributes("Project", "$(VCTargetsPath)\\Microsoft.Cpp.props"));
  project.SubElement(
      "Import",
      XmlAttributes("Project",
                    "$(VCTargetsPath)\\BuildCustomizations\\masm.props"));
  project.SubElement("ImportGroup",
                     XmlAttributes("Label", "ExtensionSettings"));

  {
    std::unique_ptr<XmlElementWriter> property_sheets = project.SubElement(
        "ImportGroup", XmlAttributes("Label", "PropertySheets"));
    property_sheets->SubElement(
        "Import",
        XmlAttributes(
            "Condition",
            "exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')")
            .add("Label", "LocalAppDataPlatform")
            .add("Project",
                 "$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props"));
  }

  project.SubElement("PropertyGroup", XmlAttributes("Label", "UserMacros"));

  {
    std::unique_ptr<XmlElementWriter> properties =
        project.SubElement("PropertyGroup");
    {
      std::unique_ptr<XmlElementWriter> out_dir =
          properties->SubElement("OutDir");
      path_output.WriteDir(out_dir->StartContent(false),
                           build_settings_->build_dir(),
                           PathOutput::DIR_NO_LAST_SLASH);
    }
    properties->SubElement("TargetName")->Text("$(ProjectName)");
    if (target->output_type() != Target::GROUP) {
      properties->SubElement("TargetPath")
          ->Text("$(OutDir)\\$(ProjectName)$(TargetExt)");
    }
  }

  {
    std::unique_ptr<XmlElementWriter> item_definitions =
        project.SubElement("ItemDefinitionGroup");
    {
      std::unique_ptr<XmlElementWriter> cl_compile =
          item_definitions->SubElement("ClCompile");
      {
        std::unique_ptr<XmlElementWriter> include_dirs =
            cl_compile->SubElement("AdditionalIncludeDirectories");
        RecursiveTargetConfigToStream<SourceDir>(
            target, &ConfigValues::include_dirs, IncludeDirWriter(path_output),
            include_dirs->StartContent(false));
        include_dirs->Text(windows_kits_include_dirs_ +
                           "$(VSInstallDir)\\VC\\atlmfc\\include;" +
                           "%(AdditionalIncludeDirectories)");
      }
      CompilerOptions options;
      ParseCompilerOptions(target, &options);
      if (!options.additional_options.empty()) {
        cl_compile->SubElement("AdditionalOptions")
            ->Text(options.additional_options + "%(AdditionalOptions)");
      }
      if (!options.buffer_security_check.empty()) {
        cl_compile->SubElement("BufferSecurityCheck")
            ->Text(options.buffer_security_check);
      }
      cl_compile->SubElement("CompileAsWinRT")->Text("false");
      cl_compile->SubElement("DebugInformationFormat")->Text("ProgramDatabase");
      if (!options.disable_specific_warnings.empty()) {
        cl_compile->SubElement("DisableSpecificWarnings")
            ->Text(options.disable_specific_warnings +
                   "%(DisableSpecificWarnings)");
      }
      cl_compile->SubElement("ExceptionHandling")->Text("false");
      if (!options.forced_include_files.empty()) {
        cl_compile->SubElement("ForcedIncludeFiles")
            ->Text(options.forced_include_files);
      }
      cl_compile->SubElement("MinimalRebuild")->Text("false");
      if (!options.optimization.empty())
        cl_compile->SubElement("Optimization")->Text(options.optimization);
      if (target->config_values().has_precompiled_headers()) {
        cl_compile->SubElement("PrecompiledHeader")->Text("Use");
        cl_compile->SubElement("PrecompiledHeaderFile")
            ->Text(target->config_values().precompiled_header());
      } else {
        cl_compile->SubElement("PrecompiledHeader")->Text("NotUsing");
      }
      {
        std::unique_ptr<XmlElementWriter> preprocessor_definitions =
            cl_compile->SubElement("PreprocessorDefinitions");
        RecursiveTargetConfigToStream<std::string>(
            target, &ConfigValues::defines, SemicolonSeparatedWriter(),
            preprocessor_definitions->StartContent(false));
        preprocessor_definitions->Text("%(PreprocessorDefinitions)");
      }
      if (!options.runtime_library.empty())
        cl_compile->SubElement("RuntimeLibrary")->Text(options.runtime_library);
      if (!options.treat_warning_as_error.empty()) {
        cl_compile->SubElement("TreatWarningAsError")
            ->Text(options.treat_warning_as_error);
      }
      if (!options.warning_level.empty())
        cl_compile->SubElement("WarningLevel")->Text(options.warning_level);
    }

    // We don't include resource compilation and link options as ninja files
    // are used to generate real build.
  }

  {
    std::unique_ptr<XmlElementWriter> group = project.SubElement("ItemGroup");
    if (!target->config_values().precompiled_source().is_null()) {
      group
          ->SubElement(
              "ClCompile", "Include",
              SourceFileWriter(path_output,
                               target->config_values().precompiled_source()))
          ->SubElement("PrecompiledHeader")
          ->Text("Create");
    }

    for (const SourceFile& file : target->sources()) {
      SourceFileType type = GetSourceFileType(file);
      if (type == SOURCE_H || type == SOURCE_CPP || type == SOURCE_C) {
        group->SubElement(type == SOURCE_H ? "ClInclude" : "ClCompile",
                          "Include", SourceFileWriter(path_output, file));
      }
    }
  }

  project.SubElement(
      "Import",
      XmlAttributes("Project", "$(VCTargetsPath)\\Microsoft.Cpp.targets"));
  project.SubElement(
      "Import",
      XmlAttributes("Project",
                    "$(VCTargetsPath)\\BuildCustomizations\\masm.targets"));
  project.SubElement("ImportGroup", XmlAttributes("Label", "ExtensionTargets"));

  std::string ninja_target = GetNinjaTarget(target);

  {
    std::unique_ptr<XmlElementWriter> build =
        project.SubElement("Target", XmlAttributes("Name", "Build"));
    build->SubElement(
        "Exec", XmlAttributes("Command",
                              "call ninja.exe -C $(OutDir) " + ninja_target));
  }

  {
    std::unique_ptr<XmlElementWriter> clean =
        project.SubElement("Target", XmlAttributes("Name", "Clean"));
    clean->SubElement(
        "Exec",
        XmlAttributes("Command",
                      "call ninja.exe -C $(OutDir) -tclean " + ninja_target));
  }

  return true;
}

void VisualStudioWriter::WriteFiltersFileContents(std::ostream& out,
                                                  const Target* target) {
  out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
  XmlElementWriter project(
      out, "Project",
      XmlAttributes("ToolsVersion", "4.0")
          .add("xmlns", "http://schemas.microsoft.com/developer/msbuild/2003"));

  std::ostringstream files_out;

  {
    std::unique_ptr<XmlElementWriter> filters_group =
        project.SubElement("ItemGroup");
    XmlElementWriter files_group(files_out, "ItemGroup", XmlAttributes(), 2);

    // File paths are relative to vcxproj files which are generated to out dirs.
    // Filters tree structure need to reflect source directories and be relative
    // to target file. We need two path outputs then.
    PathOutput file_path_output(GetTargetOutputDir(target),
                                build_settings_->root_path_utf8(),
                                EscapingMode::ESCAPE_NONE);
    PathOutput filter_path_output(target->label().dir(),
                                  build_settings_->root_path_utf8(),
                                  EscapingMode::ESCAPE_NONE);

    std::set<std::string> processed_filters;

    for (const SourceFile& file : target->sources()) {
      SourceFileType type = GetSourceFileType(file);
      if (type == SOURCE_H || type == SOURCE_CPP || type == SOURCE_C) {
        std::unique_ptr<XmlElementWriter> cl_item = files_group.SubElement(
            type == SOURCE_H ? "ClInclude" : "ClCompile", "Include",
            SourceFileWriter(file_path_output, file));

        std::ostringstream target_relative_out;
        filter_path_output.WriteFile(target_relative_out, file);
        std::string target_relative_path = target_relative_out.str();
        ConvertPathToSystem(&target_relative_path);
        base::StringPiece filter_path = FindParentDir(&target_relative_path);

        if (!filter_path.empty()) {
          std::string filter_path_str = filter_path.as_string();
          while (processed_filters.find(filter_path_str) ==
                 processed_filters.end()) {
            auto it = processed_filters.insert(filter_path_str).first;
            filters_group
                ->SubElement("Filter",
                             XmlAttributes("Include", filter_path_str))
                ->SubElement("UniqueIdentifier")
                ->Text(MakeGuid(filter_path_str, kGuidSeedFilter));
            filter_path_str = FindParentDir(&(*it)).as_string();
            if (filter_path_str.empty())
              break;
          }
          cl_item->SubElement("Filter")->Text(filter_path);
        }
      }
    }
  }

  project.Text(files_out.str());
}

bool VisualStudioWriter::WriteSolutionFile(const std::string& sln_name,
                                           Err* err) {
  std::string name = sln_name.empty() ? "all" : sln_name;
  SourceFile sln_file = build_settings_->build_dir().ResolveRelativeFile(
      Value(nullptr, name + ".sln"), err);
  if (sln_file.is_null())
    return false;

  base::FilePath sln_path = build_settings_->GetFullPath(sln_file);

  std::stringstream string_out;
  WriteSolutionFileContents(string_out, sln_path.DirName());

  // Only write the content to the file if it's different. That is
  // both a performance optimization and more importantly, prevents
  // Visual Studio from reloading the projects.
  return WriteFileIfChanged(sln_path, string_out.str(), err);
}

void VisualStudioWriter::WriteSolutionFileContents(
    std::ostream& out,
    const base::FilePath& solution_dir_path) {
  out << "Microsoft Visual Studio Solution File, Format Version 12.00"
      << std::endl;
  out << "# " << version_string_ << std::endl;

  SourceDir solution_dir(FilePathToUTF8(solution_dir_path));
  for (const SolutionEntry* folder : folders_) {
    out << "Project(\"" << kGuidTypeFolder << "\") = \"(" << folder->name
        << ")\", \"" << RebasePath(folder->path, solution_dir) << "\", \""
        << folder->guid << "\"" << std::endl;
    out << "EndProject" << std::endl;
  }

  for (const SolutionEntry* project : projects_) {
    out << "Project(\"" << kGuidTypeProject << "\") = \"" << project->name
        << "\", \"" << RebasePath(project->path, solution_dir) << "\", \""
        << project->guid << "\"" << std::endl;
    out << "EndProject" << std::endl;
  }

  out << "Global" << std::endl;

  out << "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution"
      << std::endl;
  const std::string config_mode_prefix = std::string(kConfigurationName) + '|';
  const std::string config_mode = config_mode_prefix + config_platform_;
  out << "\t\t" << config_mode << " = " << config_mode << std::endl;
  out << "\tEndGlobalSection" << std::endl;

  out << "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution"
      << std::endl;
  for (const SolutionProject* project : projects_) {
    const std::string project_config_mode =
        config_mode_prefix + project->config_platform;
    out << "\t\t" << project->guid << '.' << config_mode
        << ".ActiveCfg = " << project_config_mode << std::endl;
    out << "\t\t" << project->guid << '.' << config_mode
        << ".Build.0 = " << project_config_mode << std::endl;
  }
  out << "\tEndGlobalSection" << std::endl;

  out << "\tGlobalSection(SolutionProperties) = preSolution" << std::endl;
  out << "\t\tHideSolutionNode = FALSE" << std::endl;
  out << "\tEndGlobalSection" << std::endl;

  out << "\tGlobalSection(NestedProjects) = preSolution" << std::endl;
  for (const SolutionEntry* folder : folders_) {
    if (folder->parent_folder) {
      out << "\t\t" << folder->guid << " = " << folder->parent_folder->guid
          << std::endl;
    }
  }
  for (const SolutionEntry* project : projects_) {
    out << "\t\t" << project->guid << " = " << project->parent_folder->guid
        << std::endl;
  }
  out << "\tEndGlobalSection" << std::endl;

  out << "EndGlobal" << std::endl;
}

void VisualStudioWriter::ResolveSolutionFolders() {
  root_folder_path_.clear();

  // Get all project directories. Create solution folder for each directory.
  std::map<base::StringPiece, SolutionEntry*> processed_paths;
  for (SolutionProject* project : projects_) {
    base::StringPiece folder_path = project->label_dir_path;
    if (IsSlash(folder_path[folder_path.size() - 1]))
      folder_path = folder_path.substr(0, folder_path.size() - 1);
    auto it = processed_paths.find(folder_path);
    if (it != processed_paths.end()) {
      project->parent_folder = it->second;
    } else {
      std::string folder_path_str = folder_path.as_string();
      SolutionEntry* folder = new SolutionEntry(
          FindLastDirComponent(SourceDir(folder_path)).as_string(),
          folder_path_str, MakeGuid(folder_path_str, kGuidSeedFolder));
      folders_.push_back(folder);
      project->parent_folder = folder;
      processed_paths[folder_path] = folder;

      if (root_folder_path_.empty()) {
        root_folder_path_ = folder_path_str;
      } else {
        size_t common_prefix_len = 0;
        size_t max_common_length =
            std::min(root_folder_path_.size(), folder_path.size());
        size_t i;
        for (i = common_prefix_len; i < max_common_length; ++i) {
          if (IsSlash(root_folder_path_[i]) && IsSlash(folder_path[i]))
            common_prefix_len = i + 1;
          else if (root_folder_path_[i] != folder_path[i])
            break;
        }
        if (i == max_common_length &&
            (i == folder_path.size() || IsSlash(folder_path[i])))
          common_prefix_len = max_common_length;
        if (common_prefix_len < root_folder_path_.size()) {
          if (IsSlash(root_folder_path_[common_prefix_len - 1]))
            --common_prefix_len;
          root_folder_path_ = root_folder_path_.substr(0, common_prefix_len);
        }
      }
    }
  }

  // Create also all parent folders up to |root_folder_path_|.
  SolutionFolders additional_folders;
  for (SolutionEntry* folder : folders_) {
    if (folder->path == root_folder_path_)
      continue;

    base::StringPiece parent_path;
    while ((parent_path = FindParentDir(&folder->path)) != root_folder_path_) {
      auto it = processed_paths.find(parent_path);
      if (it != processed_paths.end()) {
        folder = it->second;
      } else {
        folder = new SolutionEntry(
            FindLastDirComponent(SourceDir(parent_path)).as_string(),
            parent_path.as_string(),
            MakeGuid(parent_path.as_string(), kGuidSeedFolder));
        additional_folders.push_back(folder);
        processed_paths[parent_path] = folder;
      }
    }
  }
  folders_.insert(folders_.end(), additional_folders.begin(),
                  additional_folders.end());

  // Sort folders by path.
  std::sort(folders_.begin(), folders_.end(),
            [](const SolutionEntry* a, const SolutionEntry* b) {
              return a->path < b->path;
            });

  // Match subfolders with their parents. Since |folders_| are sorted by path we
  // know that parent folder always precedes its children in vector.
  SolutionFolders parents;
  for (SolutionEntry* folder : folders_) {
    while (!parents.empty()) {
      if (base::StartsWith(folder->path, parents.back()->path,
                           base::CompareCase::SENSITIVE)) {
        folder->parent_folder = parents.back();
        break;
      } else {
        parents.pop_back();
      }
    }
    parents.push_back(folder);
  }
}

std::string VisualStudioWriter::GetNinjaTarget(const Target* target) {
  std::ostringstream ninja_target_out;
  DCHECK(!target->dependency_output_file().value().empty());
  ninja_path_output_.WriteFile(ninja_target_out,
                               target->dependency_output_file());
  return ninja_target_out.str();
}
