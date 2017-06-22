project('ssh thingy', 'cpp',
  default_options : 'cpp_std=c++14')

ssh_dep = dependency('libssh')
vte_dep = dependency('vte-2.91')

emb = find_program('embedder.py')

emb_sources = custom_target('glades',
  output : ['glades.cpp', 'glades.hpp'],
  input : ['connectiondialog.glade', 'createforwarding.glade', 'forwardings.glade', 'sftpwindow.glade'],
  command : [emb, '@OUTPUT0@', '@OUTPUT1@', '@INPUT@'],
  )

executable('sshprog', 'main.cpp', 'sftp.cpp', 'forwards.cpp', 'ssh_util.cpp', 'util.cpp',
  emb_sources,
  dependencies : [vte_dep, ssh_dep],
  install : true)

install_data('sshthingy.desktop',
  install_dir : join_paths(get_option('datadir'), 'applications'))
  install_data('sshprog.png',
    install_dir : join_paths(get_option('datadir'), 'icons/hicolor/48x48/apps'))
