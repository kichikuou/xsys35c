project('xsys35c', 'c')
fs = import('fs')

is_windows = host_machine.system() == 'windows'

if host_machine.system() == 'emscripten'
  zlib = declare_dependency(compile_args : ['-s', 'USE_ZLIB=1'], link_args : ['-s', 'USE_ZLIB=1'])
  png = declare_dependency(compile_args : ['-s', 'USE_LIBPNG=1'], link_args : ['-s', 'USE_LIBPNG=1'])
  common_link_args = [
    '-s', 'ENVIRONMENT=node',
    '-s', 'MODULARIZE',
    '-s', 'EXIT_RUNTIME',
    '-s', 'ALLOW_MEMORY_GROWTH',
    '-s', 'EXPORTED_RUNTIME_METHODS=["FS","NODEFS"]',
    '-O' + get_option('optimization'),
    '-lnodefs.js'
  ]
else
  zlib = dependency('zlib')
  png = dependency('libpng', static : is_windows)
  common_link_args = []
endif

#
# common
#

inc = include_directories('common')

common_srcs = [
  'common/ald.c',
  'common/container.c',
  'common/sjisutf.c',
  'common/util.c',
]

libcommon = static_library('common', common_srcs, include_directories : inc)
common = declare_dependency(include_directories : inc, link_with : libcommon, link_args : common_link_args)

common_tests_srcs = [
  'common/ald_test.c',
  'common/common_tests.c',
  'common/sjisutf_test.c',
  'common/util_test.c',
]

common_tests = executable('common_tests', common_tests_srcs, dependencies : common)
test('common_tests', common_tests, workdir : meson.current_source_dir())

#
# compiler
#

compiler_srcs = [
  'compiler/ain.c',
  'compiler/compile.c',
  'compiler/config.c',
  'compiler/debuginfo.c',
  'compiler/hel.c',
  'compiler/lexer.c',
  'compiler/sco.c',
]

libcompiler = static_library('compiler', compiler_srcs, dependencies : common)
compiler = declare_dependency(link_with : libcompiler)

xsys35c_srcs = [
  'compiler/xsys35c.c'
]
xsys35c = executable('xsys35c', xsys35c_srcs, dependencies : [common, compiler], install : true)

compiler_tests_srcs = [
  'compiler/compile_test.c',
  'compiler/compiler_tests.c',
  'compiler/hel_test.c',
  'compiler/sco_test.c',
]

compiler_tests = executable('compiler_tests', compiler_tests_srcs, dependencies : [common, compiler])
test('compiler_tests', compiler_tests, workdir : meson.current_source_dir())

#
# decompiler
#

xsys35dc_srcs = [
  'decompiler/ain.c',
  'decompiler/cali.c',
  'decompiler/decompile.c',
  'decompiler/preprocess.c',
  'decompiler/xsys35dc.c',
]
xsys35dc = executable('xsys35dc', xsys35dc_srcs, dependencies : common, install : true)

#
# tools
#

libpng_utils = static_library('png_utils', 'tools/png_utils.c', dependencies : [common, png])
png_utils = declare_dependency(link_with : libpng_utils)

ald = executable('ald', ['tools/ald.c'], dependencies : common, install : true)
alk = executable('alk', ['tools/alk.c'], dependencies : common, install : true)
vsp = executable('vsp', ['tools/vsp.c'], dependencies : [common, png, png_utils], install : true)
pms = executable('pms', ['tools/pms.c'], dependencies : [common, png, png_utils], install : true)
qnt = executable('qnt', ['tools/qnt.c'], dependencies : [common, png, png_utils, zlib], install : true)

#
# regression test
#

bash = find_program('bash')

test('regression_test',
  bash,
  args : ['./regression_test.sh', meson.current_build_dir()],
  workdir : meson.current_source_dir(),
  depends : [xsys35c, xsys35dc, ald, alk, vsp, pms, qnt])

#
# docs
#

docs = [
  ['docs/ald.adoc', ['ald.1']],
  ['docs/alk.adoc', ['alk.1']],
  ['docs/unicode.adoc', []],
  ['docs/vsp.adoc', ['vsp.1', 'pms.1', 'qnt.1']],
  ['docs/xsys35c.adoc', ['xsys35c.1']],
  ['docs/xsys35dc.adoc', ['xsys35dc.1']],
]

asciidoctor = find_program('asciidoctor', required : false)

if asciidoctor.found()
  foreach doc : docs
    adoc = doc[0]
    manpages = doc[1]

    if manpages.length() > 0
      custom_target(manpages[0],
        output : manpages,
        input : adoc,
        command : [asciidoctor, '-b', 'manpage', '@INPUT@', '-o', '@OUTPUT0@'],
        install : true,
        install_dir : get_option('mandir') / 'man1')
    endif

    html = fs.replace_suffix(fs.name(adoc), '.html')
    custom_target(html,
      output : html,
      input : adoc,
      command : [asciidoctor, '@INPUT@', '-o', '@OUTPUT0@'],
      build_by_default : true)
  endforeach
endif
