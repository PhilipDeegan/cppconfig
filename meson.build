project(
  'cppconfig',
  'cpp',
  version : '1.0.0',
  default_options : ['warning_level=3', 'cpp_std=c++17'],
  license : 'GPL3'
)

yaml_cpp_dep = dependency('yaml-cpp')
nlohmann_json_dep = dependency('nlohmann_json')
cppdict_dep = dependency('cppdict')
catch_dep = dependency('catch2-with-main', version:'>3.0.0', required : true)

cppconfig_dep = declare_dependency(
                        include_directories: include_directories('include'),
                        dependencies:[yaml_cpp_dep, nlohmann_json_dep, cppdict_dep]
                        )

library('cppconfig', extra_files:['include/cppconfig/cppconfig.hpp', 'include/cppconfig/io/yaml.hpp', 'include/cppconfig/io/json.hpp'])

foreach test:['yaml','json']
    exe = executable(test,'tests/'+test+'/test.cpp',
                    dependencies:[cppconfig_dep, catch_dep],
                    cpp_args : '-DCATCH_CONFIG_NO_POSIX_SIGNALS',
                    install: false
                    )
    test(test, exe)
endforeach
