project('ssh thingy', 'cpp',
  default_options : 'cpp_std=c++14')

ssh_dep = dependency('libssh')
vte_dep = dependency('vte-2.91')

executable('sshprog', 'main.cpp', 'sftp.cpp', 'forwards.cpp', 'ssh_util.cpp', 'util.cpp',
  dependencies : [vte_dep, ssh_dep],
  install : true)

install_data('sshthingy.desktop',
  install_dir : join_paths(get_option('datadir'), 'applications'))
  install_data('sshprog.png',
    install_dir : join_paths(get_option('datadir'), 'icons/hicolor/48x48/apps'))
