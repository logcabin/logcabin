#! /bin/bash

# Works with:
# - Ubuntu 22.04.2 LTS 
# - emulated on Parallels 19.4
# - on macOS 14.5 (ARM) 

sudo apt-get -y update
sudo apt-get -y upgrade

# Stow
sudo apt-get -y install stow

# My dotfiles
rm -rf "$HOME/dotfiles/"
mkdir "$HOME/dotfiles/"
git clone "https://github.com/alextsagkas/dotfiles.git" "$HOME/dotfiles"
stow --dir="$HOME/dotfiles" --target="$HOME/" .

# tmux
rm -rf "$HOME/.tmux/plugins/tpm/"
mkdir -p "$HOME/.tmux/plugins/tpm/"
git clone "https://github.com/tmux-plugins/tpm.git" "$HOME/.tmux/plugins/tpm"
sudo apt-get -y install tmux

# python2
sudo apt-get -y install python2
sudo ln -s /usr/bin/python2 /usr/bin/python
# pip for python2
url https://bootstrap.pypa.io/pip/2.7/get-pip.py --output get-pip.py
sudo python2 get-pip.py
# Python packages in PATH
echo 'export PATH=$PATH:"/home/parallels/.local/bin/":"/home/parallels/.local/lib/python2.7/site-packages"' >> ~/.bashrc
source ~/.bashrc
# Install scons 
sudo pip install SCons==3.1.2

# Protobuf Compiler
sudo apt install -y protobuf-compiler

# Crtypto++
sudo apt-get install -y libcrypto++-dev libcrypto++-doc libcrypto++-utils

# Doxygen
sudo apt-get install -y doxygen

# Valgrind
sudo apt-get install -y valgrind

# Ping
sudo apt-get install -y inetutils-ping