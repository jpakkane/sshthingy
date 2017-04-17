project('ssh thingy', 'cpp',
  default_options : 'cpp_std=c++14')

ssh_dep = dependency('libssh')
vte_dep = dependency('vte-2.91')

executable('sshprog', 'main.cpp', 'ssh_util.cpp', 'util.cpp',
  dependencies : [vte_dep, ssh_dep])
