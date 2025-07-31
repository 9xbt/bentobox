# [root@bentobox ~]#
# PS1='[\u@\h \W]\$ '

# root@bentobox:~#
# PS1='\[\033[01;32m\]\u@\h\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]\$ '

# root@bentobox [ ~ ]#
PS1="\[\e[1;31m\]\u@\h [ \[\e[0m\]\w\[\e[1;31m\] ]# \[\e[0m\]"

alias setfont='setfont -C /dev/console'
alias reboot='reboot -f'
